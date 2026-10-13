#include "ob/array_book.hpp"

#include <algorithm>
#include <cassert>

namespace ob {

ArrayBook::ArrayBook(Price min_price, Price max_price, std::size_t pool_reserve)
    : min_price_(min_price), max_price_(max_price) {
    assert(max_price >= min_price);
    const std::size_t slots = static_cast<std::size_t>(max_price - min_price) + 1;
    bid_levels_.assign(slots, LevelHead{});
    ask_levels_.assign(slots, LevelHead{});

    // Build the free list up front so the hot path never calls the allocator.
    pool_.resize(pool_reserve);
    for (std::size_t i = 0; i < pool_.size(); ++i) {
        pool_[i].next = (i + 1 < pool_.size()) ? static_cast<std::int32_t>(i + 1) : kNil;
    }
    free_head_ = pool_.empty() ? kNil : 0;
}

std::int32_t ArrayBook::alloc_node() {
    if (free_head_ == kNil) {
        // Pool exhausted. Doubling keeps this amortized O(1) and, more
        // importantly, keeps it off the steady-state path entirely once the
        // book reaches its working size.
        const std::size_t old = pool_.size();
        pool_.resize(old == 0 ? 1024 : old * 2);
        for (std::size_t i = old; i < pool_.size(); ++i) {
            pool_[i].next = (i + 1 < pool_.size()) ? static_cast<std::int32_t>(i + 1) : kNil;
        }
        free_head_ = static_cast<std::int32_t>(old);
    }
    const std::int32_t idx = free_head_;
    free_head_ = pool_[idx].next;
    pool_[idx].prev = kNil;
    pool_[idx].next = kNil;
    return idx;
}

void ArrayBook::free_node(std::int32_t idx) {
    pool_[idx].next = free_head_;
    pool_[idx].prev = kNil;
    free_head_ = idx;
}

void ArrayBook::link_back(LevelHead& level, std::int32_t idx) {
    pool_[idx].prev = level.tail;
    pool_[idx].next = kNil;
    if (level.tail != kNil) {
        pool_[level.tail].next = idx;
    } else {
        level.head = idx;
    }
    level.tail = idx;
}

void ArrayBook::unlink(LevelHead& level, std::int32_t idx) {
    const std::int32_t p = pool_[idx].prev;
    const std::int32_t n = pool_[idx].next;
    if (p != kNil) pool_[p].next = n; else level.head = n;
    if (n != kNil) pool_[n].prev = p; else level.tail = p;
    pool_[idx].prev = kNil;
    pool_[idx].next = kNil;
}

// Cached best prices are only invalidated when the level they point at
// empties. Repair scans outward from there. Worst case is a full sweep, but
// that only happens when the book genuinely has nothing nearby.
void ArrayBook::repair_best_bid(std::int32_t from_off) {
    for (std::int32_t o = from_off; o >= 0; --o) {
        if (!bid_levels_[o].empty()) { best_bid_off_ = o; return; }
    }
    best_bid_off_ = kNil;
}

void ArrayBook::repair_best_ask(std::int32_t from_off) {
    const std::int32_t last = static_cast<std::int32_t>(ask_levels_.size()) - 1;
    for (std::int32_t o = from_off; o <= last; ++o) {
        if (!ask_levels_[o].empty()) { best_ask_off_ = o; return; }
    }
    best_ask_off_ = kNil;
}

Quantity ArrayBook::match_levels(std::vector<LevelHead>& levels, std::int32_t& best_off,
                                   bool taker_is_buy, OrderId taker, Quantity qty,
                                   const Price* limit) {
    while (qty > 0 && best_off != kNil) {
        const std::int32_t off = best_off;
        const Price level_price = price_at(off);
        // Buy taker can only lift offers at or below its limit.
        // Sell taker can only hit bids at or above its limit.
        // Market order (limit == nullptr) takes any price.
        if (limit != nullptr) {
            const bool acceptable = taker_is_buy ? (level_price <= *limit)
                                                 : (level_price >= *limit);
            if (!acceptable) break;
        }

        LevelHead& level = levels[off];
        while (qty > 0 && !level.empty()) {
            const std::int32_t maker_idx = level.head;
            Node& maker = pool_[maker_idx];
            const Quantity fill = std::min(qty, maker.qty);

            trades_.push_back(Trade{maker.id, taker, level_price, fill});
            maker.qty -= fill;
            qty -= fill;

            if (maker.qty == 0) {
                index_.erase(maker.id);
                unlink(level, maker_idx);
                free_node(maker_idx);
            }
        }
        if (level.empty()) {
            if (taker_is_buy) repair_best_ask(off);
            else repair_best_bid(off);
        }
    }
    return qty;
}

Quantity ArrayBook::match(OrderId taker, Side taker_side, Quantity qty, const Price* limit) {
    if (taker_side == Side::Buy) {
        return match_levels(ask_levels_, best_ask_off_, true, taker, qty, limit);
    }
    return match_levels(bid_levels_, best_bid_off_, false, taker, qty, limit);
}

void ArrayBook::rest(OrderId id, Side side, Price price, Quantity qty) {
    const std::int32_t off = offset_of(price);
    const std::int32_t idx = alloc_node();
    pool_[idx].id = id;
    pool_[idx].qty = qty;
    pool_[idx].price = price;
    pool_[idx].side = side;

    if (side == Side::Buy) {
        link_back(bid_levels_[off], idx);
        if (best_bid_off_ == kNil || off > best_bid_off_) best_bid_off_ = off;
    } else {
        link_back(ask_levels_[off], idx);
        if (best_ask_off_ == kNil || off < best_ask_off_) best_ask_off_ = off;
    }
    index_.emplace(id, idx);
}

void ArrayBook::add_limit(OrderId id, Side side, Price price, Quantity qty) {
    if (qty == 0) return;
    if (!in_range(price)) return;  // outside the configured price domain
    const Quantity remaining = match(id, side, qty, &price);
    if (remaining > 0) rest(id, side, price, remaining);
}

void ArrayBook::submit_market(OrderId id, Side side, Quantity qty) {
    if (qty == 0) return;
    match(id, side, qty, nullptr);
}

bool ArrayBook::cancel(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;

    const std::int32_t idx = it->second;
    index_.erase(it);

    const Node& node = pool_[idx];
    const std::int32_t off = offset_of(node.price);
    const Side side = node.side;

    if (side == Side::Buy) {
        LevelHead& level = bid_levels_[off];
        unlink(level, idx);
        free_node(idx);
        if (level.empty() && off == best_bid_off_) repair_best_bid(off);
    } else {
        LevelHead& level = ask_levels_[off];
        unlink(level, idx);
        free_node(idx);
        if (level.empty() && off == best_ask_off_) repair_best_ask(off);
    }
    return true;
}

bool ArrayBook::best_bid(Price& out) const {
    if (best_bid_off_ == kNil) return false;
    out = price_at(best_bid_off_);
    return true;
}

bool ArrayBook::best_ask(Price& out) const {
    if (best_ask_off_ == kNil) return false;
    out = price_at(best_ask_off_);
    return true;
}

}  // namespace ob
