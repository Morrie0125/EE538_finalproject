#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <random>
#include <unordered_set>
#include <algorithm>
#include <filesystem>
#include "../include/demo_config.h"
#include "../include/commands.h"

using namespace std;

struct GenPin {
    string name;
    int dx, dy;
};

struct GenComponent {
    string id;
    int w, h;
    bool fixed = false;
    int x = -1, y = -1;
    vector<GenPin> pins;
};

struct GenNet {
    string id;
    vector<pair<int,int>> refs; // (component index, pin index)
};

enum class DemoMode {
    EASY,
    MID,
    HARD,
    LARGE,
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
    if (text == "large") {
        mode = DemoMode::LARGE;
        return true;
    }
    return false;
}

string demo_mode_name(DemoMode mode) {
    if (mode == DemoMode::EASY) {
        return "easy";
    }
    if (mode == DemoMode::MID) {
        return "mid";
    }
    if (mode == DemoMode::HARD) {
        return "hard";
    }
    return "large";
}

static bool overlaps(int x1, int y1, int w1, int h1,
                     int x2, int y2, int w2, int h2) {
    if (x1 + w1 <= x2) return false;
    if (x2 + w2 <= x1) return false;
    if (y1 + h1 <= y2) return false;
    if (y2 + h2 <= y1) return false;
    return true;
}

static bool canPlaceFixed(const vector<GenComponent>& comps, int idx,
                          int x, int y, int gridW, int gridH) {
    const auto& c = comps[idx];
    if (x < 0 || y < 0 || x + c.w > gridW || y + c.h > gridH) return false;

    for (int i = 0; i < idx; ++i) {
        if (!comps[i].fixed) continue;
        if (overlaps(x, y, c.w, c.h, comps[i].x, comps[i].y, comps[i].w, comps[i].h)) {
            return false;
        }
    }
    return true;
}

string render_progress_bar(const string& label, int current, int total, int width = 24) {
    if (total <= 0) {
        total = 1;
    }
    current = max(0, min(current, total));
    const int filled = static_cast<int>((1.0 * current * width) / total);
    string bar = "[";
    bar.append(filled, '#');
    bar.append(width - filled, '.');
    bar += "]";
    ostringstream oss;
    oss << label << " " << bar << " " << current << "/" << total;
    return oss.str();
}

void print_progress_line(const string& label, int current, int total, bool done = false) {
    cout << '\r' << render_progress_bar(label, current, total) << flush;
    if (done) {
        cout << '\n';
    }
}

