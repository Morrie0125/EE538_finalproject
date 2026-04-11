#ifndef PLACEMENT_ENGINE_H
#define PLACEMENT_ENGINE_H

#include <string>

// Runs random legal placement on an input netlist and writes placement output.
// Returns true on success and stores the final total HPWL in total_hpwl.
bool run_placement_engine(const std::string& input_path,
                          const std::string& output_path,
                          long long& total_hpwl,
                          unsigned seed = 12345);

#endif
