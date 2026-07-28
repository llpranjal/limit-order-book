// Differential test: the optimized book is only trustworthy if it is
// externally indistinguishable from the simple one. We replay identical
// seeded order flow through both and compare the full output after every
// single operation, so a divergence is reported at the op that caused it
// rather than thousands of ops later.

#include <iostream>
#include <vector>

#include "ob/array_book.hpp"
#include "ob/generator.hpp"
#include "ob/map_book.hpp"

using namespace ob;

namespace {

const char* kind_name(Op::Kind k) {
    switch (k) {
        case Op::Kind::AddLimit: return "AddLimit";
        case Op::Kind::Cancel:   return "Cancel";
        case Op::Kind::Market:   return "Market";
    }
    return "?";
}

// Returns the number of mismatches found for one seed.
int run_seed(std::uint64_t seed, std::size_t n_ops, bool verbose) {
    GenConfig cfg;
    cfg.seed = seed;
    const std::vector<Op> ops = generate(cfg, n_ops);

    MapBook a;
    ArrayBook b(cfg.min_price, cfg.max_price);

    int mismatches = 0;
    std::size_t total_trades = 0;

    for (std::size_t i = 0; i < ops.size(); ++i) {
        a.clear_trades();
        b.clear_trades();

        apply(a, ops[i]);
        apply(b, ops[i]);

        const auto& ta = a.trades();
        const auto& tb = b.trades();
        total_trades += ta.size();

        bool bad = false;
        if (ta.size() != tb.size()) {
            bad = true;
        } else {
            for (std::size_t k = 0; k < ta.size(); ++k) {
                if (ta[k] != tb[k]) { bad = true; break; }
            }
        }

        Price abid = 0, bbid = 0, aask = 0, bask = 0;
        const bool ahb = a.best_bid(abid), bhb = b.best_bid(bbid);
        const bool aha = a.best_ask(aask), bha = b.best_ask(bask);
        if (ahb != bhb || (ahb && abid != bbid)) bad = true;
        if (aha != bha || (aha && aask != bask)) bad = true;
        if (a.resting_count() != b.resting_count()) bad = true;

        if (bad) {
            ++mismatches;
            if (mismatches <= 5) {
                std::cerr << "MISMATCH seed=" << seed << " op#" << i << " ("
                          << kind_name(ops[i].kind) << " id=" << ops[i].id
                          << " px=" << ops[i].price << " qty=" << ops[i].qty << ")\n"
                          << "  trades  map=" << ta.size() << " array=" << tb.size() << "\n"
                          << "  resting map=" << a.resting_count()
                          << " array=" << b.resting_count() << "\n";
            }
        }
    }

    if (verbose) {
        std::cout << "  seed " << seed << ": " << ops.size() << " ops, "
                  << total_trades << " trades, "
                  << (mismatches == 0 ? "identical" : "DIVERGED") << "\n";
    }
    return mismatches;
}

}  // namespace

int main() {
    constexpr std::size_t kOpsPerSeed = 20000;
    constexpr std::uint64_t kSeeds = 50;

    std::cout << "differential: " << kSeeds << " seeds x " << kOpsPerSeed << " ops\n";

    int total = 0;
    for (std::uint64_t s = 1; s <= kSeeds; ++s) {
        total += run_seed(s, kOpsPerSeed, s <= 3);
    }

    if (total == 0) {
        std::cout << "differential: " << (kSeeds * kOpsPerSeed)
                  << " operations, zero divergence between MapBook and ArrayBook\n";
        return 0;
    }
    std::cerr << "differential: " << total << " diverging operations\n";
    return 1;
}
