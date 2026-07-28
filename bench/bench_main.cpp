// Benchmark harness.
//
// Two decisions worth explaining, because both were mistakes on the first try:
//
// 1. RATIOS, NOT ABSOLUTE NANOSECONDS.
//    Absolute per-op figures depend on CPU, clock source, and timer overhead,
//    and do not transfer between machines. The ratio between two
//    implementations measured in the same process on the same input does.
//
// 2. BATCHED TIMING, NOT PER-OPERATION TIMING.
//    The first version timed every individual operation. On this machine
//    steady_clock has a resolution of roughly 40ns, which is larger than the
//    operations being measured, so every sample quantized to one or two clock
//    ticks and the p50 for both implementations came out identical. That
//    result was an artifact of the timer, not a property of the books. The
//    harness now times chunks of operations and reports ns/op, which pushes
//    the measured interval well above clock resolution.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "ob/array_book.hpp"
#include "ob/generator.hpp"
#include "ob/map_book.hpp"

using namespace ob;
using Clock = std::chrono::steady_clock;

namespace {

// Operations per timed chunk. Large enough that the interval dwarfs clock
// resolution, small enough that a slow chunk still shows up in the tail.
constexpr std::size_t kChunk = 256;
constexpr int kReps = 5;

struct Stats {
    double p50 = 0, p95 = 0, p99 = 0, mean = 0;
    std::size_t chunks = 0;
};

Stats summarize(std::vector<double> ns_per_op) {
    Stats s;
    if (ns_per_op.empty()) return s;
    std::sort(ns_per_op.begin(), ns_per_op.end());
    auto pct = [&](double q) {
        return ns_per_op[static_cast<std::size_t>(q * (ns_per_op.size() - 1))];
    };
    s.chunks = ns_per_op.size();
    s.p50 = pct(0.50);
    s.p95 = pct(0.95);
    s.p99 = pct(0.99);
    double sum = 0;
    for (double v : ns_per_op) sum += v;
    s.mean = sum / ns_per_op.size();
    return s;
}

// Measure the resolution of the clock we are about to trust.
double clock_resolution_ns() {
    double best = 1e18;
    for (int i = 0; i < 1000; ++i) {
        const auto a = Clock::now();
        Clock::time_point b;
        do { b = Clock::now(); } while (b == a);
        const double d = std::chrono::duration<double, std::nano>(b - a).count();
        if (d > 0 && d < best) best = d;
    }
    return best;
}

volatile std::uint64_t sink = 0;

struct Workload {
    std::vector<Op> warmup;
    std::vector<Op> adds;
    std::vector<Op> cancels;
    std::vector<Op> markets;
};

Workload build_workload(const GenConfig& cfg, std::size_t warm_n, std::size_t measure_n) {
    Workload w;
    GenConfig warm = cfg;
    warm.p_cancel = 0.0;
    warm.p_market = 0.0;
    w.warmup = generate(warm, warm_n);

    OrderId next = warm_n + 1000000;
    std::mt19937_64 rng(cfg.seed ^ 0x9E3779B97F4A7C15ULL);
    std::normal_distribution<double> px(0.0, cfg.price_sigma);
    std::uniform_int_distribution<std::uint32_t> qty(cfg.min_qty, cfg.max_qty);
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    // Adds are placed away from the touch so they rest instead of crossing.
    // That isolates insertion cost from matching cost.
    for (std::size_t i = 0; i < measure_n; ++i) {
        const Side side = unit(rng) < 0.5 ? Side::Buy : Side::Sell;
        double raw = static_cast<double>(cfg.mid) + px(rng);
        raw += (side == Side::Buy ? -1.0 : 1.0) * 40.0;
        Price p = static_cast<Price>(raw);
        p = std::max(cfg.min_price, std::min(cfg.max_price, p));
        w.adds.push_back(Op{Op::Kind::AddLimit, next++, side, p, qty(rng)});
    }
    // Cancels target exactly the orders we added, so every cancel hits.
    w.cancels.reserve(w.adds.size());
    for (const Op& a : w.adds) {
        w.cancels.push_back(Op{Op::Kind::Cancel, a.id, a.side, 0, 0});
    }
    for (std::size_t i = 0; i < measure_n / 10; ++i) {
        const Side side = unit(rng) < 0.5 ? Side::Buy : Side::Sell;
        w.markets.push_back(Op{Op::Kind::Market, next++, side, 0, qty(rng)});
    }
    return w;
}

template <typename Fn>
Stats time_chunked(const std::vector<Op>& ops, Fn&& fn) {
    std::vector<double> ns_per_op;
    ns_per_op.reserve(ops.size() / kChunk + 1);
    for (std::size_t i = 0; i < ops.size(); i += kChunk) {
        const std::size_t end = std::min(i + kChunk, ops.size());
        const auto t0 = Clock::now();
        for (std::size_t k = i; k < end; ++k) fn(ops[k]);
        const auto t1 = Clock::now();
        const double total = std::chrono::duration<double, std::nano>(t1 - t0).count();
        ns_per_op.push_back(total / static_cast<double>(end - i));
    }
    return summarize(std::move(ns_per_op));
}

struct Result {
    Stats add, cancel, market;
};

template <typename MakeBook>
Result measure(MakeBook make, const Workload& w) {
    // Median across repetitions, so one noisy run does not decide the result.
    std::vector<Result> reps;
    for (int r = 0; r < kReps; ++r) {
        auto book = make();
        for (const Op& op : w.warmup) apply(*book, op);
        book->clear_trades();

        Result res;
        res.add = time_chunked(w.adds, [&](const Op& op) {
            book->add_limit(op.id, op.side, op.price, op.qty);
        });
        res.cancel = time_chunked(w.cancels, [&](const Op& op) {
            sink += book->cancel(op.id) ? 1u : 0u;
        });
        res.market = time_chunked(w.markets, [&](const Op& op) {
            book->submit_market(op.id, op.side, op.qty);
        });
        reps.push_back(res);
    }
    auto median_of = [&](double Stats::*field, Stats Result::*op) {
        std::vector<double> v;
        for (const Result& r : reps) v.push_back((r.*op).*field);
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    Result out;
    for (auto op : {&Result::add, &Result::cancel, &Result::market}) {
        Stats s;
        s.p50 = median_of(&Stats::p50, op);
        s.p95 = median_of(&Stats::p95, op);
        s.p99 = median_of(&Stats::p99, op);
        s.mean = median_of(&Stats::mean, op);
        out.*op = s;
    }
    return out;
}

void print_row(const char* op, const Stats& m, const Stats& a) {
    auto ratio = [](double x, double y) { return y > 0 ? x / y : 0.0; };
    std::printf("  %-7s | %8.1f %8.1f %8.1f | %8.1f %8.1f %8.1f | %5.2fx %5.2fx %5.2fx\n",
                op, m.mean, m.p50, m.p99, a.mean, a.p50, a.p99,
                ratio(m.mean, a.mean), ratio(m.p50, a.p50), ratio(m.p99, a.p99));
}

}  // namespace

struct Scenario {
    const char* name;
    Price min_price;
    Price max_price;
    double price_sigma;
    const char* note;
};

void run_scenario(const Scenario& sc, std::size_t warm_n, std::size_t measure_n) {
    GenConfig cfg;
    cfg.min_price = sc.min_price;
    cfg.max_price = sc.max_price;
    cfg.mid = (sc.min_price + sc.max_price) / 2;
    cfg.price_sigma = sc.price_sigma;

    const Workload w = build_workload(cfg, warm_n, measure_n);

    const Result m = measure([&] { return std::make_unique<MapBook>(); }, w);
    const Result a =
        measure([&] { return std::make_unique<ArrayBook>(cfg.min_price, cfg.max_price); }, w);

    std::size_t level_bytes = 0, occupied = 0, slots = 0;
    {
        ArrayBook probe(cfg.min_price, cfg.max_price);
        for (const Op& op : w.warmup) apply(probe, op);
        level_bytes = probe.level_bytes();
        occupied = probe.occupied_levels();
        slots = probe.level_slots();
    }

    std::printf("\n=== %s ===\n", sc.name);
    std::printf("  %s\n", sc.note);
    std::printf("  price domain     : [%d, %d] = %d ticks, sigma %.0f\n", cfg.min_price,
                cfg.max_price, cfg.max_price - cfg.min_price + 1, cfg.price_sigma);
    std::printf("  array level slots: %zu reserved, %zu occupied (%.2f%% used), %.1f KB\n",
                slots, occupied, 100.0 * occupied / static_cast<double>(slots),
                level_bytes / 1024.0);

    std::printf("\n  nanoseconds per operation (lower is better)\n");
    std::printf("  %-7s | %26s | %26s | %s\n", "", "MapBook  mean/p50/p99",
                "ArrayBook  mean/p50/p99", "speedup mean/p50/p99");
    std::printf("  --------+----------------------------+------------------------"
                "----+----------------------\n");
    print_row("add", m.add, a.add);
    print_row("cancel", m.cancel, a.cancel);
    print_row("market", m.market, a.market);
}

int main(int argc, char** argv) {
    std::size_t warm_n = 200000;
    std::size_t measure_n = 200000;
    if (argc > 1) warm_n = std::stoul(argv[1]);
    if (argc > 2) measure_n = std::stoul(argv[2]);

    std::printf("order book benchmark\n");
    std::printf("  clock resolution : %.1f ns\n", clock_resolution_ns());
    std::printf("  chunk size       : %zu ops per timed sample\n", kChunk);
    std::printf("  repetitions      : %d (median reported)\n", kReps);
    std::printf("  warmup ops       : %zu\n", warm_n);

    const Scenario scenarios[] = {
        {"DENSE BOOK", 9000, 11000, 25.0,
         "Tight price range, orders clustered near the touch. This is the case the\n"
         "  flat array is designed for."},
        {"WIDE, SPARSE BOOK", 0, 200000, 25.0,
         "Same order flow, but the configured price domain is 100x wider. The array\n"
         "  must reserve every tick whether or not it ever trades."},
    };

    for (const Scenario& sc : scenarios) run_scenario(sc, warm_n, measure_n);

    std::printf("\n  Ratios are the portable result. Absolute ns depend on this machine.\n");
    if (sink == 12345678ULL) std::printf(" ");
    return 0;
}
