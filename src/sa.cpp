#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "../include/adjacency.h"
#include "../include/commands.h"
#include "../include/demo_config.h"
#include "../include/delta_hpwl.h"
#include "../include/hpwl_engine.h"
#include "../include/io_engine.h"
#include "../include/placement_engine.h"
#include "../include/sa_logger.h"

using namespace std;

namespace {

enum class CostMode {
    FULL,
    DELTA
};

enum class DemoMode {
    EASY,
    MID,
    HARD,
};

struct SaConfig {
    string input_path;
    string output_path;
    unsigned seed = 12345;
    int max_iters = 1000;
    double t0 = 100.0;
    double alpha = 0.95;
    double temp_floor = 1e-9;
    CostMode cost_mode = CostMode::FULL;
    int moves_per_temp = 100;
    int illegal_retry = 3;
    double relocate_ratio = 0.5;  // API placeholder; currently not used for move sampling.
    bool demo_mode = false;
    string demo_mode_name = "easy";
};

bool parse_demo_mode(const string& text, DemoMode& mode) {
    if (text == "easy") {
        mode = DemoMode::EASY;
        return true;
    }
    if (text == "mid") {
        mode = DemoMode::MID;
        return true;
    }
    if (text == "hard") {
        mode = DemoMode::HARD;
        return true;
    }
    return false;
}

string demo_mode_name(DemoMode mode) {
    if (mode == DemoMode::EASY) return "easy";
    if (mode == DemoMode::MID) return "mid";
    return "hard";
}

string demo_snapshot_path(int stage_idx) {
    ostringstream oss;
    oss << "demo/snaps/stage_"
        << setw(4) << setfill('0') << stage_idx
        << "_best.txt";
    return oss.str();
}

string cost_mode_name(CostMode mode) {
    return mode == CostMode::FULL ? "full" : "delta";
}

string pad_right(string text, size_t width) {
    if (text.size() < width) {
        text.append(width - text.size(), ' ');
    }
    return text;
}

string make_progress_line(const string& label, int current, int total, int bar_width = 24) {
    const int safe_total = max(1, total);
    const int clamped_current = max(0, min(current, safe_total));
    const int filled = static_cast<int>((1LL * clamped_current * bar_width) / safe_total);

    string line = label + " [";
    line.append(static_cast<size_t>(filled), '#');
    line.append(static_cast<size_t>(bar_width - filled), '.');
    line += "] ";
    line += to_string(clamped_current);
    line += "/";
    line += to_string(safe_total);
    return line;
}

bool supports_ansi_cursor_control() {
#ifdef _WIN32
    if (_isatty(_fileno(stdout)) == 0) {
        return false;
    }

    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
        return false;
    }

    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode) == 0) {
        return false;
    }

    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
        if (SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
            return false;
        }
    }
    return true;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

bool parse_int(const string& text, int& value) {
    try {
        size_t p = 0;
        value = stoi(text, &p);
        return p == text.size();
    } catch (...) {
        return false;
    }
}

bool parse_uint(const string& text, unsigned& value) {
    try {
        size_t p = 0;
        value = static_cast<unsigned>(stoul(text, &p));
        return p == text.size();
    } catch (...) {
        return false;
    }
}

bool parse_double(const string& text, double& value) {
    try {
        size_t p = 0;
        value = stod(text, &p);
        return p == text.size();
    } catch (...) {
        return false;
    }
}

int print_sa_usage(const char* argv0) {
    cerr << "Usage: " << argv0
         << " <input> <output> <seed> <max_iters> <t0> <alpha>"
         << " [--cost full|delta] [--moves_per_temp N] [--illegal_retry K]"
         << " [--relocate_ratio R]\n";
    cerr << "       " << argv0 << " --demo [easy|mid|hard]\n";
    return 1;
}

