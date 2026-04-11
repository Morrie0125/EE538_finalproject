#ifndef IO_ENGINE_H
#define IO_ENGINE_H

#include <string>

#include "types.h"

// Reads a netlist file and fills the placement state.
bool read_netlist(const std::string& path, PlacementState& state);

// Writes placement data and optional metadata to an output file.
bool write_placement(const std::string& path,
                     const PlacementState& state,
                     long long cost,
                     const std::string& meta = "");

#endif