int run_generator_cli(int argc, char* argv[]) {
    DemoPreset demo_preset;
    bool demo_mode_active = false;

    if (argc >= 2 && string(argv[1]) == "--demo") {
        if (argc > 3) {
            cerr << "Usage:\n";
            cerr << argv[0] << " --demo [easy|mid|hard|large]\n";
            return 1;
        }

        DemoMode mode = DemoMode::EASY;
        if (argc == 3) {
            if (!parse_demo_mode(argv[2], mode)) {
                cerr << "Unknown demo mode: " << argv[2] << "\n";
                cerr << "Available modes: easy, mid, hard, large\n";
                return 1;
            }
        }

        string error;
        if (!load_demo_preset(demo_mode_name(mode), demo_preset, error)) {
            cerr << "Failed to load demo config: " << error << "\n";
            return 1;
        }
        demo_mode_active = true;
        std::error_code ec;
        filesystem::create_directories("demo", ec);
        if (ec) {
            cerr << "Failed to create demo directory: " << ec.message() << "\n";
            return 1;
        }
        cout << "Generator demo preset: mode=" << demo_preset.mode_name << "\n";
    }

    if (argc < 7) {
        if (!demo_mode_active) {
            cerr << "Usage:\n";
            cerr << argv[0] << " <output.txt> <gridW> <gridH> <numComponents> <numNets> <seed>\n";
            cerr << argv[0] << " --demo [easy|mid|hard|large]\n";
            cerr << "Example:\n";
            cerr << argv[0] << " case1.txt 20 20 12 18 12345\n";
            return 1;
        }
    }

    string outFile;
    int gridW = 0;
    int gridH = 0;
    int numComponents = 0;
    int numNets = 0;
    unsigned seed = 0;
    int fixedChancePct = 20;
    int pinMin = 1;
    int pinMax = 3;
    int netDegreeMin = 2;
    int netDegreeMax = 4;
    vector<pair<int, int>> sizeChoices = {{1, 1}, {2, 1}, {1, 2}, {2, 2}};

    if (demo_mode_active) {
        outFile = demo_preset.generator.output_path;
        gridW = demo_preset.generator.grid_w;
        gridH = demo_preset.generator.grid_h;
        numComponents = demo_preset.generator.num_components;
        numNets = demo_preset.generator.num_nets;
        seed = demo_preset.generator.seed;
        fixedChancePct = demo_preset.generator.fixed_chance_pct;
        pinMin = demo_preset.generator.pin_min;
        pinMax = demo_preset.generator.pin_max;
        netDegreeMin = demo_preset.generator.net_degree_min;
        netDegreeMax = demo_preset.generator.net_degree_max;
        sizeChoices = demo_preset.generator.size_choices;
    } else {
        outFile = argv[1];
        gridW = stoi(argv[2]);
        gridH = stoi(argv[3]);
        numComponents = stoi(argv[4]);
        numNets = stoi(argv[5]);
        seed = static_cast<unsigned>(stoul(argv[6]));
    }

    if (gridW <= 0 || gridH <= 0 || numComponents <= 0 || numNets <= 0) {
        cerr << "All numeric arguments must be positive.\n";
        return 1;
    }

    mt19937 rng(seed);

    uniform_int_distribution<int> sizeDist(0, (int)sizeChoices.size() - 1);
    uniform_int_distribution<int> fixedChanceDist(0, 99);
    uniform_int_distribution<int> pinCountDist(pinMin, pinMax);
    uniform_int_distribution<int> netDegreeDist(netDegreeMin, netDegreeMax);

    vector<GenComponent> comps;
    comps.reserve(numComponents);

    // 1) Generate components
    for (int i = 0; i < numComponents; ++i) {
        GenComponent c;
        c.id = "U" + to_string(i);

        auto [w, h] = sizeChoices[sizeDist(rng)];
        c.w = w;
        c.h = h;

        // Demo presets control how congested the benchmark is.
        c.fixed = (fixedChanceDist(rng) < fixedChancePct);

        comps.push_back(c);

        if ((i + 1) % max(1, numComponents / 16) == 0 || i + 1 == numComponents) {
            print_progress_line("components", i + 1, numComponents, i + 1 == numComponents);
        }
    }

    // 2) Assign fixed positions legally
    int fixed_total = 0;
    for (const auto& c : comps) {
        if (c.fixed) {
            ++fixed_total;
        }
    }
    int fixed_done = 0;
    for (int i = 0; i < numComponents; ++i) {
        if (!comps[i].fixed) continue;

        bool placed = false;
        vector<pair<int,int>> candidates;
        for (int y = 0; y <= gridH - comps[i].h; ++y) {
            for (int x = 0; x <= gridW - comps[i].w; ++x) {
                if (canPlaceFixed(comps, i, x, y, gridW, gridH)) {
                    candidates.push_back({x, y});
                }
            }
        }

        if (candidates.empty()) {
            // fallback: make it movable if no legal fixed spot exists
            comps[i].fixed = false;
            continue;
        }

        uniform_int_distribution<int> candDist(0, (int)candidates.size() - 1);
        auto [px, py] = candidates[candDist(rng)];
        comps[i].x = px;
        comps[i].y = py;
        placed = true;

        if (!placed) {
            comps[i].fixed = false;
        }

        ++fixed_done;
        print_progress_line("fixed", fixed_done, fixed_total, fixed_done == fixed_total);
    }

    // 3) Generate pins for each component
    for (int i = 0; i < static_cast<int>(comps.size()); ++i) {
        auto& c = comps[i];
        int pinCount = pinCountDist(rng);
        int maxPins = c.w * c.h;
        pinCount = min(pinCount, maxPins);

        vector<pair<int,int>> allSites;
        for (int dy = 0; dy < c.h; ++dy) {
            for (int dx = 0; dx < c.w; ++dx) {
                allSites.push_back({dx, dy});
            }
        }

        shuffle(allSites.begin(), allSites.end(), rng);

        for (int p = 0; p < pinCount; ++p) {
            GenPin pin;
            pin.name = "P" + to_string(p);
            pin.dx = allSites[p].first;
            pin.dy = allSites[p].second;
            c.pins.push_back(pin);
        }

        if ((i + 1) % max(1, numComponents / 16) == 0 || i + 1 == numComponents) {
            print_progress_line("pins", i + 1, numComponents, i + 1 == numComponents);
        }
    }

    // 4) Build a flat list of all available pins
    vector<pair<int,int>> allPinRefs; // (compIdx, pinIdx)
    for (int ci = 0; ci < (int)comps.size(); ++ci) {
        for (int pi = 0; pi < (int)comps[ci].pins.size(); ++pi) {
            allPinRefs.push_back({ci, pi});
        }
    }

    if ((int)allPinRefs.size() < 2) {
        cerr << "Not enough pins to form nets.\n";
        return 1;
    }

    // 5) Generate nets
    vector<GenNet> nets;
    nets.reserve(numNets);

    for (int ni = 0; ni < numNets; ++ni) {
        GenNet net;
        net.id = "N" + to_string(ni);

        int degree = netDegreeDist(rng);
        degree = min(degree, numComponents);
        degree = max(degree, 2);

        // Pick distinct components first
        vector<int> compIndices(numComponents);
        for (int i = 0; i < numComponents; ++i) compIndices[i] = i;
        shuffle(compIndices.begin(), compIndices.end(), rng);
        compIndices.resize(degree);

        for (int ci : compIndices) {
            uniform_int_distribution<int> pinDist(0, (int)comps[ci].pins.size() - 1);
            int pi = pinDist(rng);
            net.refs.push_back({ci, pi});
        }

        nets.push_back(net);

        if ((ni + 1) % max(1, numNets / 16) == 0 || ni + 1 == numNets) {
            print_progress_line("nets", ni + 1, numNets, ni + 1 == numNets);
        }
    }

    // 6) Write file
    print_progress_line("write", 1, 1, true);
    ofstream fout(outFile);
    if (!fout) {
        cerr << "Cannot open output file: " << outFile << "\n";
        return 1;
    }

    fout << "GRID " << gridW << " " << gridH << "\n\n";

    fout << "COMPONENTS " << comps.size() << "\n";
    for (const auto& c : comps) {
        fout << "COMPONENT " << c.id << " " << c.w << " " << c.h << " ";
        if (c.fixed) {
            fout << "fixed " << c.x << " " << c.y << "\n";
        } else {
            fout << "movable\n";
        }
    }

    fout << "\nPINS ";
    int totalPins = 0;
    for (const auto& c : comps) totalPins += (int)c.pins.size();
    fout << totalPins << "\n";

    for (const auto& c : comps) {
        for (const auto& p : c.pins) {
            fout << "PIN " << c.id << " " << p.name << " " << p.dx << " " << p.dy << "\n";
        }
    }

    fout << "\nNETS " << nets.size() << "\n";
    for (const auto& n : nets) {
        fout << "NET " << n.id << " " << n.refs.size();
        for (auto [ci, pi] : n.refs) {
            fout << " " << comps[ci].id << "." << comps[ci].pins[pi].name;
        }
        fout << "\n";
    }

    fout.close();

    cout << "Generated input file: " << outFile << "\n";
    cout << "Grid: " << gridW << " x " << gridH << "\n";
    cout << "Components: " << comps.size() << "\n";
    cout << "Nets: " << nets.size() << "\n";
    cout << "Pins: " << totalPins << "\n";

    return 0;
}