#pragma once

#include <cstdint>
#include <ostream>

namespace ob {

// Prices are integer ticks, never floating point. Two orders at "the same
// price" must compare exactly equal, and floating point cannot promise that.
using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

inline Side opposite(Side s) { return s == Side::Buy ? Side::Sell : Side::Buy; }

// A trade always prints at the resting (maker) order's price. The taker
// crossed the spread, so the maker's price is the one that was displayed.
struct Trade {
    OrderId maker_id;
    OrderId taker_id;
    Price price;
    Quantity quantity;

    bool operator==(const Trade& o) const {
        return maker_id == o.maker_id && taker_id == o.taker_id &&
               price == o.price && quantity == o.quantity;
    }
    bool operator!=(const Trade& o) const { return !(*this == o); }
};

inline std::ostream& operator<<(std::ostream& os, const Trade& t) {
    os << "Trade{maker=" << t.maker_id << ", taker=" << t.taker_id
       << ", px=" << t.price << ", qty=" << t.quantity << "}";
    return os;
}

}  // namespace ob
