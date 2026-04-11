#include <algorithm>    // for std::min, std::max
#include <limits>       // for numeric limits (init bounding box)
#include <stdexcept>    // for runtime_error
#include <string>

#include "../include/hpwl_engine.h"

namespace hpwl {
/*
Compute the absolute (x, y) location of a pin.
Convention:
    - pin position = node position + pin offset
    - (node.x + pin.dx, node.y + pin.dy)
This assumes nodes are already placed (x, y >= 0).
*/
std::pair<long long, long long> absolute_pin_location(const PlacementState& state,
                                                      const NetPinRef& ref) {
  // Validate node index
  if (ref.nodeIdx < 0 || ref.nodeIdx >= static_cast<int>(state.nodes.size())) {
    throw std::runtime_error("HPWL: net references an invalid node index.");
  }

  const Node& node = state.nodes[ref.nodeIdx];

  // Validate pin index within the node
  if (ref.pinIdx < 0 || ref.pinIdx >= static_cast<int>(node.pins.size())) {
    throw std::runtime_error("HPWL: net references an invalid pin index.");
  }

  // Ensure node has been placed (simple validity check)
  if (node.x < 0 || node.y < 0) {
    throw std::runtime_error("HPWL: component not placed yet: " + node.id);
  }

  const Pin& pin = node.pins[ref.pinIdx];

  // Return absolute coordinates of the pin
  return {static_cast<long long>(node.x) + pin.dx,
          static_cast<long long>(node.y) + pin.dy};
}

/*
Compute HPWL (Half-Perimeter Wirelength) for a single net.
HPWL = (max_x - min_x) + (max_y - min_y)
This forms the bounding box around all pins in the net.
*/
long long net_hpwl(const PlacementState& state, const Net& net) {
  // Nets with fewer than 2 pins have no wirelength
  if (net.pins.size() < 2) {
    return 0;
  }

  // Initialize bounding box extremes
  long long min_x = std::numeric_limits<long long>::max();
  long long max_x = std::numeric_limits<long long>::min();
  long long min_y = std::numeric_limits<long long>::max();
  long long max_y = std::numeric_limits<long long>::min();

  // Iterate over all pins in the net
  for (const NetPinRef& ref : net.pins) {
    // Get absolute pin position
    const auto [x, y] = absolute_pin_location(state, ref);

    // Update bounding box
    min_x = std::min(min_x, x);
    max_x = std::max(max_x, x);
    min_y = std::min(min_y, y);
    max_y = std::max(max_y, y);
  }

  // Return half-perimeter of the bounding box
  return (max_x - min_x) + (max_y - min_y);
}

/*
Compute total HPWL across all nets in the placement.
This is the objective function used in placement optimization.
*/
long long total_hpwl(const PlacementState& state) {
  long long total = 0;

  // Sum HPWL over all nets
  for (const Net& net : state.nets) {
    total += net_hpwl(state, net);
  }

  return total;
}

}  // namespace hpwl