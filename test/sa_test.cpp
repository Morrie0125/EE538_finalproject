#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "../include/commands.h"
#include "../include/demo_config.h"
#include "../include/placement_engine.h"

using namespace std;

namespace {

struct RangeSpec {
    double min_value = 0.0;
    double max_value = 0.0;
};

struct SweepRanges {
    RangeSpec t0;
    RangeSpec alpha;
    int steps_per_t = 200;
    int point_count = 10;
};

struct SweepPoint {
    double t0 = 0.0;
    double alpha = 0.0;
};

struct RunMetrics {
    string level;
    string benchmark_name;
    string benchmark_path;
    unsigned seed = 0;
    double t0 = 0.0;
    double alpha = 0.0;
    int steps_per_t = 0;
    bool use_heuristic = false;
    long long baseline_cost = 0;
    long long final_cost = 0;
    long long best_cost = 0;
    double improvement_pct = 0.0;
    long long total_steps = 0;
    long long total_stages = 0;
    long long runtime_ms = 0;
    bool legality_ok = false;
};

struct SummaryKey {
    string level;
    double t0 = 0.0;
    double alpha = 0.0;
    int steps_per_t = 0;
    bool use_heuristic = false;

    bool operator<(const SummaryKey& rhs) const {
        return tie(level, t0, alpha, steps_per_t, use_heuristic) <
               tie(rhs.level, rhs.t0, rhs.alpha, rhs.steps_per_t, rhs.use_heuristic);
    }
};

struct SummaryStats {
    int runs = 0;
    double sum_improvement = 0.0;
    double best_improvement = -numeric_limits<double>::infinity();
    double sum_runtime_ms = 0.0;
    int legality_pass = 0;
    long long sum_total_steps = 0;
    long long sum_total_stages = 0;
};

vector<string> split_csv(const string& text) {
    vector<string> out;
    string token;
    stringstream ss(text);
    while (getline(ss, token, ',')) {
        if (!token.empty()) {
            out.push_back(token);
        }
    }
    return out;
}

RangeSpec parse_double_range(const string& text, const string& name) {
    const vector<string> parts = split_csv(text);
    if (parts.size() != 2) {
        throw runtime_error(name + " range must be min,max");
    }

    size_t min_pos = 0;
    size_t max_pos = 0;
    const double min_value = stod(parts[0], &min_pos);
    const double max_value = stod(parts[1], &max_pos);
    if (min_pos != parts[0].size() || max_pos != parts[1].size()) {
        throw runtime_error(name + " range contains an invalid number");
    }
    if (min_value > max_value) {
        throw runtime_error(name + " range min must be <= max");
    }

    return {min_value, max_value};
}

vector<int> shuffled_indices(int count, mt19937& rng) {
    vector<int> indices(count);
    for (int i = 0; i < count; ++i) {
        indices[i] = i;
    }
    shuffle(indices.begin(), indices.end(), rng);
    return indices;
}

vector<SweepPoint> build_sweep_points(const SweepRanges& ranges) {
    if (ranges.point_count <= 0) {
        throw runtime_error("point count must be positive");
    }

    mt19937 rng(20260418u);
    const vector<int> t0_bins = shuffled_indices(ranges.point_count, rng);
    const vector<int> alpha_bins = shuffled_indices(ranges.point_count, rng);

    vector<SweepPoint> points;
    points.reserve(static_cast<size_t>(ranges.point_count));

    const double denom = static_cast<double>(ranges.point_count);
    for (int i = 0; i < ranges.point_count; ++i) {
        const double t0_frac = (static_cast<double>(t0_bins[i]) + 0.5) / denom;
        const double alpha_frac = (static_cast<double>(alpha_bins[i]) + 0.5) / denom;

        const double t0 = ranges.t0.min_value + (ranges.t0.max_value - ranges.t0.min_value) * t0_frac;
        const double alpha = ranges.alpha.min_value + (ranges.alpha.max_value - ranges.alpha.min_value) * alpha_frac;

        points.push_back({t0, alpha});
    }

    return points;
}

string make_progress_line(const string& label, size_t done, size_t total, size_t width = 24) {
    const size_t safe_total = max<size_t>(1, total);
    const double ratio = static_cast<double>(done) / static_cast<double>(safe_total);
    const size_t filled = static_cast<size_t>(llround(ratio * static_cast<double>(width)));
    string bar = "[";
    bar.append(min(width, filled), '#');
    bar.append(width - min(width, filled), '.');
    bar.push_back(']');

    ostringstream oss;
    oss << label << ' ' << bar << ' ' << done << '/' << total
        << " (" << fixed << setprecision(1) << (ratio * 100.0) << "%)";
    return oss.str();
}

void print_progress(const string& line, size_t& last_width) {
    cout << '\r' << line;
    if (line.size() < last_width) {
        cout << string(last_width - line.size(), ' ');
    }
    cout << flush;
    last_width = line.size();
}

string csv_escape(const string& text) {
    if (text.find_first_of(",\"") == string::npos) {
        return text;
    }
    string out = "\"";
    for (char c : text) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out += '"';
    return out;
}

bool parse_bool_flag(const string& key, const string& value, bool& out) {
    if (key != value) {
        return false;
    }
    out = true;
    return true;
}

bool ensure_benchmark_generated(const string& level, string& benchmark_path, string& error) {
    DemoPreset preset;
    if (!load_demo_preset(level, preset, error)) {
        return false;
    }

    benchmark_path = preset.sa.input_path;
    error.clear();

    std::error_code ec;
    filesystem::create_directories("demo", ec);

    vector<string> gen_args = {"generate", "--demo", level};
    vector<char*> gen_argv;
    gen_argv.reserve(gen_args.size());
    for (auto& s : gen_args) {
        gen_argv.push_back(const_cast<char*>(s.c_str()));
    }

    const int rc = run_generator_cli(static_cast<int>(gen_argv.size()), gen_argv.data());
    if (rc != 0) {
        error = "generator failed for level: " + level;
        return false;
    }

    return true;
}

bool sample_distinct_pair(const vector<int>& movables, mt19937& rng, int& a, int& b) {
    if (movables.size() < 2) {
        return false;
    }
    uniform_int_distribution<int> pick(0, static_cast<int>(movables.size()) - 1);
    a = movables[pick(rng)];
    do {
        b = movables[pick(rng)];
    } while (b == a);
    return true;
}

long long net_hpwl_from_state(const PlacementState& state, const Net& net) {
    int minX = numeric_limits<int>::max();
    int maxX = numeric_limits<int>::min();
    int minY = numeric_limits<int>::max();
    int maxY = numeric_limits<int>::min();

    for (const auto& ref : net.pins) {
        const Node& n = state.nodes[ref.nodeIdx];
        const Pin& p = n.pins[ref.pinIdx];
        const int x = n.x + p.dx;
        const int y = n.y + p.dy;
        minX = min(minX, x);
        maxX = max(maxX, x);
        minY = min(minY, y);
        maxY = max(maxY, y);
    }

    return static_cast<long long>(maxX - minX) + static_cast<long long>(maxY - minY);
}

struct Occupancy {
    int grid_w = 0;
    int grid_h = 0;
    vector<vector<int>> cells;

