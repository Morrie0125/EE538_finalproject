#include "delta_hpwl.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace delta_hpwl {
namespace {

// Computes absolute (x,y) of a pin
// Each pin is defined relative to its node (dx, dy) so abs. pos = node pos + pin offset
// Throws if node index is invalid, pin index is invalid, or node is not place (neg. coordinates)
std::pair<long long, long long> absolute_pin_location(
    const PlacementState& state,
    const NetPinRef& ref)
{
    if (ref.nodeIdx < 0 || ref.nodeIdx >= static_cast<int>(state.nodes.size())) {
        throw std::runtime_error("delta_hpwl: invalid node index in net pin ref");
    }
    const Node& node = state.nodes[ref.nodeIdx];

    if (ref.pinIdx < 0 || ref.pinIdx >= static_cast<int>(node.pins.size())) {
        throw std::runtime_error("delta_hpwl: invalid pin index in net pin ref");
    }
    if (node.x < 0 || node.y < 0) {
        throw std::runtime_error("delta_hpwl: node has not been placed");
    }

    const Pin& pin = node.pins[ref.pinIdx];
    return {static_cast<long long>(node.x) + pin.dx,
            static_cast<long long>(node.y) + pin.dy};
}

// Compute HPWL for a single net in a given placement state.
// HPWL = (max_x - min_x) + (max_y - min_y)
// Scans all pins of the net and builds a bounding box. 
// Note that nets with <2 pins contribute 0 by definition
long long net_hpwl_for_state(const PlacementState& state, const Net& net)
{
    if (net.pins.size() < 2) {
        return 0;
    }

    long long min_x = std::numeric_limits<long long>::max();
    long long max_x = std::numeric_limits<long long>::min();
    long long min_y = std::numeric_limits<long long>::max();
    long long max_y = std::numeric_limits<long long>::min();

    for (const NetPinRef& ref : net.pins) {
        const auto [x, y] = absolute_pin_location(state, ref);
        min_x = std::min(min_x, x);
        max_x = std::max(max_x, x);
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
    }

    return (max_x - min_x) + (max_y - min_y);
}

} // namespace

DeltaHpwlResult compute_delta_hpwl(
    const PlacementState& before,
    const PlacementState& after,
    const adjacency::Adjacency& adj,
    const std::vector<int>& moved_nodes)
{
    if (before.nodes.size() != after.nodes.size()) {
        throw std::runtime_error("delta_hpwl: before/after node count mismatch");
    }
    if (before.nets.size() != after.nets.size()) {
        throw std::runtime_error("delta_hpwl: before/after net count mismatch");
    }
    if (adj.node_to_nets.size() != before.nodes.size()) {
        throw std::runtime_error("delta_hpwl: adjacency node_to_nets size mismatch");
    }

    std::unordered_set<int> affected_set;
    for (int node_idx : moved_nodes) {
        if (node_idx < 0 || node_idx >= static_cast<int>(before.nodes.size())) {
            throw std::runtime_error("delta_hpwl: moved node index out of range");
        }
        for (int net_idx : adj.node_to_nets[node_idx]) {
            affected_set.insert(net_idx);
        }
    }

    DeltaHpwlResult result;
    result.affected_nets.assign(affected_set.begin(), affected_set.end());
    std::sort(result.affected_nets.begin(), result.affected_nets.end());

    long long delta = 0;
    for (int net_idx : result.affected_nets) {
        const long long old_hpwl = net_hpwl_for_state(before, before.nets[net_idx]);
        const long long new_hpwl = net_hpwl_for_state(after, after.nets[net_idx]);
        delta += (new_hpwl - old_hpwl);
    }

    result.delta = delta;
    return result;
}

} // namespace delta_hpwl