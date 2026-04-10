#ifndef HPWL_H
#define HPWL_H

#include <utility>

#include "types.h"

namespace hpwl {

// Returns the absolute pin coordinate based on current convention:
// pin = component lower-left corner + relative pin offset
std::pair<long long, long long> absolute_pin_location(const PlacementState& state,
                                                      const NetPinRef& ref);

// Half-perimeter wirelength of one net.
long long net_hpwl(const PlacementState& state, const Net& net);

// Sum of HPWL over all nets.
long long total_hpwl(const PlacementState& state);

}  // namespace hpwl

#endif  // HPWL_H
