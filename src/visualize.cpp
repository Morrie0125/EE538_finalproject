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
    if (argc > 2) {
        cerr << "Usage: visualize [placement_file]\n";
        return 1;
    }

    const string placement_file = (argc == 2) ? argv[1] : "placement_out.txt";

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

    vector<string> launch_cmds;
#ifdef _WIN32
    launch_cmds.push_back("py -3 " + quoted_script + " " + quoted_input);
    launch_cmds.push_back("python " + quoted_script + " " + quoted_input);
    launch_cmds.push_back("python3 " + quoted_script + " " + quoted_input);
#else
    launch_cmds.push_back("python3 " + quoted_script + " " + quoted_input);
    launch_cmds.push_back("python " + quoted_script + " " + quoted_input);
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
