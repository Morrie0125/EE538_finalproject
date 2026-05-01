// tests_delta_consistency.cpp
//
// T21 — Delta vs Full Regression Suite
//
// Strategy
// --------
// 1. Load a benchmark netlist into a PlacementDB.
// 2. Randomise the initial placement.
// 3. Build an Adjacency structure and initialise a running cost:
//      cached_cost = total_hpwl(state)
// 4. Apply a random sequence of RELOCATE and SWAP moves.
//    After each accepted move:
//      a. Compute delta via compute_delta_hpwl()  →  cached_cost += delta
//      b. Every `check_every` moves also compute full_cost = total_hpwl()
//      c. If |cached_cost - full_cost| > 0  →  MISMATCH: print diagnostics.
// 5. Summarise pass/fail for each benchmark.

#include "tests_delta_consistency.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "adjacency.h"
#include "delta_hpwl.h"
#include "hpwl_engine.h"
#include "placement_engine.h"
#include "types.h"

// ----------------------------------------------------------------
// Internal helpers
// ----------------------------------------------------------------

namespace {

// ANSI colour helpers (gracefully degrades on non-colour terminals)
const char* RED    = "\033[1;31m";
const char* GREEN  = "\033[1;32m";
const char* YELLOW = "\033[1;33m";
const char* CYAN   = "\033[1;36m";
const char* RESET  = "\033[0m";

// ----------------------------------------------------------------
// Snapshot the per-net HPWL for a set of net indices.
// Used to generate the "before" baseline for mismatch reporting.
// ----------------------------------------------------------------
std::vector<long long> snapshot_net_hpwl(const PlacementState& state,
                                          const std::vector<int>& net_ids) {
    std::vector<long long> out;
    out.reserve(net_ids.size());
    for (int ni : net_ids) {
        out.push_back(hpwl::net_hpwl(state, state.nets[ni]));
    }
    return out;
}

// ----------------------------------------------------------------
// Print a detailed mismatch report.
// ----------------------------------------------------------------
void print_mismatch(
        const PlacementState&            before,   // state prior to move
        const PlacementState&            after,    // state after move
        PlacementMoveType                move_type,
        int node_a, int node_b,                   // node_b == -1 for relocate
        int old_ax, int old_ay,
        int new_ax, int new_ay,
        int old_bx, int old_by,
        int new_bx, int new_by,
        const delta_hpwl::DeltaHpwlResult& dr,    // delta result
        const std::vector<long long>&    old_net_hpwl,
        long long cached_cost,
        long long full_cost)
{
    std::cout << "\n" << RED
              << "╔══════════════════════════════════════════════════════╗\n"
              << "║            DELTA CONSISTENCY MISMATCH                ║\n"
              << "╚══════════════════════════════════════════════════════╝"
              << RESET << "\n";

    // Move summary
    std::cout << YELLOW << "Move type : " << RESET
              << (move_type == PlacementMoveType::SWAP ? "SWAP" : "RELOCATE") << "\n";

    const std::string& name_a = before.nodes[node_a].id;
    std::cout << YELLOW << "Node A    : " << RESET
              << "[" << node_a << "] " << name_a
              << "  (" << old_ax << "," << old_ay << ") → ("
              << new_ax << "," << new_ay << ")\n";

    if (node_b >= 0) {
        const std::string& name_b = before.nodes[node_b].id;
        std::cout << YELLOW << "Node B    : " << RESET
                  << "[" << node_b << "] " << name_b
                  << "  (" << old_bx << "," << old_by << ") → ("
                  << new_bx << "," << new_by << ")\n";
    }

    // Per-net breakdown
    std::cout << "\n" << CYAN
              << std::left << std::setw(20) << "Net"
              << std::right << std::setw(14) << "old_hpwl(delta)"
              << std::setw(14) << "new_hpwl(delta)"
              << std::setw(14) << "new_hpwl(full)"
              << RESET << "\n";
    std::cout << std::string(62, '-') << "\n";

    for (size_t i = 0; i < dr.affected_nets.size(); ++i) {
        int ni = dr.affected_nets[i];
        long long old_h  = old_net_hpwl[i];
        long long new_h_delta = hpwl::net_hpwl(after, after.nets[ni]);
        long long new_h_full  = new_h_delta;  // same call; label differs for clarity

        std::cout << std::left  << std::setw(20) << after.nets[ni].id
                  << std::right << std::setw(14) << old_h
                                << std::setw(14) << new_h_delta
                                << std::setw(14) << new_h_full
                  << "\n";
    }

    // Cost summary
    std::cout << "\n" << YELLOW << "cached_cost : " << RESET << cached_cost << "\n";
    std::cout << YELLOW << "full_cost   : " << RESET << full_cost   << "\n";
    std::cout << RED    << "discrepancy : " << RESET
              << (cached_cost - full_cost) << "\n\n";
}

// ----------------------------------------------------------------
// Core test loop for one PlacementDB instance.
// Returns number of mismatches found.
// ----------------------------------------------------------------
int run_consistency_loop(PlacementDB& db,
                         const DeltaConsistencyConfig& cfg,
                         const std::string& label) {

    std::mt19937 rng(cfg.seed);

    // Rebuild adjacency after the random placement
    adjacency::Adjacency adj = adjacency::build_adjacency(db);

    long long cached_cost = hpwl::total_hpwl(db);
    int mismatches = 0;
    int move_idx   = 0;

    std::vector<int> movables = collect_movable_nodes(db);
    if (movables.size() < 2) {
        std::cout << YELLOW << "[SKIP] " << label
                  << " — fewer than 2 movable nodes\n" << RESET;
        return 0;
    }

    while (move_idx < cfg.num_moves) {

        // Snapshot state BEFORE the move (needed for delta and diagnostics)
        // PlacementDB IS a PlacementState (public inheritance), so we can
        // copy-construct the base to freeze positions.
        PlacementState before_state = static_cast<const PlacementState&>(db);

        // Choose move type alternately (swap / relocate)
        bool do_swap = (move_idx % 2 == 1);

        std::vector<int> moved_nodes;
        bool applied = false;

        if (do_swap) {
            applied = apply_random_swap_move(db, movables, rng, moved_nodes);
        } else {
            applied = apply_random_relocate_move(db, movables, rng, moved_nodes);
        }

        if (!applied || moved_nodes.empty()) {
            ++move_idx;
            continue;
        }

        // State AFTER the move
        PlacementState after_state = static_cast<const PlacementState&>(db);

        // Compute delta
        delta_hpwl::DeltaHpwlResult dr =
            delta_hpwl::compute_delta_hpwl(before_state, after_state, adj, moved_nodes);

        // Snapshot old per-net HPWL for diagnostics (must be done before update)
        std::vector<long long> old_net_hpwl =
            snapshot_net_hpwl(before_state, dr.affected_nets);

        cached_cost += dr.delta;
        ++move_idx;

        if (cfg.verbose_on_pass) {
            std::cout << "  move " << std::setw(4) << move_idx
                      << " | " << (do_swap ? "SWAP    " : "RELOCATE")
                      << " | delta=" << std::setw(8) << dr.delta
                      << " | cached=" << cached_cost << "\n";
        }

        // Periodic full-recompute check
        if (move_idx % cfg.check_every == 0) {
            long long full_cost = hpwl::total_hpwl(db);

            if (cached_cost != full_cost) {
                ++mismatches;

                // Gather move geometry for the report.
                // moved_nodes[0] is always node A; [1] exists for swap.
                int node_a = moved_nodes[0];
                int node_b = (moved_nodes.size() > 1) ? moved_nodes[1] : -1;

                int old_ax = before_state.nodes[node_a].x;
                int old_ay = before_state.nodes[node_a].y;
                int new_ax = after_state.nodes[node_a].x;
                int new_ay = after_state.nodes[node_a].y;

                int old_bx = -1, old_by = -1, new_bx = -1, new_by = -1;
                if (node_b >= 0) {
                    old_bx = before_state.nodes[node_b].x;
                    old_by = before_state.nodes[node_b].y;
                    new_bx = after_state.nodes[node_b].x;
                    new_by = after_state.nodes[node_b].y;
                }

                print_mismatch(before_state, after_state,
                               do_swap ? PlacementMoveType::SWAP
                                       : PlacementMoveType::RELOCATE,
                               node_a, node_b,
                               old_ax, old_ay, new_ax, new_ay,
                               old_bx, old_by, new_bx, new_by,
                               dr, old_net_hpwl,
                               cached_cost, full_cost);

                // Re-sync cached cost so subsequent moves are meaningful
                cached_cost = full_cost;
            }
        }
    }

    return mismatches;
}

// ----------------------------------------------------------------
// Run one complete test scenario.
// ----------------------------------------------------------------
int test_scenario(const std::string& benchmark_path,
                  const DeltaConsistencyConfig& cfg,
                  const std::string& label) {

    std::cout << CYAN << "\n[TEST] " << RESET << label
              << " (" << benchmark_path << ")\n";

    PlacementDB db;
    try {
        db.parseFile(benchmark_path);
    } catch (const std::exception& e) {
        std::cout << RED << "  ERROR loading file: " << e.what() << RESET << "\n";
        return -1;
    }

    db.randomLegalPlacement(cfg.seed);

    int mismatches = run_consistency_loop(db, cfg, label);

    if (mismatches == 0) {
        std::cout << GREEN << "  PASS" << RESET
                  << " — " << cfg.num_moves << " moves, checked every "
                  << cfg.check_every << "\n";
    } else {
        std::cout << RED << "  FAIL" << RESET
                  << " — " << mismatches << " mismatch(es) detected\n";
    }

    return (mismatches == 0) ? 0 : 1;
}

// ----------------------------------------------------------------
// Synthetic micro-benchmark: 3 nodes, 2 nets, fully deterministic.
// This is the "reproducible failing case" scaffold — if there is a
// bug in compute_delta_hpwl the tiny topology makes it easy to
// reason through by hand.
//
// Layout (grid 10×10):
//   N0 at (0,0), N1 at (5,0), N2 at (5,5)
//   Net0: N0-pin0, N1-pin0   →  initial HPWL = 5
//   Net1: N1-pin0, N2-pin0   →  initial HPWL = 5
//   total = 10
// ----------------------------------------------------------------
PlacementState make_synthetic_state() {
    PlacementState s;
    s.gridW = 10;
    s.gridH = 10;

    // Nodes
    Node n0; n0.id = "N0"; n0.w = 1; n0.h = 1; n0.x = 0; n0.y = 0;
    Pin p0; p0.name = "p"; p0.dx = 0; p0.dy = 0;
    n0.pins.push_back(p0); n0.pinNameToIdx["p"] = 0;

    Node n1; n1.id = "N1"; n1.w = 1; n1.h = 1; n1.x = 5; n1.y = 0;
    n1.pins.push_back(p0); n1.pinNameToIdx["p"] = 0;

    Node n2; n2.id = "N2"; n2.w = 1; n2.h = 1; n2.x = 5; n2.y = 5;
    n2.pins.push_back(p0); n2.pinNameToIdx["p"] = 0;

    s.nodes = {n0, n1, n2};
    s.nodeNameToIdx = {{"N0", 0}, {"N1", 1}, {"N2", 2}};

    // Nets
    Net net0; net0.id = "net0";
    net0.pins.push_back({0, 0}); net0.pins.push_back({1, 0});

    Net net1; net1.id = "net1";
    net1.pins.push_back({1, 0}); net1.pins.push_back({2, 0});

    s.nets = {net0, net1};

    return s;
}

int test_synthetic_relocate() {
    std::cout << CYAN << "\n[TEST] " << RESET << "synthetic_relocate\n";

    PlacementState before = make_synthetic_state();
    PlacementState after  = before;

    // Move N0 from (0,0) to (3,3)
    after.nodes[0].x = 3;
    after.nodes[0].y = 3;

    adjacency::Adjacency adj = adjacency::build_adjacency(before);
    std::vector<int> moved = {0};

    long long full_before = hpwl::total_hpwl(before);  // 10
    delta_hpwl::DeltaHpwlResult dr =
        delta_hpwl::compute_delta_hpwl(before, after, adj, moved);
    long long cached = full_before + dr.delta;
    long long full_after = hpwl::total_hpwl(after);

    bool ok = (cached == full_after);
    std::cout << (ok ? GREEN : RED)
              << (ok ? "  PASS" : "  FAIL") << RESET
              << " — before=" << full_before
              << " delta=" << dr.delta
              << " cached=" << cached
              << " full=" << full_after << "\n";

    if (!ok) {
        std::cout << RED << "  REPRODUCIBLE FAILING CASE:\n" << RESET;
        std::cout << "    N0 moved (0,0)→(3,3)\n";
        std::cout << "    net0 (N0-N1): old=" << hpwl::net_hpwl(before, before.nets[0])
                  << " new=" << hpwl::net_hpwl(after, after.nets[0]) << "\n";
        std::cout << "    net1 (N1-N2): old=" << hpwl::net_hpwl(before, before.nets[1])
                  << " new=" << hpwl::net_hpwl(after, after.nets[1]) << "\n";
        std::cout << "    affected nets returned by delta: ";
        for (int ni : dr.affected_nets) std::cout << ni << " ";
        std::cout << "\n";
    }

    return ok ? 0 : 1;
}

int test_synthetic_swap() {
    std::cout << CYAN << "\n[TEST] " << RESET << "synthetic_swap\n";

    PlacementState before = make_synthetic_state();
    PlacementState after  = before;

    // Swap N0 (0,0) and N2 (5,5)
    after.nodes[0].x = 5; after.nodes[0].y = 5;
    after.nodes[2].x = 0; after.nodes[2].y = 0;

    adjacency::Adjacency adj = adjacency::build_adjacency(before);
    std::vector<int> moved = {0, 2};

    long long full_before = hpwl::total_hpwl(before);
    delta_hpwl::DeltaHpwlResult dr =
        delta_hpwl::compute_delta_hpwl(before, after, adj, moved);
    long long cached = full_before + dr.delta;
    long long full_after = hpwl::total_hpwl(after);

    bool ok = (cached == full_after);
    std::cout << (ok ? GREEN : RED)
              << (ok ? "  PASS" : "  FAIL") << RESET
              << " — before=" << full_before
              << " delta=" << dr.delta
              << " cached=" << cached
              << " full=" << full_after << "\n";

    if (!ok) {
        std::cout << RED << "  REPRODUCIBLE FAILING CASE:\n" << RESET;
        std::cout << "    N0↔N2 swapped  N0:(0,0)→(5,5)  N2:(5,5)→(0,0)\n";
        for (int ni : {0, 1}) {
            std::cout << "    net" << ni << " (" << before.nets[ni].id << "):"
                      << " old=" << hpwl::net_hpwl(before, before.nets[ni])
                      << " new=" << hpwl::net_hpwl(after,  after.nets[ni]) << "\n";
        }
        std::cout << "    affected nets: ";
        for (int ni : dr.affected_nets) std::cout << ni << " ";
        std::cout << "\n";
    }

    return ok ? 0 : 1;
}

} // anonymous namespace