    explicit Occupancy(const PlacementState& state) {
        grid_w = state.gridW;
        grid_h = state.gridH;
        cells.assign(grid_h, vector<int>(grid_w, -1));
    }

    bool inside(int x, int y, int w, int h) const {
        return x >= 0 && y >= 0 && x + w <= grid_w && y + h <= grid_h;
    }

    void stamp(const PlacementState& state, int node_idx, int x, int y) {
        const Node& node = state.nodes[node_idx];
        for (int yy = y; yy < y + node.h; ++yy) {
            for (int xx = x; xx < x + node.w; ++xx) {
                cells[yy][xx] = node_idx;
            }
        }
    }

    void unstamp(const PlacementState& state, int node_idx, int x, int y) {
        const Node& node = state.nodes[node_idx];
        for (int yy = y; yy < y + node.h; ++yy) {
            for (int xx = x; xx < x + node.w; ++xx) {
                if (cells[yy][xx] == node_idx) {
                    cells[yy][xx] = -1;
                }
            }
        }
    }

    bool can_place(const PlacementState& state, int node_idx, int x, int y) const {
        const Node& node = state.nodes[node_idx];
        if (!inside(x, y, node.w, node.h)) {
            return false;
        }
        for (int yy = y; yy < y + node.h; ++yy) {
            for (int xx = x; xx < x + node.w; ++xx) {
                if (cells[yy][xx] != -1) {
                    return false;
                }
            }
        }
        return true;
    }
};

struct AppliedMove {
    enum class Type {
        Relocate,
        Swap,
    };

