#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../include/commands.h"
#include "../src/placement.cpp"

using namespace std;

struct DebugLogger {
    explicit DebugLogger(const string& path) : out(path) {}
    ofstream out;

    bool ok() const {
        return out.good();
    }

    void line(const string& s) {
        out << s << "\n";
    }
};

struct NullBuffer : streambuf {
    int overflow(int c) override {
        return c;
    }
};

struct ScopedSilence {
    ScopedSilence(ostream& os, streambuf* replacement)
        : os_(os), old_(os.rdbuf(replacement)) {}

    ~ScopedSilence() {
        os_.rdbuf(old_);
    }

    ostream& os_;
    streambuf* old_;
};

static bool check_or_fail(bool cond, const string& msg, string& err) {
    if (cond) return true;
    err = msg;
    return false;
}

static int find_fixed_idx(const PlacementDB& db) {
    for (int i = 0; i < (int)db.comps.size(); ++i) {
        if (db.comps[i].fixed) return i;
    }
    return -1;
}

static vector<int> find_movable_indices(const PlacementDB& db) {
    vector<int> ans;
    for (int i = 0; i < (int)db.comps.size(); ++i) {
        if (!db.comps[i].fixed) ans.push_back(i);
    }
    return ans;
}

static bool find_legal_relocate_target(PlacementDB& db, int compIdx, int& outX, int& outY) {
    const int oldX = db.comps[compIdx].x;
    const int oldY = db.comps[compIdx].y;
    const int w = db.comps[compIdx].w;
    const int h = db.comps[compIdx].h;

    for (int y = 0; y <= db.gridH - h; ++y) {
        for (int x = 0; x <= db.gridW - w; ++x) {
            if (x == oldX && y == oldY) continue;

            if (db.moveComponent(compIdx, x, y)) {
                outX = x;
                outY = y;

                bool restored = db.restoreLastMove();
                if (!restored) return false;
                if (db.comps[compIdx].x != oldX) return false;
                if (db.comps[compIdx].y != oldY) return false;
                return true;
            }
        }
    }

    return false;
}

static bool find_swappable_pair(PlacementDB& db,
                                const vector<int>& movables,
                                int& outA,
                                int& outB) {
    for (int i = 0; i < (int)movables.size(); ++i) {
        for (int j = i + 1; j < (int)movables.size(); ++j) {
            int a = movables[i];
            int b = movables[j];

            if (db.swapComponents(a, b)) {
                outA = a;
                outB = b;

                bool restored = db.restoreLastMove();
                if (!restored) return false;
                return true;
            }
        }
    }

    return false;
}

static bool run_generator_case(const string& caseFile,
                               unsigned seed,
                               int gridW,
                               int gridH,
                               int numComponents,
                               int numNets,
                               bool silent,
                               string& err) {
    vector<string> args = {
        "generator",
        caseFile,
        to_string(gridW),
        to_string(gridH),
        to_string(numComponents),
        to_string(numNets),
        to_string(seed)
    };
    vector<char*> argv;
    argv.reserve(args.size());
    for (string& s : args) argv.push_back(&s[0]);

    NullBuffer nullbuf;
    unique_ptr<ScopedSilence> muteOut;
    unique_ptr<ScopedSilence> muteErr;
    if (silent) {
        muteOut = make_unique<ScopedSilence>(cout, &nullbuf);
        muteErr = make_unique<ScopedSilence>(cerr, &nullbuf);
    }

    int rc = run_generator_cli((int)argv.size(), argv.data());
    if (rc != 0) {
        err = "generator failed";
        return false;
    }
    return true;
}

