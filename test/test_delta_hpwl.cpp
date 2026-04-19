#include <cassert>
#include <iostream>
#include <random>
#include <vector>
#include <unordered_set>

#include "adjacency.h"
#include "delta_hpwl.h"

// Full HPWL recomputation over all nets
// This is the reference used in tests. We compare delta_hpwl == (full_after - full_before)
static long long full_hpwl(const PlacementState& s) {
    long long total = 0;

    for (const Net& net : s.nets) {
        if (net.pins.size() < 2) {
            continue;
        }

        long long min_x = std::numeric_limits<long long>::max();
        long long max_x = std::numeric_limits<long long>::min();
        long long min_y = std::numeric_limits<long long>::max();
        long long max_y = std::numeric_limits<long long>::min();

        for (const NetPinRef& ref : net.pins) {
            const Node& node = s.nodes.at(ref.nodeIdx);
            const Pin& pin = node.pins.at(ref.pinIdx);
            const long long x = static_cast<long long>(node.x) + pin.dx;
            const long long y = static_cast<long long>(node.y) + pin.dy;
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
        }

        total += (max_x - min_x) + (max_y - min_y);
    }

    return total;
}

// Small deterministic test case
// 3 nodes, 2 nets:
// n0: A-B
// n1: B-C
// Moving B should affect both nets
static PlacementState make_small_state() {
    PlacementState s;
    s.gridW = 10;
    s.gridH = 10;

    s.nodes.resize(3);
    s.nodes[0].id = "A";
    s.nodes[1].id = "B";
    s.nodes[2].id = "C";

    for (auto& n : s.nodes) {
        n.w = 1;
        n.h = 1;
        n.fixed = false;
        n.x = 0;
        n.y = 0;
    }

    s.nodes[0].pins = {Pin{"p0", 0, 0}};
    s.nodes[1].pins = {Pin{"p0", 0, 0}};
    s.nodes[2].pins = {Pin{"p0", 0, 0}};

    s.nets.resize(2);
    s.nets[0].id = "n0";
    s.nets[0].pins = {NetPinRef{0, 0}, NetPinRef{1, 0}};
    s.nets[1].id = "n1";
    s.nets[1].pins = {NetPinRef{1, 0}, NetPinRef{2, 0}};

    return s;
}

// Generate random but valid placement state
// Used to stress-test delta correctness across multiple scenarios
static PlacementState make_random_state(std::mt19937& rng) {
    PlacementState s;
    s.gridW = 20;
    s.gridH = 20;

    constexpr int kNumNodes = 6;
    constexpr int kNumNets = 5;

    s.nodes.resize(kNumNodes);
    for (int i = 0; i < kNumNodes; ++i) {
        s.nodes[i].id = "N" + std::to_string(i);
        s.nodes[i].w = 1;
        s.nodes[i].h = 1;
        s.nodes[i].fixed = false;
        s.nodes[i].x = static_cast<int>(rng() % 10);
        s.nodes[i].y = static_cast<int>(rng() % 10);
        s.nodes[i].pins = {
            Pin{"p0", 0, 0},
            Pin{"p1", 1, 0}
        };
    }

    std::uniform_int_distribution<int> node_dist(0, kNumNodes - 1);
    std::uniform_int_distribution<int> pin_dist(0, 1);
    for (int net_i = 0; net_i < kNumNets; ++net_i) {
        Net net;
        net.id = "net" + std::to_string(net_i);

        std::unordered_set<int> used_nodes;
        int pins_needed = 2 + static_cast<int>(rng() % 3); // 2..4 pins
        while (static_cast<int>(net.pins.size()) < pins_needed) {
            int node_idx = node_dist(rng);
            if (used_nodes.insert(node_idx).second) {
                net.pins.push_back(NetPinRef{node_idx, pin_dist(rng)});
            }
        }

        s.nets.push_back(std::move(net));
    }

    return s;
}

int main() {
    // Test 1: small deterministic case
    {
        PlacementState before = make_small_state();
        PlacementState after = before;

        after.nodes[1].x = 4;
        after.nodes[1].y = 2;

        auto adj = adjacency::build_adjacency(before);
        auto delta = delta_hpwl::compute_delta_hpwl(before, after, adj, {1});

        const long long full_before = full_hpwl(before);
        const long long full_after = full_hpwl(after);

        assert(delta.delta == (full_after - full_before));
        assert(delta.affected_nets.size() == 2);
        assert(delta.affected_nets[0] == 0);
        assert(delta.affected_nets[1] == 1);
    }

    // Test 2: randomized stress case
    {
        std::mt19937 rng(12345);

        for (int trial = 0; trial < 200; ++trial) {
            PlacementState before = make_random_state(rng);
            PlacementState after = before;

            std::uniform_int_distribution<int> node_dist(0, static_cast<int>(before.nodes.size()) - 1);
            std::uniform_int_distribution<int> pos_dist(0, 15);

            std::vector<int> moved_nodes;
            std::unordered_set<int> seen;
            int moves = 1 + static_cast<int>(rng() % 3);
            while (static_cast<int>(moved_nodes.size()) < moves) {
                int idx = node_dist(rng);
                if (seen.insert(idx).second) {
                    moved_nodes.push_back(idx);
                    after.nodes[idx].x = pos_dist(rng);
                    after.nodes[idx].y = pos_dist(rng);
                }
            }

            auto adj = adjacency::build_adjacency(before);
            auto delta = delta_hpwl::compute_delta_hpwl(before, after, adj, moved_nodes);

            const long long full_before = full_hpwl(before);
            const long long full_after = full_hpwl(after);

            assert(delta.delta == (full_after - full_before));
        }
    }

    std::cout << "All delta HPWL tests passed\n";
    return 0;
}