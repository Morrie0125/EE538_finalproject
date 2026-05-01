#ifndef TESTS_DELTA_CONSISTENCY_H
#define TESTS_DELTA_CONSISTENCY_H

#include <string>

// ============================================================
//  T21 — Delta vs Full Regression Suite
// ============================================================
//
//  Verifies that incremental HPWL evaluation (cached_cost + Δ)
//  always matches a full recompute, for both SWAP and RELOCATE
//  moves applied in random sequences.
//
//  On any mismatch the test prints:
//    • move type, involved node indices and names
//    • list of affected net ids
//    • per-net old HPWL  vs  new HPWL (delta path)
//    • per-net HPWL from full recompute
//    • cumulative cached cost vs full cost
//
//  Entry points
//  ------------
//  run_delta_consistency_tests()   – full battery (returns 0 on pass)
//  run_single_benchmark()          – one named benchmark file
// ============================================================

struct DeltaConsistencyConfig {
    int  num_moves        = 200;   // random moves per benchmark
    int  check_every      = 10;    // compare cached vs full every N moves
    unsigned seed         = 42;    // RNG seed
    bool verbose_on_pass  = false; // also print per-move info when passing
};

// Run all built-in test scenarios.
// Returns 0 if every check passes, non-zero otherwise.
int run_delta_consistency_tests(const DeltaConsistencyConfig& cfg = {});

// Run checks on a single benchmark file (absolute or relative path).
// Returns 0 on pass, 1 on any mismatch, -1 on file/setup error.
int run_single_benchmark(const std::string& benchmark_path,
                         const DeltaConsistencyConfig& cfg = {});

#endif // TESTS_DELTA_CONSISTENCY_H