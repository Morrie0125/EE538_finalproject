#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <readline/history.h>
#include <readline/readline.h>

#include "../include/commands.h"
#include "../include/cli_repl.h"

using namespace std;

namespace {

const vector<string> kBuiltinCommands = {
    "generate",
    "place",
    "sa_place",
    "roundtrip_test",
    "visualize",
    "help",
    "exit",
    "quit",
};

string history_file_path() {
    if (const char* home = getenv("HOME")) {
        return string(home) + "/.ee538_history";
    }
    if (const char* userprofile = getenv("USERPROFILE")) {
        return string(userprofile) + "\\.ee538_history";
    }
    return ".ee538_history";
}

bool ends_with_space(const string& text) {
    return !text.empty() && isspace(static_cast<unsigned char>(text.back())) != 0;
}

vector<string> split_tokens(const string& line) {
    vector<string> tokens;
    istringstream iss(line);
    string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

string trim_left(const string& text) {
    size_t i = 0;
    while (i < text.size() && isspace(static_cast<unsigned char>(text[i])) != 0) {
        ++i;
    }
    return text.substr(i);
}

char* duplicate_text(const string& text) {
    char* result = static_cast<char*>(malloc(text.size() + 1));
    if (result == nullptr) {
        return nullptr;
    }
    memcpy(result, text.c_str(), text.size() + 1);
    return result;
}

int dispatch_command(const vector<string>& tokens) {
    if (tokens.empty()) {
        return 0;
    }

    if (tokens[0] == "help") {
        cout << "Commands:\n";
        cout << "  generate <output.txt> <gridW> <gridH> <numComponents> <numNets> <seed>\n";
        cout << "  place <input_file>\n";
        cout << "  sa_place <input> <output> <seed> <max_iters> <t0> <alpha> [--cost full|delta] [--moves_per_temp N] [--illegal_retry K] [--relocate_ratio R]\n";
        cout << "  roundtrip_test [input_file] [output_file]\n";
        cout << "  visualize [placement_file]\n";
        cout << "  help\n";
        cout << "  exit\n";
        return 0;
    }

    vector<char*> argv;
    argv.reserve(tokens.size());
    for (const auto& token : tokens) {
        argv.push_back(const_cast<char*>(token.c_str()));
    }

    const int argc = static_cast<int>(argv.size());
    if (tokens[0] == "generate") {
        return run_generator_cli(argc, argv.data());
    }
    if (tokens[0] == "place") {
        return run_placement_cli(argc, argv.data());
    }
    if (tokens[0] == "sa_place") {
        return run_sa_place_cli(argc, argv.data());
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

char* command_generator(const char* text, int state) {
    static size_t index = 0;
    static string prefix;

    if (state == 0) {
        index = 0;
        prefix = text ? text : "";
    }

    while (index < kBuiltinCommands.size()) {
        const string& candidate = kBuiltinCommands[index++];
        if (candidate.rfind(prefix, 0) == 0) {
            return duplicate_text(candidate);
        }
    }

    return nullptr;
}

char** completion_dispatch(const char* text, int start, int end) {
    (void)end;

    const string buffer = rl_line_buffer ? rl_line_buffer : "";
    const string prefix = buffer.substr(0, static_cast<size_t>(start));
    const vector<string> tokens = split_tokens(prefix);

    if (tokens.empty() || (tokens.size() == 1 && !ends_with_space(prefix))) {
        return rl_completion_matches(text, command_generator);
    }

    return rl_completion_matches(text, rl_filename_completion_function);
}

}  // namespace

int run_interactive_cli() {
    using_history();
    rl_attempted_completion_function = completion_dispatch;

    const string history_path = history_file_path();
    read_history(history_path.c_str());

    cout << "EE538 interactive CLI\n";
    cout << "Type 'help' for commands, 'exit' to quit.\n";

    while (true) {
        char* raw_line = readline("> ");
        if (raw_line == nullptr) {
            cout << "\n";
            break;
        }

        string line = trim_left(raw_line);
        free(raw_line);

        if (line.empty()) {
            continue;
        }

        add_history(line.c_str());

        const vector<string> tokens = split_tokens(line);
        if (tokens.empty()) {
            continue;
        }
        if (tokens[0] == "exit" || tokens[0] == "quit") {
            break;
        }

        const int rc = dispatch_command(tokens);
        if (rc != 0) {
            cerr << "Command exited with code " << rc << "\n";
        }
    }

    write_history(history_path.c_str());
    return 0;
}
