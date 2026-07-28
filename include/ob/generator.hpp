#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include "ob/types.hpp"

namespace ob {

// A single instruction in a replayable order stream.
struct Op {
    enum class Kind : std::uint8_t { AddLimit, Cancel, Market };
    Kind kind;
    OrderId id;
    Side side;
    Price price;
    Quantity qty;
};

struct GenConfig {
    std::uint64_t seed = 42;
    Price min_price = 9000;
    Price max_price = 11000;
    Price mid = 10000;
    // Standard deviation, in ticks, of where new limit orders are placed
    // relative to the mid. Small values produce a dense book near the touch.
    double price_sigma = 25.0;
    Quantity min_qty = 1;
    Quantity max_qty = 100;
    double p_cancel = 0.30;
    double p_market = 0.05;
};

// Deterministic given the seed. Both books replay the identical stream, which
// is what makes the differential test meaningful: any divergence in output is
// a bug in one of the implementations, never a difference in input.
inline std::vector<Op> generate(const GenConfig& cfg, std::size_t n_ops) {
    std::mt19937_64 rng(cfg.seed);
    std::normal_distribution<double> price_dist(0.0, cfg.price_sigma);
    std::uniform_int_distribution<std::uint32_t> qty_dist(cfg.min_qty, cfg.max_qty);
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    std::vector<Op> ops;
    ops.reserve(n_ops);

    std::vector<OrderId> live;  // ids we have added, some may already be filled
    live.reserve(n_ops);

    OrderId next_id = 1;

    for (std::size_t i = 0; i < n_ops; ++i) {
        const double roll = unit(rng);
        const Side side = unit(rng) < 0.5 ? Side::Buy : Side::Sell;
        const Quantity qty = qty_dist(rng);

        if (roll < cfg.p_market) {
            ops.push_back(Op{Op::Kind::Market, next_id++, side, 0, qty});
            continue;
        }
        if (roll < cfg.p_market + cfg.p_cancel && !live.empty()) {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const std::size_t k = pick(rng);
            // Cancelling an already-filled id is legal and returns false. We
            // keep those in the stream on purpose, since both books must agree
            // on the failure case too.
            ops.push_back(Op{Op::Kind::Cancel, live[k], side, 0, 0});
            live[k] = live.back();
            live.pop_back();
            continue;
        }

        double raw = static_cast<double>(cfg.mid) + price_dist(rng);
        // Buyers bid below the mid, sellers offer above, so the book has a
        // spread most of the time instead of being permanently crossed.
        raw += (side == Side::Buy ? -1.0 : 1.0) * 2.0;
        Price px = static_cast<Price>(raw);
        if (px < cfg.min_price) px = cfg.min_price;
        if (px > cfg.max_price) px = cfg.max_price;

        const OrderId id = next_id++;
        ops.push_back(Op{Op::Kind::AddLimit, id, side, px, qty});
        live.push_back(id);
    }
    return ops;
}

inline void apply(Book& book, const Op& op) {
    switch (op.kind) {
        case Op::Kind::AddLimit: book.add_limit(op.id, op.side, op.price, op.qty); break;
        case Op::Kind::Cancel:   book.cancel(op.id); break;
        case Op::Kind::Market:   book.submit_market(op.id, op.side, op.qty); break;
    }
}

}  // namespace ob
