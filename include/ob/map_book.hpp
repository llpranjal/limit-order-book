#pragma once

#include <functional>
#include <list>
#include <map>
#include <unordered_map>

#include "ob/book.hpp"

namespace ob {

// Baseline implementation. This is what you write first, and what most
// textbook descriptions of an order book look like.
//
//   std::map      keeps price levels sorted, so best bid/ask is map.begin()
//   std::list     preserves time priority within a level, FIFO
//   unordered_map maps order id to its list node so cancel does not scan
//
// Costs: every price level is a red-black tree node reached by pointer
// chasing, and every resting order is a separate heap allocation.
class MapBook final : public Book {
public:
    void add_limit(OrderId id, Side side, Price price, Quantity qty) override;
    bool cancel(OrderId id) override;
    void submit_market(OrderId id, Side side, Quantity qty) override;

    bool best_bid(Price& out) const override;
    bool best_ask(Price& out) const override;
    std::size_t resting_count() const override { return index_.size(); }
    const char* name() const override { return "MapBook"; }

private:
    struct Order {
        OrderId id;
        Quantity qty;
    };

    using Level = std::list<Order>;
    // Bids descend so that begin() is the highest bid. Asks ascend so that
    // begin() is the lowest ask. Both sides then read "best is begin()".
    using BidLevels = std::map<Price, Level, std::greater<Price>>;
    using AskLevels = std::map<Price, Level, std::less<Price>>;

    struct Location {
        Side side;
        Price price;
        Level::iterator node;
    };

    // limit == nullptr means "no price bound", i.e. a market order.
    Quantity match(OrderId taker, Side taker_side, Quantity qty, const Price* limit);

    template <typename Levels>
    Quantity match_side(Levels& levels, OrderId taker, Quantity qty, const Price* limit,
                        bool taker_is_buy);

    void rest(OrderId id, Side side, Price price, Quantity qty);

    BidLevels bids_;
    AskLevels asks_;
    std::unordered_map<OrderId, Location> index_;
};

}  // namespace ob
