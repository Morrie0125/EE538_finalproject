#ifndef DEMO_CONFIG_H
#define DEMO_CONFIG_H

#include <string>
#include <utility>
#include <vector>

struct DemoGeneratorConfig {
    std::string output_path;
    int grid_w = 0;
    int grid_h = 0;
    int num_components = 0;
    int num_nets = 0;
    unsigned seed = 0;
    int fixed_chance_pct = 0;
    int pin_min = 0;
    int pin_max = 0;
    int net_degree_min = 0;
    int net_degree_max = 0;
    std::vector<std::pair<int, int>> size_choices;
};

struct DemoSaConfig {
    std::string input_path;
    std::string output_path;
    unsigned seed = 0;
    int max_iters = 0;
    double t0 = 0.0;
    double alpha = 0.0;
    double temp_floor = 0.0;
    std::string cost_mode;
    int moves_per_temp = 0;
    int illegal_retry = 0;
};

struct DemoPreset {
    std::string mode_name;
    DemoGeneratorConfig generator;
    DemoSaConfig sa;
};

bool load_demo_preset(const std::string& mode_name, DemoPreset& preset, std::string& error);

#endif
