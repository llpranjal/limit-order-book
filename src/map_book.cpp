#include "ob/map_book.hpp"

#include <algorithm>

namespace ob {

// Walk price levels from the best outward, filling against resting orders in
// FIFO order, until the incoming quantity is exhausted or the next level is
// no longer acceptable to the taker.
template <typename Levels>
Quantity MapBook::match_side(Levels& levels, OrderId taker, Quantity qty, const Price* limit,
                             bool taker_is_buy) {
    while (qty > 0 && !levels.empty()) {
        auto level_it = levels.begin();
        const Price level_price = level_it->first;

        // A market order has no limit and takes any price. A limit order stops
        // once the book's best price is worse than what the taker will pay.
        if (limit != nullptr) {
            const bool acceptable =
                taker_is_buy ? (level_price <= *limit) : (level_price >= *limit);
            if (!acceptable) break;
        }

        Level& queue = level_it->second;
        while (qty > 0 && !queue.empty()) {
            Order& maker = queue.front();
            const Quantity fill = std::min(qty, maker.qty);

            // Trade prints at the maker's price, not the taker's.
            trades_.push_back(Trade{maker.id, taker, level_price, fill});

            maker.qty -= fill;
            qty -= fill;

            if (maker.qty == 0) {
                index_.erase(maker.id);
                queue.pop_front();
            }
        }

        // Empty levels must go, otherwise begin() stops meaning "best price".
        if (queue.empty()) levels.erase(level_it);
    }
    return qty;
}

Quantity MapBook::match(OrderId taker, Side taker_side, Quantity qty, const Price* limit) {
    if (taker_side == Side::Buy) {
        return match_side(asks_, taker, qty, limit, true);
    }
    return match_side(bids_, taker, qty, limit, false);
}

void MapBook::rest(OrderId id, Side side, Price price, Quantity qty) {
    if (side == Side::Buy) {
        Level& queue = bids_[price];
        queue.push_back(Order{id, qty});
        index_.emplace(id, Location{side, price, std::prev(queue.end())});
    } else {
        Level& queue = asks_[price];
        queue.push_back(Order{id, qty});
        index_.emplace(id, Location{side, price, std::prev(queue.end())});
    }
}

void MapBook::add_limit(OrderId id, Side side, Price price, Quantity qty) {
    if (qty == 0) return;
    const Quantity remaining = match(id, side, qty, &price);
    if (remaining > 0) rest(id, side, price, remaining);
}

void MapBook::submit_market(OrderId id, Side side, Quantity qty) {
    if (qty == 0) return;
    match(id, side, qty, nullptr);  // no limit, and never rests
}

bool MapBook::cancel(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;  // already filled, or never existed

    const Location loc = it->second;
    index_.erase(it);

    if (loc.side == Side::Buy) {
        auto level_it = bids_.find(loc.price);
        level_it->second.erase(loc.node);
        if (level_it->second.empty()) bids_.erase(level_it);
    } else {
        auto level_it = asks_.find(loc.price);
        level_it->second.erase(loc.node);
        if (level_it->second.empty()) asks_.erase(level_it);
    }
    return true;
}

bool MapBook::best_bid(Price& out) const {
    if (bids_.empty()) return false;
    out = bids_.begin()->first;
    return true;
}

bool MapBook::best_ask(Price& out) const {
    if (asks_.empty()) return false;
    out = asks_.begin()->first;
    return true;
}

}  // namespace ob
