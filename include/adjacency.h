#ifndef ADJACENCY_H
#define ADJACENCY_H

#include <vector>
#include "types.h"

namespace adjacency {

struct Adjacency {
  // For each node index, list all incident net indices.
  std::vector<std::vector<int>> node_to_nets;

  // Simple per-net cache placeholder for future incremental evaluation.
  std::vector<long long> net_hpwl_cache;
};

Adjacency build_adjacency(const PlacementState& state);

// Small correctness check:
// for every net pin, the corresponding node must list that net.
void validate_adjacency(const PlacementState& state, const Adjacency& adj);

}  // namespace adjacency

#endif