static void sample_regression_params(unsigned seed,
                                     int& gridW,
                                     int& gridH,
                                     int& numComponents,
                                     int& numNets) {
    mt19937 rng(seed);
    uniform_int_distribution<int> gridDist(12, 40);
    uniform_real_distribution<double> utilDist(0.12, 0.28);
    uniform_real_distribution<double> netRatioDist(1.2, 2.4);

    gridW = gridDist(rng);
    gridH = gridDist(rng);

    int area = gridW * gridH;
    int maxCompsByUtil = max(6, (int)(area * utilDist(rng) / 2.0));
    int cap = min(80, maxCompsByUtil);
    uniform_int_distribution<int> compDist(6, max(6, cap));
    numComponents = compDist(rng);

    int netsMin = max(4, (int)(numComponents * 0.8));
    int netsMax = max(netsMin, (int)(numComponents * netRatioDist(rng)));
    uniform_int_distribution<int> netDist(netsMin, netsMax);
    numNets = netDist(rng);
}

static bool run_move_tests_on_case(const string& caseFile,
                                   unsigned seed,
                                   DebugLogger* dbg,
                                   string& err) {
    PlacementDB db;
    try {
        db.parseFile(caseFile);
        db.randomLegalPlacement(seed);
    } catch (const exception& ex) {
        err = string("exception during parse/place: ") + ex.what();
        return false;
    }

    if (dbg) dbg->line("seed=" + to_string(seed));
    if (!check_or_fail(db.isPlacementLegal(), "initial placement is illegal", err)) return false;
    if (dbg) dbg->line("step1 ok: initial placement legal");

    int fixedIdx = find_fixed_idx(db);
    if (!check_or_fail(fixedIdx != -1, "no fixed component found", err)) return false;

    vector<int> movables = find_movable_indices(db);
    if (!check_or_fail(movables.size() >= 2, "need at least 2 movable components", err)) return false;

    int a = movables[0];
    int b = movables[1];
    if (dbg) {
        dbg->line("picked movable a=" + db.comps[a].id + ", b=" + db.comps[b].id);
        dbg->line("fixed=" + db.comps[fixedIdx].id);
    }

    {
        int fx = db.comps[fixedIdx].x;
        int fy = db.comps[fixedIdx].y;
        int ax = db.comps[a].x;
        int ay = db.comps[a].y;

        bool ok = db.swapComponents(fixedIdx, a);
        if (!check_or_fail(ok == false, "fixed+movable swap should fail", err)) return false;
        if (!check_or_fail(db.isPlacementLegal(), "illegal after fixed+movable swap", err)) return false;
        if (!check_or_fail(db.comps[fixedIdx].x == fx && db.comps[fixedIdx].y == fy,
                           "fixed component moved unexpectedly", err)) return false;
        if (!check_or_fail(db.comps[a].x == ax && db.comps[a].y == ay,
                           "movable component changed unexpectedly", err)) return false;
        if (dbg) dbg->line("step2 ok: fixed+movable swap rejected");
    }

    {
        int ax = db.comps[a].x;
        int ay = db.comps[a].y;
        int bx = db.comps[b].x;
        int by = db.comps[b].y;

        bool ok = db.swapComponents(a, b);
        if (ok) {
            if (!check_or_fail(db.comps[a].x == bx && db.comps[a].y == by,
                               "movable swap did not move a correctly", err)) return false;
            if (!check_or_fail(db.comps[b].x == ax && db.comps[b].y == ay,
                               "movable swap did not move b correctly", err)) return false;
        } else {
            if (!check_or_fail(db.comps[a].x == ax && db.comps[a].y == ay,
                               "failed movable swap changed a", err)) return false;
            if (!check_or_fail(db.comps[b].x == bx && db.comps[b].y == by,
                               "failed movable swap changed b", err)) return false;
        }
        if (!check_or_fail(db.isPlacementLegal(), "illegal after movable swap", err)) return false;

        if (dbg) {
            ostringstream oss;
            oss << "step3 ok: movable swap result=" << ok
                << " a=(" << db.comps[a].x << "," << db.comps[a].y << ")"
                << " b=(" << db.comps[b].x << "," << db.comps[b].y << ")";
            dbg->line(oss.str());
        }
    }

    {
        int ax = db.comps[a].x;
        int ay = db.comps[a].y;

        bool ok = db.swapComponents(a, a);
        if (!check_or_fail(ok == false, "swap self should fail", err)) return false;
        if (!check_or_fail(db.comps[a].x == ax && db.comps[a].y == ay,
                           "swap self changed component", err)) return false;
        if (!check_or_fail(db.isPlacementLegal(), "illegal after swap self", err)) return false;
        if (dbg) dbg->line("step4 ok: swap self rejected");
    }

    {
        int oldAx = db.comps[a].x;
        int oldAy = db.comps[a].y;
        int newAx = -1;
        int newAy = -1;

        bool foundTarget = find_legal_relocate_target(db, a, newAx, newAy);
        if (!check_or_fail(foundTarget, "no legal relocate target found", err)) return false;

        bool moved = db.moveComponent(a, newAx, newAy);
        if (!check_or_fail(moved, "relocate move failed", err)) return false;
        if (!check_or_fail(db.comps[a].x == newAx && db.comps[a].y == newAy,
                           "relocate result mismatch", err)) return false;
        if (!check_or_fail(db.isPlacementLegal(), "illegal after relocate", err)) return false;

        bool restored = db.restoreLastMove();
        if (!check_or_fail(restored, "restore after relocate failed", err)) return false;
        if (!check_or_fail(db.comps[a].x == oldAx && db.comps[a].y == oldAy,
                           "restore after relocate did not recover original", err)) return false;
        if (!check_or_fail(db.isPlacementLegal(), "illegal after restore relocate", err)) return false;

        if (dbg) {
            ostringstream oss;
            oss << "step5 ok: relocate->restore old=(" << oldAx << "," << oldAy
                << ") target=(" << newAx << "," << newAy << ")";
            dbg->line(oss.str());
        }
    }

    {
        int sa = -1;
        int sb = -1;
        bool foundPair = find_swappable_pair(db, movables, sa, sb);
        if (!check_or_fail(foundPair, "no swappable movable pair found", err)) return false;

        int oldSax = db.comps[sa].x;
        int oldSay = db.comps[sa].y;
        int oldSbx = db.comps[sb].x;
        int oldSby = db.comps[sb].y;

        bool swapped = db.swapComponents(sa, sb);
        if (!check_or_fail(swapped, "swap for restore test failed", err)) return false;
        if (!check_or_fail(db.comps[sa].x == oldSbx && db.comps[sa].y == oldSby,
                           "swap restore test mismatch on sa", err)) return false;
        if (!check_or_fail(db.comps[sb].x == oldSax && db.comps[sb].y == oldSay,
                           "swap restore test mismatch on sb", err)) return false;
        if (!check_or_fail(db.isPlacementLegal(), "illegal after swap before restore", err)) return false;

        bool restored = db.restoreLastMove();
        if (!check_or_fail(restored, "restore after swap failed", err)) return false;
        if (!check_or_fail(db.comps[sa].x == oldSax && db.comps[sa].y == oldSay,
                           "restore after swap mismatch on sa", err)) return false;
        if (!check_or_fail(db.comps[sb].x == oldSbx && db.comps[sb].y == oldSby,
                           "restore after swap mismatch on sb", err)) return false;
        if (!check_or_fail(db.isPlacementLegal(), "illegal after restore swap", err)) return false;

        if (dbg) {
            ostringstream oss;
            oss << "step6 ok: swap->restore pair(" << db.comps[sa].id
                << "," << db.comps[sb].id << ")";
            dbg->line(oss.str());
        }
    }

    {
        db.commitMoves();
        bool restored = db.restoreLastMove();
        if (!check_or_fail(restored == false, "empty-history restore should fail", err)) return false;
        if (dbg) dbg->line("step7 ok: empty-history restore rejected");
    }

    return true;
}

