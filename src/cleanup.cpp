#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "../include/commands.h"

using namespace std;

namespace {

bool remove_if_exists(const filesystem::path& path, int& removed_count) {
    std::error_code ec;
    if (!filesystem::exists(path, ec)) {
        return true;
    }

    const auto n = filesystem::remove_all(path, ec);
    if (ec) {
        cerr << "cleanup: failed to remove " << path.string() << ": " << ec.message() << "\n";
        return false;
    }

    if (n > 0) {
        ++removed_count;
        cout << "cleanup: removed " << path.string() << "\n";
    }
    return true;
}

}  // namespace

int run_cleanup_cli(int argc, char* argv[]) {
    if (argc != 1) {
        cerr << "Usage: cleanup\n";
        return 1;
    }

    // Known outputs produced by commands inside main.exe sessions.
    const vector<filesystem::path> targets = {
        "placement_out.txt",
        "sa_out.txt",
        "sa_test.txt",
        "examples/roundtrip_out.txt",
        "logs",
        "demo/snaps",
    };

    int removed = 0;
    bool ok = true;
    for (const auto& target : targets) {
        if (!remove_if_exists(target, removed)) {
            ok = false;
        }
    }

    cout << "cleanup: done, removed " << removed << " target(s)\n";
    return ok ? 0 : 1;
}
