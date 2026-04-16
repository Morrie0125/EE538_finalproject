#ifndef DELTA_HPWL_H
#define DELTA_HPWL_H

#include <vector>
#include "adjacency.h"

namespace delta_hpwl {

// Result of delta HPWL computation. Delta = sum over affected nets of (new_hpwl-old_hpwl)
struct DeltaHpwlResult {
    long long delta = 0;
    std::vector<int> affected_nets;
};

// Compute incremental change in HPWL after a move
// Instead of recomputing HPWL for all nets, we:
//      1. Identify nets touched by moved nodes via adjacency
//      2. Recompute only those nets
//      3. Return the delta cost
// before = the state before the move, after =  proposed state after the move.
DeltaHpwlResult compute_delta_hpwl(
    const PlacementState& before,
    const PlacementState& after,
    const adjacency::Adjacency& adj,
    const std::vector<int>& moved_nodes);

} // namespace delta_hpwl


#endif