static int run_regression(unsigned startSeed, int count) {
    int passCount = 0;
    vector<unsigned> seeds;
    vector<string> results;
    vector<string> reasons;

    for (int i = 0; i < count; ++i) {
        unsigned seed = startSeed + (unsigned)i;
        string caseFile = "move_reg_case_seed_" + to_string(seed) + ".tmp.txt";
        string err;
        int gridW = 0;
        int gridH = 0;
        int numComponents = 0;
        int numNets = 0;

        sample_regression_params(seed, gridW, gridH, numComponents, numNets);
        seeds.push_back(seed);

        if (!run_generator_case(caseFile,
                                seed,
                                gridW,
                                gridH,
                                numComponents,
                                numNets,
                                true,
                                err)) {
            results.push_back("FAIL");
            reasons.push_back(err + " params=(" + to_string(gridW) + "x" + to_string(gridH) +
                             ", comps=" + to_string(numComponents) +
                             ", nets=" + to_string(numNets) + ")");
            remove(caseFile.c_str());
            continue;
        }

        if (!run_move_tests_on_case(caseFile, seed, nullptr, err)) {
            results.push_back("FAIL");
            reasons.push_back(err + " params=(" + to_string(gridW) + "x" + to_string(gridH) +
                             ", comps=" + to_string(numComponents) +
                             ", nets=" + to_string(numNets) + ")");
            remove(caseFile.c_str());
            continue;
        }

        results.push_back("PASS");
        reasons.push_back("OK params=(" + to_string(gridW) + "x" + to_string(gridH) +
                          ", comps=" + to_string(numComponents) +
                          ", nets=" + to_string(numNets) + ")");
        ++passCount;
        remove(caseFile.c_str());
    }

    cout << "Regression summary\n";
    cout << "total=" << count << " pass=" << passCount
         << " fail=" << (count - passCount) << "\n";
    for (size_t i = 0; i < seeds.size(); ++i) {
        cout << "seed=" << seeds[i]
             << " result=" << results[i]
             << " reason=" << reasons[i] << "\n";
    }
    return passCount == count ? 0 : 1;
}

