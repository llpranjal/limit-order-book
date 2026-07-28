// Hand-written scenarios that pin down the matching rules. These run against
// both implementations through the Book interface, so neither can drift.

#include <iostream>
#include <memory>
#include <vector>

#include "ob/array_book.hpp"
#include "ob/generator.hpp"
#include "ob/map_book.hpp"

using namespace ob;

static int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " [" << label \
                      << "] " << #cond << "\n";                                  \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static void test_resting_no_trade(Book& b, const char* label) {
    b.add_limit(1, Side::Buy, 100, 10);
    b.add_limit(2, Side::Sell, 101, 10);
    CHECK(b.trades().empty());
    Price p = 0;
    CHECK(b.best_bid(p) && p == 100);
    CHECK(b.best_ask(p) && p == 101);
    CHECK(b.resting_count() == 2);
}

static void test_full_cross(Book& b, const char* label) {
    b.add_limit(1, Side::Sell, 100, 10);
    b.add_limit(2, Side::Buy, 100, 10);
    CHECK(b.trades().size() == 1);
    CHECK(b.trades()[0] == (Trade{1, 2, 100, 10}));
    CHECK(b.resting_count() == 0);
    Price p = 0;
    CHECK(!b.best_bid(p));
    CHECK(!b.best_ask(p));
}

static void test_partial_fill_rests_remainder(Book& b, const char* label) {
    b.add_limit(1, Side::Sell, 100, 4);
    b.add_limit(2, Side::Buy, 100, 10);
    CHECK(b.trades().size() == 1);
    CHECK(b.trades()[0] == (Trade{1, 2, 100, 4}));
    // 6 of the buy is unfilled and must rest at 100.
    Price p = 0;
    CHECK(b.best_bid(p) && p == 100);
    CHECK(b.resting_count() == 1);
}

static void test_time_priority(Book& b, const char* label) {
    b.add_limit(1, Side::Sell, 100, 5);
    b.add_limit(2, Side::Sell, 100, 5);  // same price, arrived later
    b.add_limit(3, Side::Buy, 100, 5);
    CHECK(b.trades().size() == 1);
    // Order 1 must fill first. If this fails, FIFO within a level is broken.
    CHECK(b.trades()[0].maker_id == 1);
}

static void test_price_priority(Book& b, const char* label) {
    b.add_limit(1, Side::Sell, 102, 5);
    b.add_limit(2, Side::Sell, 100, 5);  // better price, arrived later
    b.add_limit(3, Side::Buy, 102, 10);
    CHECK(b.trades().size() == 2);
    // The cheaper offer must fill first even though it arrived second.
    CHECK(b.trades()[0].maker_id == 2);
    CHECK(b.trades()[0].price == 100);
    CHECK(b.trades()[1].maker_id == 1);
    CHECK(b.trades()[1].price == 102);
}

static void test_trade_prints_at_maker_price(Book& b, const char* label) {
    b.add_limit(1, Side::Sell, 100, 5);
    b.add_limit(2, Side::Buy, 105, 5);  // willing to pay 105
    CHECK(b.trades().size() == 1);
    // ...but pays 100, because that is the price that was displayed.
    CHECK(b.trades()[0].price == 100);
}

static void test_market_order_sweeps_and_discards(Book& b, const char* label) {
    b.add_limit(1, Side::Sell, 100, 5);
    b.add_limit(2, Side::Sell, 101, 5);
    b.submit_market(3, Side::Buy, 20);
    CHECK(b.trades().size() == 2);
    CHECK(b.trades()[0].price == 100);
    CHECK(b.trades()[1].price == 101);
    // 10 unfilled is discarded, not rested.
    CHECK(b.resting_count() == 0);
    Price p = 0;
    CHECK(!b.best_bid(p));
}

static void test_cancel(Book& b, const char* label) {
    b.add_limit(1, Side::Buy, 100, 5);
    b.add_limit(2, Side::Buy, 100, 5);
    CHECK(b.cancel(1));
    CHECK(!b.cancel(1));       // second cancel of same id fails
    CHECK(!b.cancel(999));     // unknown id fails
    CHECK(b.resting_count() == 1);
    b.add_limit(3, Side::Sell, 100, 5);
    // Order 1 is gone, so order 2 is now first in line.
    CHECK(b.trades().size() == 1);
    CHECK(b.trades()[0].maker_id == 2);
}

static void test_cancel_repairs_best(Book& b, const char* label) {
    b.add_limit(1, Side::Buy, 100, 5);
    b.add_limit(2, Side::Buy, 99, 5);
    Price p = 0;
    CHECK(b.best_bid(p) && p == 100);
    CHECK(b.cancel(1));
    // Cached best must fall back to the next occupied level.
    CHECK(b.best_bid(p) && p == 99);
    CHECK(b.cancel(2));
    CHECK(!b.best_bid(p));
}

static void test_cancel_after_fill_is_noop(Book& b, const char* label) {
    b.add_limit(1, Side::Sell, 100, 5);
    b.add_limit(2, Side::Buy, 100, 5);
    // Order 1 is fully filled, so its id is no longer cancellable.
    CHECK(!b.cancel(1));
    CHECK(b.resting_count() == 0);
}

using Scenario = void (*)(Book&, const char*);

int main() {
    const std::vector<std::pair<const char*, Scenario>> scenarios = {
        {"resting_no_trade", test_resting_no_trade},
        {"full_cross", test_full_cross},
        {"partial_fill_rests_remainder", test_partial_fill_rests_remainder},
        {"time_priority", test_time_priority},
        {"price_priority", test_price_priority},
        {"trade_prints_at_maker_price", test_trade_prints_at_maker_price},
        {"market_order_sweeps_and_discards", test_market_order_sweeps_and_discards},
        {"cancel", test_cancel},
        {"cancel_repairs_best", test_cancel_repairs_best},
        {"cancel_after_fill_is_noop", test_cancel_after_fill_is_noop},
    };

    for (const auto& [name, fn] : scenarios) {
        {
            MapBook b;
            fn(b, name);
        }
        {
            ArrayBook b(0, 200);
            fn(b, name);
        }
    }

    if (failures == 0) {
        std::cout << "correctness: all " << scenarios.size() * 2 << " scenario runs passed\n";
        return 0;
    }
    std::cerr << "correctness: " << failures << " check(s) failed\n";
    return 1;
}