// ----------------------------------------------------------------
// Public API
// ----------------------------------------------------------------

int run_single_benchmark(const std::string& benchmark_path,
                         const DeltaConsistencyConfig& cfg) {
    return test_scenario(benchmark_path, cfg, benchmark_path);
}

int run_delta_consistency_tests(const DeltaConsistencyConfig& cfg) {

    std::cout << "================================================\n";
    std::cout << "  T21 — Delta vs Full HPWL Consistency Suite\n";
    std::cout << "================================================\n";
    std::cout << "moves=" << cfg.num_moves
              << "  check_every=" << cfg.check_every
              << "  seed=" << cfg.seed << "\n";

    int total_failures = 0;

    // --- Synthetic unit tests (always run, no file I/O required) ---
    total_failures += test_synthetic_relocate();
    total_failures += test_synthetic_swap();

    // --- Benchmark file tests ---
    // Paths assume the test binary is run from the project root.
    // Adjust as needed for your build system.
    struct BenchCase {
        std::string path;
        std::string label;
    };

    const std::vector<BenchCase> bench_cases = {
        {"bench/tiny_10.txt",          "tiny_10          (random, 10 nodes)"},
        {"bench/tiny_30.txt",          "tiny_30          (random, 30 nodes)"},
        {"bench/tiny_100.txt",         "tiny_100         (random, 100 nodes)"},
        {"bench/clustered_small.txt",  "clustered_small  (clustered, 20 nodes)"},
        {"bench/clustered_medium.txt", "clustered_medium (clustered, 50 nodes)"},
        {"bench/hub_small.txt",        "hub_small        (hub-heavy, 20 nodes)"},
        {"bench/hub_medium.txt",       "hub_medium       (hub-heavy, 50 nodes)"},
    };

    for (const auto& bc : bench_cases) {
        int result = test_scenario(bc.path, cfg, bc.label);
        if (result < 0) {
            // File not found — warn but don't count as failure
            std::cout << YELLOW << "  (skipped — file not found)\n" << RESET;
        } else {
            total_failures += result;
        }
    }

    // --- Summary ---
    std::cout << "\n================================================\n";
    if (total_failures == 0) {
        std::cout << GREEN << "  ALL CHECKS PASSED\n" << RESET;
    } else {
        std::cout << RED << "  " << total_failures << " SCENARIO(S) FAILED\n" << RESET;
    }
    std::cout << "================================================\n";

    return total_failures;
}

// ----------------------------------------------------------------
// main() — plain runner (no test framework dependency)
// ----------------------------------------------------------------
int main(int argc, char* argv[]) {
    DeltaConsistencyConfig cfg;

    // Optional CLI overrides:
    //   ./tests_delta_consistency [num_moves] [check_every] [seed]
    if (argc > 1) cfg.num_moves   = std::stoi(argv[1]);
    if (argc > 2) cfg.check_every = std::stoi(argv[2]);
    if (argc > 3) cfg.seed        = static_cast<unsigned>(std::stoul(argv[3]));

    // If a path is given as argv[1] and can't be parsed as an integer,
    // treat it as a single benchmark file.
    if (argc == 2) {
        std::string arg = argv[1];
        bool looks_like_path = (arg.find('/') != std::string::npos ||
                                arg.find('.') != std::string::npos);
        if (looks_like_path) {
            return run_single_benchmark(arg, cfg);
        }
    }

    return run_delta_consistency_tests(cfg);
}