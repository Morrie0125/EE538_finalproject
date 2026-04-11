#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "commands.h"

using namespace std;

static vector<string> split_tokens(const string& line) {
    vector<string> tokens;
    istringstream iss(line);
    string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

static void print_help() {
    cout << "Commands:\n";
    cout << "  generate <output.txt> <gridW> <gridH> <numComponents> <numNets> <seed>\n";
    cout << "  place <input_file>\n";
    cout << "  roundtrip_test [input_file] [output_file]\n";
    cout << "  visualize [placement_file]\n";
    cout << "  help\n";
    cout << "  exit\n";
}

static int dispatch_command(const vector<string>& tokens) {
    if (tokens.empty()) return 0;

    if (tokens[0] == "help") {
        print_help();
        return 0;
    }

    vector<char*> argv;
    argv.reserve(tokens.size());
    for (const auto& t : tokens) {
        argv.push_back(const_cast<char*>(t.c_str()));
    }

    int argc = static_cast<int>(argv.size());

    if (tokens[0] == "generate") {
        return run_generator_cli(argc, argv.data());
    }
    if (tokens[0] == "place") {
        return run_placement_cli(argc, argv.data());
    }
    if (tokens[0] == "roundtrip_test") {
        return run_roundtrip_test_cli(argc, argv.data());
    }
    if (tokens[0] == "visualize") {
        return run_visualize_cli(argc, argv.data());
    }

    cerr << "Unknown command: " << tokens[0] << "\n";
    cerr << "Type 'help' to see available commands.\n";
    return 1;
}

int main() {
    cout << "EE538 interactive CLI\n";
    cout << "Type 'help' for commands, 'exit' to quit.\n";

    string line;
    while (true) {
        cout << "> ";
        if (!getline(cin, line)) {
            cout << "\n";
            break;
        }

        auto tokens = split_tokens(line);
        if (tokens.empty()) continue;
        if (tokens[0] == "exit" || tokens[0] == "quit") break;

        int rc = dispatch_command(tokens);
        if (rc != 0) {
            cerr << "Command exited with code " << rc << "\n";
        }
    }

    return 0;
}
