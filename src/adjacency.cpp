#include "adjacency.h"

#include <stdexcept>
#include <string>

namespace adjacency {

Adjacency build_adjacency(const PlacementState& state) {
  Adjacency adj;
  adj.node_to_nets.assign(state.nodes.size(), {});
  adj.net_hpwl_cache.assign(state.nets.size(), 0);

  for (int net_idx = 0; net_idx < static_cast<int>(state.nets.size()); ++net_idx) {
    const Net& net = state.nets[net_idx];
    for (const NetPinRef& ref : net.pins) {
      if (ref.nodeIdx < 0 || ref.nodeIdx >= static_cast<int>(state.nodes.size())) {
        throw std::runtime_error("adjacency: net references invalid node index");
      }
      adj.node_to_nets[ref.nodeIdx].push_back(net_idx);
    }
  }

  return adj;
}

void validate_adjacency(const PlacementState& state, const Adjacency& adj) {
  if (adj.node_to_nets.size() != state.nodes.size()) {
    throw std::runtime_error("adjacency: node_to_nets size mismatch");
  }
  if (adj.net_hpwl_cache.size() != state.nets.size()) {
    throw std::runtime_error("adjacency: net_hpwl_cache size mismatch");
  }

  // Check that every pin's node lists its net.
  for (int net_idx = 0; net_idx < static_cast<int>(state.nets.size()); ++net_idx) {
    const Net& net = state.nets[net_idx];
    for (const NetPinRef& ref : net.pins) {
      const auto& incident = adj.node_to_nets.at(ref.nodeIdx);
      bool found = false;
      for (int listed_net : incident) {
        if (listed_net == net_idx) {
          found = true;
          break;
        }
      }
      if (!found) {
        throw std::runtime_error("adjacency: missing incidence for net " + net.id);
      }
    }
  }
}

}  // namespace adjacency