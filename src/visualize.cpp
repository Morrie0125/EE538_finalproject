#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "../include/commands.h"

using namespace std;

static string quote_arg(const string& arg) {
    string out = "\"";
    for (char c : arg) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

int run_visualize_cli(int argc, char* argv[]) {
    if (argc > 3) {
        cerr << "Usage: visualize [placement_file] [--demo]\n";
        return 1;
    }

    bool demo_mode = false;
    string placement_file = "placement_out.txt";

    for (int i = 1; i < argc; ++i) {
        const string arg = argv[i];
        if (arg == "--demo") {
            demo_mode = true;
        } else {
            placement_file = arg;
        }
    }

    string script = "scripts/visualize.py";
    if (!filesystem::exists(script) && filesystem::exists("visualize.py")) {
        script = "visualize.py";
    }

    if (!filesystem::exists(script)) {
        cerr << "Visualization script not found. Expected scripts/visualize.py\n";
        return 1;
    }

    const string quoted_script = quote_arg(script);
    const string quoted_input = quote_arg(placement_file);

    string arg_string;
    if (demo_mode) {
        arg_string = " --demo";
    } else {
        arg_string = " " + quoted_input;
    }

    vector<string> launch_cmds;
#ifdef _WIN32
    launch_cmds.push_back("py -3 " + quoted_script + arg_string);
    launch_cmds.push_back("python " + quoted_script + arg_string);
    launch_cmds.push_back("python3 " + quoted_script + arg_string);
#else
    launch_cmds.push_back("python3 " + quoted_script + arg_string);
    launch_cmds.push_back("python " + quoted_script + arg_string);
#endif

    int last_rc = 1;
    for (const auto& cmd : launch_cmds) {
        last_rc = system(cmd.c_str());
        if (last_rc == 0) {
            return 0;
        }
    }

    cerr << "Failed to launch visualization. Ensure Python and matplotlib are installed.\n";
    return last_rc;
}