bool parse_sa_config(int argc, char* argv[], SaConfig& cfg) {
    if (argc >= 2 && string(argv[1]) == "--demo") {
        if (argc > 3) {
            cerr << "Demo mode accepts only: sa_place --demo [easy|mid|hard]\n";
            return false;
        }
        DemoMode mode = DemoMode::EASY;
        if (argc == 3 && !parse_demo_mode(argv[2], mode)) {
            cerr << "Unknown demo mode: " << argv[2] << "\n";
            cerr << "Available modes: easy, mid, hard\n";
            return false;
        }
        DemoPreset preset;
        string error;
        if (!load_demo_preset(demo_mode_name(mode), preset, error)) {
            cerr << "Failed to load demo config: " << error << "\n";
            return false;
        }
        cfg.demo_mode = true;
        cfg.demo_mode_name = preset.mode_name;
        cfg.input_path = preset.sa.input_path;
        cfg.output_path = preset.sa.output_path;
        cfg.seed = preset.sa.seed;
        cfg.max_iters = preset.sa.max_iters;
        cfg.t0 = preset.sa.t0;
        cfg.alpha = preset.sa.alpha;
        cfg.temp_floor = preset.sa.temp_floor;
        cfg.cost_mode = preset.sa.cost_mode == "delta" ? CostMode::DELTA : CostMode::FULL;
        cfg.moves_per_temp = preset.sa.moves_per_temp;
        cfg.illegal_retry = preset.sa.illegal_retry;
        return true;
    }

    if (argc < 7) {
        return false;
    }

    cfg.input_path = argv[1];
    cfg.output_path = argv[2];

    if (!parse_uint(argv[3], cfg.seed)) {
        cerr << "seed must be an unsigned integer\n";
        return false;
    }
    if (!parse_int(argv[4], cfg.max_iters) || cfg.max_iters <= 0) {
        cerr << "max_iters must be a positive integer\n";
        return false;
    }
    if (!parse_double(argv[5], cfg.t0) || cfg.t0 <= 0.0) {
        cerr << "t0 must be > 0\n";
        return false;
    }
    if (!parse_double(argv[6], cfg.alpha) || cfg.alpha <= 0.0 || cfg.alpha >= 1.0) {
        cerr << "alpha must satisfy 0 < alpha < 1\n";
        return false;
    }

    int i = 7;
    while (i < argc) {
        const string key = argv[i];
        if (key == "--cost") {
            if (i + 1 >= argc) {
                cerr << "--cost requires a value\n";
                return false;
            }
            const string value = argv[i + 1];
            if (value == "full") {
                cfg.cost_mode = CostMode::FULL;
            } else if (value == "delta") {
                cfg.cost_mode = CostMode::DELTA;
            } else {
                cerr << "--cost must be full or delta\n";
                return false;
            }
            i += 2;
            continue;
        }

        if (key == "--moves_per_temp") {
            if (i + 1 >= argc) {
                cerr << "--moves_per_temp requires a value\n";
                return false;
            }
            if (!parse_int(argv[i + 1], cfg.moves_per_temp) || cfg.moves_per_temp <= 0) {
                cerr << "--moves_per_temp must be a positive integer\n";
                return false;
            }
            i += 2;
            continue;
        }

        if (key == "--illegal_retry") {
            if (i + 1 >= argc) {
                cerr << "--illegal_retry requires a value\n";
                return false;
            }
            if (!parse_int(argv[i + 1], cfg.illegal_retry) || cfg.illegal_retry <= 0) {
                cerr << "--illegal_retry must be a positive integer\n";
                return false;
            }
            i += 2;
            continue;
        }

        if (key == "--relocate_ratio") {
            if (i + 1 >= argc) {
                cerr << "--relocate_ratio requires a value\n";
                return false;
            }
            if (!parse_double(argv[i + 1], cfg.relocate_ratio) ||
                cfg.relocate_ratio < 0.0 || cfg.relocate_ratio > 1.0) {
                cerr << "--relocate_ratio must satisfy 0 <= R <= 1\n";
                return false;
            }
            i += 2;
            continue;
        }

        if (key == "--temp_floor") {
            if (i + 1 >= argc) {
                cerr << "--temp_floor requires a value\n";
                return false;
            }
            if (!parse_double(argv[i + 1], cfg.temp_floor) || cfg.temp_floor <= 0.0) {
                cerr << "--temp_floor must be > 0\n";
                return false;
            }
            i += 2;
            continue;
        }

        cerr << "Unknown option: " << key << "\n";
        return false;
    }

    return true;
}

}  // namespace

