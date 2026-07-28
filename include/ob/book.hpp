#pragma once

#include <cstddef>
#include <vector>

#include "ob/types.hpp"

namespace ob {

// Shared interface for both implementations.
//
// The whole point of this project is that two very different internal
// representations must be externally indistinguishable. The differential test
// drives the same order flow through both and asserts byte-identical trades.
// If the interface leaked implementation details, that test would be worthless.
class Book {
public:
    virtual ~Book() = default;

    // Add a limit order. Crosses against the opposite side while the price
    // permits, then rests whatever quantity remains.
    virtual void add_limit(OrderId id, Side side, Price price, Quantity qty) = 0;

    // Remove a resting order. Returns false if the id is unknown, which
    // happens legitimately when an order was already fully filled.
    virtual bool cancel(OrderId id) = 0;

    // Marketable order with no price limit. Never rests. Unfilled quantity
    // is discarded (immediate-or-cancel semantics).
    virtual void submit_market(OrderId id, Side side, Quantity qty) = 0;

    // Best prices. Return false when that side is empty.
    virtual bool best_bid(Price& out) const = 0;
    virtual bool best_ask(Price& out) const = 0;

    virtual std::size_t resting_count() const = 0;
    virtual const char* name() const = 0;

    const std::vector<Trade>& trades() const { return trades_; }
    void clear_trades() { trades_.clear(); }

protected:
    std::vector<Trade> trades_;
};

}  // namespace ob
