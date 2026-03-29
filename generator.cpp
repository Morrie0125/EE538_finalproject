#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <random>
#include <unordered_set>
#include <algorithm>

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

int main(int argc, char* argv[]) {
    if (argc < 7) {
        cerr << "Usage:\n";
        cerr << argv[0] << " <output.txt> <gridW> <gridH> <numComponents> <numNets> <seed>\n";
        cerr << "Example:\n";
        cerr << argv[0] << " case1.txt 20 20 12 18 12345\n";
        return 1;
    }

    string outFile = argv[1];
    int gridW = stoi(argv[2]);
    int gridH = stoi(argv[3]);
    int numComponents = stoi(argv[4]);
    int numNets = stoi(argv[5]);
    unsigned seed = static_cast<unsigned>(stoul(argv[6]));

    if (gridW <= 0 || gridH <= 0 || numComponents <= 0 || numNets <= 0) {
        cerr << "All numeric arguments must be positive.\n";
        return 1;
    }

    mt19937 rng(seed);

    vector<pair<int,int>> sizeChoices = {
        {1,1}, {2,1}, {1,2}, {2,2}
    };

    uniform_int_distribution<int> sizeDist(0, (int)sizeChoices.size() - 1);
    uniform_int_distribution<int> fixedChanceDist(0, 99);
    uniform_int_distribution<int> pinCountDist(1, 3);
    uniform_int_distribution<int> netDegreeDist(2, 4);

    vector<GenComponent> comps;
    comps.reserve(numComponents);

    // 1) Generate components
    for (int i = 0; i < numComponents; ++i) {
        GenComponent c;
        c.id = "U" + to_string(i);

        auto [w, h] = sizeChoices[sizeDist(rng)];
        c.w = w;
        c.h = h;

        // Around 20% fixed
        c.fixed = (fixedChanceDist(rng) < 20);

        comps.push_back(c);
    }

    // 2) Assign fixed positions legally
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
    }

    // 3) Generate pins for each component
    for (auto& c : comps) {
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
    }

    // 6) Write file
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