int run_sa_place_cli(int argc, char* argv[]) {
    SaConfig cfg;
    if (!parse_sa_config(argc, argv, cfg)) {
        return print_sa_usage(argv[0]);
    }

    vector<string> cli_args;
    cli_args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        cli_args.push_back(argv[i]);
    }

    if (cfg.demo_mode) {
        vector<string> gen_args = {"generate", "--demo", cfg.demo_mode_name};
        vector<char*> gen_argv;
        gen_argv.reserve(gen_args.size());
        for (auto& s : gen_args) {
            gen_argv.push_back(const_cast<char*>(s.c_str()));
        }
        const int gen_rc = run_generator_cli(static_cast<int>(gen_argv.size()), gen_argv.data());
        if (gen_rc != 0) {
            cerr << "Failed to generate demo input for sa_place --demo\n";
            return gen_rc;
        }
    }

    PlacementDB db;
    try {
        db.parseFile(cfg.input_path);
        db.randomLegalPlacement(cfg.seed);
    } catch (const exception& e) {
        cerr << "Failed to initialize placement DB: " << e.what() << "\n";
        return 1;
    }

    const vector<int> movables = collect_movable_nodes(db);
    if (movables.empty()) {
        cerr << "No movable components found; nothing to optimize\n";
        return 1;
    }

    mt19937 rng(cfg.seed);

    long long current_hpwl = db.totalHPWL();
    const long long initial_hpwl = current_hpwl;
    long long best_hpwl = current_hpwl;
    PlacementState best_state = static_cast<const PlacementState&>(db);

    adjacency::Adjacency adj;
    if (cfg.cost_mode == CostMode::DELTA) {
        try {
            const PlacementState state_snapshot = static_cast<const PlacementState&>(db);
            adj = adjacency::build_adjacency(state_snapshot);
            adjacency::validate_adjacency(state_snapshot, adj);
        } catch (const exception& e) {
            cerr << "Failed to build adjacency for delta mode: " << e.what() << "\n";
            return 1;
        }
    }

    SaRunLogger logger("sa_place", cost_mode_name(cfg.cost_mode), cli_args, cfg.temp_floor);
    if (!logger.ok()) {
        cerr << "Failed to open SA log files\n";
        return 1;
    }

    if (cfg.demo_mode) {
        std::error_code ec;
        filesystem::remove_all("demo/snaps", ec);
        filesystem::create_directories("demo/snaps", ec);
        if (ec) {
            cerr << "Failed to prepare demo/snaps: " << ec.message() << "\n";
            return 1;
        }
    }

    uniform_real_distribution<double> prob01(0.0, 1.0);
    int total_steps = 0;
    int total_proposals = 0;
    int total_accepted = 0;

    const auto run_start = chrono::steady_clock::now();

    double temp = cfg.t0;
    int stage_idx = 0;
    const int stage_total_budget = (cfg.max_iters + cfg.moves_per_temp - 1) / cfg.moves_per_temp;
    bool progress_started = false;
    const char* two_line_env = getenv("SA_TWO_LINE_PROGRESS");
    const bool request_two_line = (two_line_env != nullptr && string(two_line_env) == "1");
    const bool ansi_progress = request_two_line && supports_ansi_cursor_control();

    auto print_progress = [&](int stage_now,
                              int step_now,
                              int step_total,
                              long long best_now,
                              double runtime_now,
                              bool finish_output = false) {
        ostringstream info;
        info << "best=" << best_now << " runtime=" << fixed << setprecision(3) << runtime_now << "s";

        const string stage_line = pad_right(make_progress_line("stage", stage_now, stage_total_budget) + " " + info.str(), 120);
        const string step_line = pad_right(make_progress_line("step ", step_now, step_total), 120);

        if (!ansi_progress) {
            const string compact = pad_right(stage_line, 130);
            cout << "\r" << compact;
            if (finish_output) {
                cout << "\n";
            }
            cout << flush;
            return;
        }

        if (!progress_started) {
            cout << stage_line << "\n" << step_line;
            progress_started = true;
        } else {
            cout << "\r" << stage_line << "\n\r" << step_line;
        }

        if (finish_output) {
            cout << "\n";
        } else {
            cout << "\x1b[2A";
        }
        cout << flush;
    };

    while (total_steps < cfg.max_iters) {
        if (temp < cfg.temp_floor) {
            break;
        }

        SaStageLogEntry stage;
        stage.stage_idx = stage_idx;
        stage.temperature = temp;
        stage.cost_mode = cost_mode_name(cfg.cost_mode);

        const int step_total = min(cfg.moves_per_temp, cfg.max_iters - total_steps);
        int stage_steps = 0;
        print_progress(stage_idx + 1, 0, step_total, best_hpwl, 0.0);
        while (stage_steps < cfg.moves_per_temp && total_steps < cfg.max_iters) {
            bool legal_proposal_seen = false;
            for (int retry = 0; retry < cfg.illegal_retry; ++retry) {
                ++stage.attempted_moves;
                ++total_proposals;

                const bool try_relocate = prob01(rng) < 0.5;
                vector<int> moved_nodes;

                const size_t cp = db.checkpoint();
                PlacementState before = static_cast<const PlacementState&>(db);
                bool legal = false;
                if (try_relocate) {
                    legal = apply_random_relocate_move(db, movables, rng, moved_nodes);
                } else {
                    legal = apply_random_swap_move(db, movables, rng, moved_nodes);
                }

                if (!legal) {
                    continue;
                }

                legal_proposal_seen = true;
                PlacementState after = static_cast<const PlacementState&>(db);

                long long candidate_hpwl = current_hpwl;
                long long delta = 0;
                if (cfg.cost_mode == CostMode::FULL) {
                    candidate_hpwl = hpwl::total_hpwl(after);
                    delta = candidate_hpwl - current_hpwl;
                } else {
                    try {
                        const auto d = delta_hpwl::compute_delta_hpwl(before, after, adj, moved_nodes);
                        delta = d.delta;
                        candidate_hpwl = current_hpwl + delta;
                    } catch (const exception& e) {
                        cerr << "delta mode failed: " << e.what() << "\n";
                        return 1;
                    }
                }

                bool accept = false;
                if (delta <= 0) {
                    accept = true;
                } else {
                    const double p = exp(-static_cast<double>(delta) / temp);
                    accept = prob01(rng) < p;
                }

                if (accept) {
                    current_hpwl = candidate_hpwl;
                    ++stage.accepted_moves;
                    ++total_accepted;
                    if (delta > 0) {
                        ++stage.accepted_uphill_moves;
                    }
                    if (current_hpwl < best_hpwl) {
                        best_hpwl = current_hpwl;
                        best_state = after;
                    }
                } else {
                    if (!db.rollbackTo(cp)) {
                        cerr << "Failed to rollback rejected move\n";
                        return 1;
                    }
                }
                break;
            }

            if (!legal_proposal_seen) {
                // This SA step is rejected due to illegal retries exhausted.
            }

            ++total_steps;
            ++stage_steps;

            const auto progress_now = chrono::steady_clock::now();
            const double elapsed = chrono::duration<double>(progress_now - run_start).count();
            print_progress(stage_idx + 1, stage_steps, step_total, best_hpwl, elapsed);
        }

        const auto stage_end = chrono::steady_clock::now();
        stage.runtime_sec = chrono::duration<double>(stage_end - run_start).count();
        stage.current_hpwl = current_hpwl;
        stage.best_hpwl_so_far = best_hpwl;
        logger.log_stage(stage);

        if (cfg.demo_mode) {
            const string snap_path = demo_snapshot_path(stage_idx);
            ostringstream meta;
            meta << "sa_demo_stage=" << stage_idx;
            if (!write_placement(snap_path, best_state, best_hpwl, meta.str())) {
                cerr << "Failed to write demo snapshot: " << snap_path << "\n";
                return 1;
            }
        }

        temp *= cfg.alpha;
        ++stage_idx;
    }

    if (!write_placement(cfg.output_path, best_state, best_hpwl, "sa_place_best")) {
        cerr << "Failed to write output placement: " << cfg.output_path << "\n";
        return 1;
    }

    const double total_runtime_sec = chrono::duration<double>(chrono::steady_clock::now() - run_start).count();

    logger.log_summary(initial_hpwl,
                       current_hpwl,
                       best_hpwl,
                       total_runtime_sec,
                       total_accepted,
                       total_steps,
                       total_proposals,
                       cfg.output_path);

    if (progress_started) {
        print_progress(stage_idx, 1, 1, best_hpwl, total_runtime_sec, true);
    }
    cout << "SA done: best=" << best_hpwl
         << " runtime=" << fixed << setprecision(3) << total_runtime_sec << "s\n";

    return 0;
}
