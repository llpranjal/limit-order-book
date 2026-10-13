#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ob/book.hpp"

namespace ob {

// Optimized implementation.
//
// Three changes from the baseline, each targeting a specific cost:
//
//   1. Price levels live in a flat vector indexed by (price - min_price).
//      Finding a level is an array index instead of a red-black tree descent.
//   2. Orders live in one preallocated pool and are linked by 32-bit indices
//      instead of pointers. The steady-state hot path performs no allocation.
//   3. Best bid and best ask are cached as offsets and repaired only when the
//      level at the cached offset empties out.
//
// The cost of all this is a bounded, dense price domain. The vector is sized
// for the entire configured range whether or not those prices ever trade, so
// a wide range or a sparse book wastes memory the std::map version would not.
// That tradeoff is the interesting part of the project, not the speedup.
class ArrayBook final : public Book {
public:
    ArrayBook(Price min_price, Price max_price, std::size_t pool_reserve = 1u << 16);

    void add_limit(OrderId id, Side side, Price price, Quantity qty) override;
    bool cancel(OrderId id) override;
    void submit_market(OrderId id, Side side, Quantity qty) override;

    bool best_bid(Price& out) const override;
    bool best_ask(Price& out) const override;
    std::size_t resting_count() const override { return index_.size(); }
    const char* name() const override { return "ArrayBook"; }

    // Exposed so the benchmark can report the memory cost of the price domain.
    std::size_t level_slots() const { return bid_levels_.size() * 2; }
    std::size_t pool_capacity() const { return pool_.size(); }

    // Memory reserved for the price domain regardless of how much of it is
    // ever used. This is the cost the std::map version does not pay.
    std::size_t level_bytes() const { return level_slots() * sizeof(LevelHead); }
    std::size_t pool_bytes() const { return pool_.size() * sizeof(Node); }

    // How many of the reserved levels actually hold an order right now.
    std::size_t occupied_levels() const {
        std::size_t n = 0;
        for (const LevelHead& l : bid_levels_) if (!l.empty()) ++n;
        for (const LevelHead& l : ask_levels_) if (!l.empty()) ++n;
        return n;
    }

private:
    static constexpr std::int32_t kNil = -1;

    // Intrusive list node. prev/next are indices into pool_, not pointers,
    // which halves the node size on 64-bit and keeps the pool relocatable.
    struct Node {
        OrderId id = 0;
        Quantity qty = 0;
        std::int32_t prev = kNil;
        std::int32_t next = kNil;
        Price price = 0;
        Side side = Side::Buy;
    };

    struct LevelHead {
        std::int32_t head = kNil;
        std::int32_t tail = kNil;
        bool empty() const { return head == kNil; }
    };

    std::int32_t offset_of(Price p) const { return p - min_price_; }
    Price price_at(std::int32_t off) const { return min_price_ + off; }
    bool in_range(Price p) const { return p >= min_price_ && p <= max_price_; }

    std::int32_t alloc_node();
    void free_node(std::int32_t idx);
    void link_back(LevelHead& level, std::int32_t idx);
    void unlink(LevelHead& level, std::int32_t idx);

    void repair_best_bid(std::int32_t from_off);
    void repair_best_ask(std::int32_t from_off);

    Quantity match(OrderId taker, Side taker_side, Quantity qty, const Price* limit);
    // Single matching loop shared by both sides. taker_is_buy selects the ask
    // array (scan up) vs the bid array (scan down) and flips the limit check.
    Quantity match_levels(std::vector<LevelHead>& levels, std::int32_t& best_off,
                          bool taker_is_buy, OrderId taker, Quantity qty,
                          const Price* limit);
    void rest(OrderId id, Side side, Price price, Quantity qty);

    Price min_price_;
    Price max_price_;

    std::vector<LevelHead> bid_levels_;
    std::vector<LevelHead> ask_levels_;
    std::vector<Node> pool_;
    std::int32_t free_head_ = kNil;

    std::unordered_map<OrderId, std::int32_t> index_;

    std::int32_t best_bid_off_ = kNil;
    std::int32_t best_ask_off_ = kNil;
};

}  // namespace ob
