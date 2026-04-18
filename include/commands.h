#ifndef COMMANDS_H
#define COMMANDS_H

// Runs the benchmark generator CLI command.
int run_generator_cli(int argc, char* argv[]);

// Runs the legal-placement CLI command.
int run_placement_cli(int argc, char* argv[]);

// Runs the parser-writer roundtrip validation CLI command.
int run_roundtrip_test_cli(int argc, char* argv[]);

// Runs the visualization CLI command by invoking the Python visualizer script.
int run_visualize_cli(int argc, char* argv[]);

// Runs simulated annealing placement optimization.
int run_sa_place_cli(int argc, char* argv[]);

#endif