static int run_debug(unsigned seed) {
    string caseFile = "move_debug_seed_" + to_string(seed) + ".txt";
    string logFile = "move_debug_seed_" + to_string(seed) + ".log";
    string err;
    int gridW = 0;
    int gridH = 0;
    int numComponents = 0;
    int numNets = 0;

    // Debug and regression must share the exact same seed->params mapping
    // so users can reproduce a failing regression case in debug mode.
    sample_regression_params(seed, gridW, gridH, numComponents, numNets);

    DebugLogger logger(logFile);
    if (!logger.ok()) {
        cerr << "cannot create debug log file: " << logFile << "\n";
        return 1;
    }

    logger.line("debug mode start");
    logger.line("seed=" + to_string(seed));
    logger.line("params=(" + to_string(gridW) + "x" + to_string(gridH) +
                ", comps=" + to_string(numComponents) +
                ", nets=" + to_string(numNets) + ")");

    if (!run_generator_case(caseFile,
                            seed,
                            gridW,
                            gridH,
                            numComponents,
                            numNets,
                            true,
                            err)) {
        logger.line("generator failed: " + err);
        cerr << "Debug failed. See log: " << logFile << "\n";
        return 1;
    }
    logger.line("generator output file=" + caseFile);

    if (!run_move_tests_on_case(caseFile, seed, &logger, err)) {
        logger.line("move engine test failed: " + err);
        cerr << "Debug failed. See log: " << logFile << "\n";
        return 1;
    }

    logger.line("all checks passed");
    cout << "Debug passed. Log: " << logFile << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage:\n";
        cerr << argv[0] << " regression [start_seed] [count]\n";
        cerr << argv[0] << " debug <seed>\n";
        return 1;
    }

    string mode = argv[1];

    if (mode == "regression") {
        unsigned startSeed = 1;
        int count = 100;
        if (argc >= 3) startSeed = static_cast<unsigned>(stoul(argv[2]));
        if (argc >= 4) count = stoi(argv[3]);
        if (count <= 0) {
            cerr << "count must be positive\n";
            return 1;
        }
        return run_regression(startSeed, count);
    }

    if (mode == "debug") {
        if (argc < 3) {
            cerr << "debug mode requires seed\n";
            return 1;
        }
        unsigned seed = static_cast<unsigned>(stoul(argv[2]));
        return run_debug(seed);
    }

    cerr << "Unknown mode: " << mode << "\n";
    cerr << "Expected regression or debug\n";
    return 1;
}