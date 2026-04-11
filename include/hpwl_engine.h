#ifndef HPWL_ENGINE_H
#define HPWL_ENGINE_H

#include <utility>

#include "types.h"

namespace hpwl {

// Returns absolute pin coordinates from node origin and pin offset.
std::pair<long long, long long> absolute_pin_location(const PlacementState& state,
                                                      const NetPinRef& ref);

// Computes half-perimeter wirelength for a single net.
long long net_hpwl(const PlacementState& state, const Net& net);

// Computes total half-perimeter wirelength for all nets.
long long total_hpwl(const PlacementState& state);

}  // namespace hpwl

#endif