    Type type = Type::Relocate;
    int a = -1;
    int b = -1;
    int old_ax = -1;
    int old_ay = -1;
    int old_bx = -1;
    int old_by = -1;
};

bool build_occupancy_from_state(const PlacementState& state, Occupancy& occ, string& err) {
    for (int i = 0; i < static_cast<int>(state.nodes.size()); ++i) {
        const Node& node = state.nodes[i];
        if (node.x < 0 || node.y < 0) {
            if (!node.fixed) {
                continue;
            }
            err = "fixed node has unset location: " + node.id;
            return false;
        }
        if (!occ.inside(node.x, node.y, node.w, node.h)) {
            err = "node outside grid: " + node.id;
            return false;
        }
        if (!occ.can_place(state, i, node.x, node.y)) {
            err = "overlap while building occupancy at node: " + node.id;
            return false;
        }
        occ.stamp(state, i, node.x, node.y);
    }
    return true;
}

long long total_hpwl(const PlacementState& state) {
    long long total = 0;
    for (const auto& net : state.nets) {
        total += net_hpwl_from_state(state, net);
    }
    return total;
}

int pick_node_from_high_cost_net(const PlacementState& state,
                                 const vector<int>& movables,
                                 mt19937& rng) {
    if (movables.empty()) {
        return -1;
    }

    vector<char> is_movable(state.nodes.size(), 0);
    for (int idx : movables) {
        if (idx >= 0 && idx < static_cast<int>(state.nodes.size())) {
            is_movable[idx] = 1;
        }
    }

    vector<double> net_weights(state.nets.size(), 0.0);
    double total_weight = 0.0;
    for (int ni = 0; ni < static_cast<int>(state.nets.size()); ++ni) {
        const Net& net = state.nets[ni];
        bool has_movable = false;
        for (const auto& ref : net.pins) {
            if (is_movable[ref.nodeIdx]) {
                has_movable = true;
                break;
            }
        }
        if (!has_movable) {
            continue;
        }

        const double w = static_cast<double>(max(1LL, net_hpwl_from_state(state, net)));
        net_weights[ni] = w;
        total_weight += w;
    }

    uniform_int_distribution<int> random_movable(0, static_cast<int>(movables.size()) - 1);
    if (total_weight <= 0.0) {
        return movables[random_movable(rng)];
    }

    uniform_real_distribution<double> pick_weight(0.0, total_weight);
    double target = pick_weight(rng);
    int selected_net = -1;
    for (int ni = 0; ni < static_cast<int>(net_weights.size()); ++ni) {
        if (net_weights[ni] <= 0.0) {
            continue;
        }
        target -= net_weights[ni];
        if (target <= 0.0) {
            selected_net = ni;
            break;
        }
    }

    if (selected_net < 0) {
        return movables[random_movable(rng)];
    }

    vector<char> visited(state.nodes.size(), 0);
    vector<int> candidates;
    for (const auto& ref : state.nets[selected_net].pins) {
        const int node_idx = ref.nodeIdx;
        if (!is_movable[node_idx] || visited[node_idx]) {
            continue;
        }
        visited[node_idx] = 1;
        candidates.push_back(node_idx);
    }

    if (candidates.empty()) {
        return movables[random_movable(rng)];
    }

    uniform_int_distribution<int> pick_candidate(0, static_cast<int>(candidates.size()) - 1);
    return candidates[pick_candidate(rng)];
}

bool apply_random_relocate_move(PlacementState& state,
                                Occupancy& occ,
                                const vector<int>& movables,
                                mt19937& rng,
                                AppliedMove& rec,
                                vector<int>& moved_nodes) {
    if (movables.empty()) {
        return false;
    }

    uniform_int_distribution<int> pick_comp(0, static_cast<int>(movables.size()) - 1);
    const int idx = movables[pick_comp(rng)];
    Node& node = state.nodes[idx];

    uniform_int_distribution<int> pick_x(0, occ.grid_w - node.w);
    uniform_int_distribution<int> pick_y(0, occ.grid_h - node.h);
    const int nx = pick_x(rng);
    const int ny = pick_y(rng);

    const int ox = node.x;
    const int oy = node.y;

    occ.unstamp(state, idx, ox, oy);
    if (!occ.can_place(state, idx, nx, ny)) {
        occ.stamp(state, idx, ox, oy);
        return false;
    }

    node.x = nx;
    node.y = ny;
    occ.stamp(state, idx, nx, ny);

    rec.type = AppliedMove::Type::Relocate;
    rec.a = idx;
    rec.old_ax = ox;
    rec.old_ay = oy;
    moved_nodes = {idx};
    return true;
}

bool apply_random_swap_move(PlacementState& state,
                            Occupancy& occ,
                            const vector<int>& movables,
                            mt19937& rng,
                            AppliedMove& rec,
                            vector<int>& moved_nodes) {
    int a = -1;
    int b = -1;
    if (!sample_distinct_pair(movables, rng, a, b)) {
        return false;
    }

    Node& na = state.nodes[a];
    Node& nb = state.nodes[b];

    const int ax = na.x;
    const int ay = na.y;
    const int bx = nb.x;
    const int by = nb.y;

    occ.unstamp(state, a, ax, ay);
    occ.unstamp(state, b, bx, by);

    const bool ok_a = occ.can_place(state, a, bx, by);
    const bool ok_b = occ.can_place(state, b, ax, ay);
    if (!ok_a || !ok_b) {
        occ.stamp(state, a, ax, ay);
        occ.stamp(state, b, bx, by);
        return false;
    }

    na.x = bx;
    na.y = by;
    nb.x = ax;
    nb.y = ay;
    occ.stamp(state, a, na.x, na.y);
    occ.stamp(state, b, nb.x, nb.y);

    rec.type = AppliedMove::Type::Swap;
    rec.a = a;
    rec.b = b;
    rec.old_ax = ax;
    rec.old_ay = ay;
    rec.old_bx = bx;
    rec.old_by = by;
    moved_nodes = {a, b};
    return true;
}

bool try_relocate_at(PlacementState& state,
                    Occupancy& occ,
                    int idx,
                    int nx,
                    int ny,
                    AppliedMove& rec,
                    vector<int>& moved_nodes) {
    if (idx < 0 || idx >= static_cast<int>(state.nodes.size())) {
        return false;
    }

    Node& node = state.nodes[idx];
    const int ox = node.x;
    const int oy = node.y;

    occ.unstamp(state, idx, ox, oy);
    if (!occ.can_place(state, idx, nx, ny)) {
        occ.stamp(state, idx, ox, oy);
        return false;
    }

    node.x = nx;
    node.y = ny;
    occ.stamp(state, idx, nx, ny);

    rec.type = AppliedMove::Type::Relocate;
    rec.a = idx;
    rec.old_ax = ox;
    rec.old_ay = oy;
    moved_nodes = {idx};
    return true;
}

bool apply_heuristic_relocate_move(PlacementState& state,
                                   Occupancy& occ,
                                   const vector<int>& movables,
                                   mt19937& rng,
                                   AppliedMove& rec,
                                   vector<int>& moved_nodes) {
    if (movables.empty()) {
        return false;
    }

    const int idx = pick_node_from_high_cost_net(state, movables, rng);
    if (idx < 0) {
        return false;
    }

    const Node& node = state.nodes[idx];
    const int local_radius_x = max(2, node.w * 2);
    const int local_radius_y = max(2, node.h * 2);
    const int min_x = max(0, node.x - local_radius_x);
    const int max_x = min(occ.grid_w - node.w, node.x + local_radius_x);
    const int min_y = max(0, node.y - local_radius_y);
    const int max_y = min(occ.grid_h - node.h, node.y + local_radius_y);

    if (min_x <= max_x && min_y <= max_y) {
        uniform_int_distribution<int> pick_x(min_x, max_x);
        uniform_int_distribution<int> pick_y(min_y, max_y);
        for (int i = 0; i < 8; ++i) {
            if (try_relocate_at(state, occ, idx, pick_x(rng), pick_y(rng), rec, moved_nodes)) {
                return true;
            }
        }
    }

    uniform_int_distribution<int> global_x(0, occ.grid_w - node.w);
    uniform_int_distribution<int> global_y(0, occ.grid_h - node.h);
    for (int i = 0; i < 4; ++i) {
        if (try_relocate_at(state, occ, idx, global_x(rng), global_y(rng), rec, moved_nodes)) {
            return true;
        }
    }
    return false;
}

bool apply_heuristic_swap_move(PlacementState& state,
                               Occupancy& occ,
                               const vector<int>& movables,
                               mt19937& rng,
                               AppliedMove& rec,
                               vector<int>& moved_nodes) {
    if (movables.size() < 2) {
        return false;
    }

    const int a = pick_node_from_high_cost_net(state, movables, rng);
    if (a < 0) {
        return false;
    }

    vector<int> candidates;
    candidates.reserve(movables.size());
    for (int idx : movables) {
        if (idx != a) {
            candidates.push_back(idx);
        }
    }
    if (candidates.empty()) {
        return false;
    }

    shuffle(candidates.begin(), candidates.end(), rng);
    const int max_trials = min(static_cast<int>(candidates.size()), 8);
    for (int i = 0; i < max_trials; ++i) {
        const int b = candidates[i];
        Node& na = state.nodes[a];
        Node& nb = state.nodes[b];
        const int ax = na.x;
        const int ay = na.y;
        const int bx = nb.x;
        const int by = nb.y;

        occ.unstamp(state, a, ax, ay);
        occ.unstamp(state, b, bx, by);

        const bool ok_a = occ.can_place(state, a, bx, by);
        const bool ok_b = occ.can_place(state, b, ax, ay);
        if (!ok_a || !ok_b) {
            occ.stamp(state, a, ax, ay);
            occ.stamp(state, b, bx, by);
            continue;
        }

        na.x = bx;
        na.y = by;
        nb.x = ax;
        nb.y = ay;
        occ.stamp(state, a, na.x, na.y);
        occ.stamp(state, b, nb.x, nb.y);

        rec.type = AppliedMove::Type::Swap;
        rec.a = a;
        rec.b = b;
        rec.old_ax = ax;
        rec.old_ay = ay;
        rec.old_bx = bx;
        rec.old_by = by;
        moved_nodes = {a, b};
        return true;
    }

    return false;
}

void revert_move(PlacementState& state, Occupancy& occ, const AppliedMove& rec) {
    if (rec.type == AppliedMove::Type::Relocate) {
        Node& n = state.nodes[rec.a];
        occ.unstamp(state, rec.a, n.x, n.y);
        n.x = rec.old_ax;
        n.y = rec.old_ay;
        occ.stamp(state, rec.a, n.x, n.y);
        return;
    }

    Node& a = state.nodes[rec.a];
    Node& b = state.nodes[rec.b];
    occ.unstamp(state, rec.a, a.x, a.y);
    occ.unstamp(state, rec.b, b.x, b.y);
    a.x = rec.old_ax;
    a.y = rec.old_ay;
    b.x = rec.old_bx;
    b.y = rec.old_by;
    occ.stamp(state, rec.a, a.x, a.y);
    occ.stamp(state, rec.b, b.x, b.y);
}

RunMetrics run_single_sa(const string& level,
                         const string& benchmark_path,
                         unsigned seed,
                         double t0,
                         double alpha,
                         int steps_per_t,
                         bool use_heuristic,
                         int illegal_retry) {
    PlacementDB initial_db;
    initial_db.parseFile(benchmark_path);
    initial_db.randomLegalPlacement(seed);

    PlacementState state = static_cast<const PlacementState&>(initial_db);
    Occupancy occ(state);
    string err;
    if (!build_occupancy_from_state(state, occ, err)) {
        throw runtime_error("invalid initial placement: " + err);
    }

    RunMetrics metrics;
    metrics.level = level;
    metrics.benchmark_name = level;
    metrics.benchmark_path = benchmark_path;
    metrics.seed = seed;
    metrics.t0 = t0;
    metrics.alpha = alpha;
    metrics.steps_per_t = steps_per_t;
    metrics.use_heuristic = use_heuristic;
    metrics.baseline_cost = total_hpwl(state);

    const vector<int> movables = collect_movable_nodes(state);
    if (movables.empty()) {
        metrics.final_cost = metrics.baseline_cost;
        metrics.best_cost = metrics.baseline_cost;
        PlacementDB legality_db;
        legality_db.gridW = state.gridW;
        legality_db.gridH = state.gridH;
        legality_db.nodes = state.nodes;
        legality_db.nets = state.nets;
        legality_db.nodeNameToIdx = state.nodeNameToIdx;
        metrics.legality_ok = legality_db.isPlacementLegal();
        return metrics;
    }

    mt19937 rng(seed);
    uniform_real_distribution<double> prob01(0.0, 1.0);

    long long current_cost = metrics.baseline_cost;
    long long best_cost = current_cost;
    long long total_steps = 0;
    long long total_stages = 0;
    int stagnant_stages = 0;
    double temp = t0;

    const auto start = chrono::steady_clock::now();

    while (stagnant_stages < 10) {
        const long long best_before_stage = best_cost;

        for (int step = 0; step < steps_per_t; ++step) {
            AppliedMove rec;
            vector<int> moved_nodes;
            bool legal = false;

            for (int retry = 0; retry < illegal_retry; ++retry) {
                legal = use_heuristic
                    ? (uniform_real_distribution<double>(0.0, 1.0)(rng) < 0.5
                           ? apply_heuristic_relocate_move(state, occ, movables, rng, rec, moved_nodes)
                           : apply_heuristic_swap_move(state, occ, movables, rng, rec, moved_nodes))
                    : (uniform_real_distribution<double>(0.0, 1.0)(rng) < 0.5
                           ? apply_random_relocate_move(state, occ, movables, rng, rec, moved_nodes)
                           : apply_random_swap_move(state, occ, movables, rng, rec, moved_nodes));
                if (legal) {
                    break;
                }
            }

            if (legal) {
                Occupancy sanity(state);
                string sanity_err;
                if (!build_occupancy_from_state(state, sanity, sanity_err)) {
                    revert_move(state, occ, rec);
                    continue;
                }

                const long long candidate_cost = total_hpwl(state);
                const long long delta = candidate_cost - current_cost;

                bool accept = false;
                if (delta <= 0) {
                    accept = true;
                } else {
                    const double accept_prob = exp(-static_cast<double>(delta) / max(temp, 1e-12));
                    accept = prob01(rng) < accept_prob;
                }

                if (accept) {
                    current_cost = candidate_cost;
                    best_cost = min(best_cost, current_cost);
                } else {
                    revert_move(state, occ, rec);
                    Occupancy check_state(state);
                    string check_err;
                    if (!build_occupancy_from_state(state, check_state, check_err)) {
                        throw runtime_error("rollback left an illegal state: " + check_err);
                    }
                }
            }

            ++total_steps;
        }

        ++total_stages;
        if (best_cost < best_before_stage) {
            stagnant_stages = 0;
        } else {
            ++stagnant_stages;
        }

        temp = max(temp * alpha, 1e-12);
    }

    const auto end = chrono::steady_clock::now();
    metrics.final_cost = current_cost;
    metrics.best_cost = best_cost;
    metrics.total_steps = total_steps;
    metrics.total_stages = total_stages;
    metrics.runtime_ms = chrono::duration_cast<chrono::milliseconds>(end - start).count();

    PlacementDB legality_db;
    legality_db.gridW = state.gridW;
    legality_db.gridH = state.gridH;
    legality_db.nodes = state.nodes;
    legality_db.nets = state.nets;
    legality_db.nodeNameToIdx = state.nodeNameToIdx;
    metrics.legality_ok = legality_db.isPlacementLegal();

    if (metrics.baseline_cost > 0) {
        metrics.improvement_pct = 100.0 * static_cast<double>(metrics.baseline_cost - metrics.best_cost) /
                                  static_cast<double>(metrics.baseline_cost);
    }

    return metrics;
}

void write_results_header(ofstream& out) {
    out << "level,benchmark_name,benchmark_path,seed,T0,alpha,steps_per_T,"
        << "heuristic_mode,baseline_cost,final_cost,best_cost,improvement_pct,"
        << "total_steps,total_stages,runtime_ms,legality_ok\n";
}

void write_result_row(ofstream& out, const RunMetrics& m) {
    out << csv_escape(m.level) << ','
        << csv_escape(m.benchmark_name) << ','
        << csv_escape(m.benchmark_path) << ','
        << m.seed << ','
        << fixed << setprecision(3) << m.t0 << ','
        << fixed << setprecision(5) << m.alpha << ','
        << m.steps_per_t << ','
        << (m.use_heuristic ? "heuristic" : "random") << ','
        << m.baseline_cost << ','
        << m.final_cost << ','
        << m.best_cost << ','
        << fixed << setprecision(4) << m.improvement_pct << ','
        << m.total_steps << ','
        << m.total_stages << ','
        << m.runtime_ms << ','
        << (m.legality_ok ? "true" : "false") << '\n';
}

void write_summary_header(ofstream& out) {
    out << "level,T0,alpha,steps_per_T,heuristic_mode,runs,mean_improvement_pct,"
        << "best_improvement_pct,mean_runtime_ms,legality_pass_rate,mean_total_steps,"
        << "mean_total_stages\n";
}

void write_summary_row(ofstream& out, const SummaryKey& key, const SummaryStats& stats) {
    const double runs = static_cast<double>(max(1, stats.runs));
    out << csv_escape(key.level) << ','
        << fixed << setprecision(3) << key.t0 << ','
        << fixed << setprecision(5) << key.alpha << ','
        << key.steps_per_t << ','
        << (key.use_heuristic ? "heuristic" : "random") << ','
        << stats.runs << ','
        << fixed << setprecision(4) << (stats.sum_improvement / runs) << ','
        << fixed << setprecision(4) << stats.best_improvement << ','
        << fixed << setprecision(2) << (stats.sum_runtime_ms / runs) << ','
        << fixed << setprecision(4) << (100.0 * static_cast<double>(stats.legality_pass) / runs) << ','
        << fixed << setprecision(2) << (static_cast<double>(stats.sum_total_steps) / runs) << ','
        << fixed << setprecision(2) << (static_cast<double>(stats.sum_total_stages) / runs) << '\n';
}

bool parse_args(int argc,
                char* argv[],
                vector<string>& levels,
                SweepRanges& ranges,
                string& results_path,
                string& summary_path,
                bool& use_heuristic) {
    levels = {"easy", "mid", "hard", "large"};
    ranges.t0 = {24.0, 96.0};
    ranges.alpha = {0.86, 0.992};
    ranges.steps_per_t = 200;
    ranges.point_count = 10;
    results_path = "sweeps/results.csv";
    summary_path = "sweeps/summary.csv";
    use_heuristic = false;

    for (int i = 1; i < argc; ++i) {
        const string key = argv[i];
        if (key == "--level") {
            if (i + 1 >= argc) {
                cerr << "--level requires a value\n";
                return false;
            }
            const string value = argv[++i];
            if (value == "easy" || value == "mid" || value == "hard" || value == "large") {
                levels = {value};
            } else if (value == "all") {
                levels = {"easy", "mid", "hard", "large"};
            } else {
                cerr << "Unknown level: " << value << "\n";
                return false;
            }
            continue;
        }
        if (key == "--t0_range") {
            if (i + 1 >= argc) {
                cerr << "--t0_range requires min,max\n";
                return false;
            }
            try {
                ranges.t0 = parse_double_range(argv[++i], "t0");
            } catch (const exception& e) {
                cerr << e.what() << "\n";
                return false;
            }
            continue;
        }
        if (key == "--alpha_range") {
            if (i + 1 >= argc) {
                cerr << "--alpha_range requires min,max\n";
                return false;
            }
            try {
                ranges.alpha = parse_double_range(argv[++i], "alpha");
            } catch (const exception& e) {
                cerr << e.what() << "\n";
                return false;
            }
            continue;
        }
        if (key == "--points") {
            if (i + 1 >= argc) {
                cerr << "--points requires a positive integer\n";
                return false;
            }
            try {
                size_t pos = 0;
                const int value = stoi(argv[++i], &pos);
                if (pos != string(argv[i]).size() || value <= 0) {
                    throw runtime_error("invalid points value");
                }
                ranges.point_count = value;
            } catch (const exception& e) {
                cerr << e.what() << "\n";
                return false;
            }
            continue;
        }
        if (key == "--steps_per_t") {
            if (i + 1 >= argc) {
                cerr << "--steps_per_t requires a positive integer\n";
                return false;
            }
            try {
                size_t pos = 0;
                const int value = stoi(argv[++i], &pos);
                if (pos != string(argv[i]).size() || value <= 0) {
                    throw runtime_error("invalid steps_per_t value");
                }
                ranges.steps_per_t = value;
            } catch (const exception& e) {
                cerr << e.what() << "\n";
                return false;
            }
            continue;
        }
        if (key == "--results") {
            if (i + 1 >= argc) {
                cerr << "--results requires a path\n";
                return false;
            }
            results_path = argv[++i];
            continue;
        }
        if (key == "--summary") {
            if (i + 1 >= argc) {
                cerr << "--summary requires a path\n";
                return false;
            }
            summary_path = argv[++i];
            continue;
        }
        if (key == "--use_heuristic") {
            use_heuristic = true;
            continue;
        }
        if (key == "--help" || key == "-h") {
            return false;
        }

        cerr << "Unknown option: " << key << "\n";
        return false;
    }

    return true;
}

void print_usage(const char* argv0) {
    cerr << "Usage: " << argv0
                << " [--level all|easy|mid|hard|large]"
            << " [--t0_range min,max] [--alpha_range min,max]"
                << " [--points N] [--steps_per_t N]"
         << " [--results sweeps/results.csv] [--summary sweeps/summary.csv]"
            << " [default steps_per_T=200]"
         << " [--use_heuristic]\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    vector<string> levels;
    SweepRanges ranges;
    string results_path;
    string summary_path;
    bool use_heuristic = false;

    if (!parse_args(argc, argv, levels, ranges, results_path, summary_path, use_heuristic)) {
        print_usage(argv[0]);
        return 1;
    }

    filesystem::create_directories(filesystem::path(results_path).parent_path());
    filesystem::create_directories(filesystem::path(summary_path).parent_path());

    ofstream results_out(results_path);
    ofstream summary_out(summary_path);
    if (!results_out || !summary_out) {
        cerr << "Failed to open output CSV files\n";
        return 1;
    }

    write_results_header(results_out);
    write_summary_header(summary_out);

    map<SummaryKey, SummaryStats> summary_map;
    const vector<SweepPoint> points = build_sweep_points(ranges);
    const size_t total_runs = static_cast<size_t>(levels.size()) * points.size();
    size_t completed_runs = 0;
    size_t last_progress_width = 0;

    if (total_runs > 0) {
        print_progress(make_progress_line("sweep", 0, total_runs), last_progress_width);
    }

    for (const auto& level : levels) {
        string benchmark_path;
        string error;
        if (!ensure_benchmark_generated(level, benchmark_path, error)) {
            cout << '\n';
            cerr << error << "\n";
            return 1;
        }

        DemoPreset preset;
        if (!load_demo_preset(level, preset, error)) {
            cout << '\n';
            cerr << error << "\n";
            return 1;
        }

        const unsigned seed = preset.sa.seed;
        for (const auto& point : points) {
            try {
                RunMetrics metrics = run_single_sa(level,
                                                   benchmark_path,
                                                   seed,
                                                   point.t0,
                                                   point.alpha,
                                                   ranges.steps_per_t,
                                                   use_heuristic,
                                                   preset.sa.illegal_retry);

                write_result_row(results_out, metrics);

                SummaryKey key{level, point.t0, point.alpha, ranges.steps_per_t, use_heuristic};
                SummaryStats& stats = summary_map[key];
                ++stats.runs;
                stats.sum_improvement += metrics.improvement_pct;
                stats.best_improvement = max(stats.best_improvement, metrics.improvement_pct);
                stats.sum_runtime_ms += static_cast<double>(metrics.runtime_ms);
                if (metrics.legality_ok) {
                    ++stats.legality_pass;
                }
                stats.sum_total_steps += metrics.total_steps;
                stats.sum_total_stages += metrics.total_stages;

                ++completed_runs;
                print_progress(make_progress_line("sweep", completed_runs, total_runs), last_progress_width);
            } catch (const exception& e) {
                cout << '\n';
                cerr << "run failed for level=" << level
                     << " seed=" << seed
                     << " T0=" << point.t0
                     << " alpha=" << point.alpha
                     << " steps_per_T=" << ranges.steps_per_t
                     << ": " << e.what() << "\n";
                return 1;
            }
        }
    }

    for (const auto& [key, stats] : summary_map) {
        write_summary_row(summary_out, key, stats);
    }

    if (total_runs > 0) {
        cout << '\n';
    }

    cout << "Sweep complete\n";
    cout << "Results: " << results_path << "\n";
    cout << "Summary: " << summary_path << "\n";
    return 0;
}