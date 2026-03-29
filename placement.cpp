#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <random>
#include <algorithm>
#include <limits>

using namespace std;

struct Pin {
    string name;
    int dx = 0;
    int dy = 0;
};

struct Component {
    string id;
    int w = 1;
    int h = 1;
    bool fixed = false;
    int x = -1;   // lower-left corner
    int y = -1;
    vector<Pin> pins;
    unordered_map<string, int> pinNameToIdx;
};

struct NetPinRef {
    int compIdx = -1;
    int pinIdx = -1;
};

struct Net {
    string id;
    vector<NetPinRef> pins;
};

class PlacementDB {
public:
    int gridW = 0;
    int gridH = 0;

    vector<Component> comps;
    vector<Net> nets;

    unordered_map<string, int> compNameToIdx;
    unordered_map<string, int> netNameToIdx;

    void parseFile(const string& filename) {
        ifstream fin(filename);
        if (!fin) {
            throw runtime_error("Cannot open input file: " + filename);
        }

        vector<string> lines;
        string line;
        while (getline(fin, line)) {
            auto sharpPos = line.find('#');
            if (sharpPos != string::npos) {
                line = line.substr(0, sharpPos);
            }
            if (!trim(line).empty()) {
                lines.push_back(trim(line));
            }
        }

        size_t i = 0;
        parseGrid(lines, i);
        parseComponents(lines, i);
        parsePins(lines, i);
        parseNets(lines, i);

        validateAll();
    }

    long long totalHPWL() const {
        long long total = 0;
        for (const auto& net : nets) {
            total += netHPWL(net);
        }
        return total;
    }

    void randomLegalPlacement(unsigned seed = 12345) {
        // Place fixed components first
        for (const auto& c : comps) {
            if (c.fixed) {
                if (!insideBoundary(c.x, c.y, c.w, c.h)) {
                    throw runtime_error("Fixed component " + c.id + " is out of boundary.");
                }
            }
        }

        vector<vector<int>> occ(gridH, vector<int>(gridW, -1));

        // Mark fixed components
        for (int ci = 0; ci < (int)comps.size(); ++ci) {
            const auto& c = comps[ci];
            if (c.fixed) {
                if (!canPlaceAt(ci, c.x, c.y, occ)) {
                    throw runtime_error("Fixed component overlap detected: " + c.id);
                }
                stampComponent(ci, c.x, c.y, occ);
            }
        }

        mt19937 rng(seed);

        // Place movable components randomly
        for (int ci = 0; ci < (int)comps.size(); ++ci) {
            auto& c = comps[ci];
            if (c.fixed) continue;

            vector<pair<int,int>> candidates;
            for (int y = 0; y <= gridH - c.h; ++y) {
                for (int x = 0; x <= gridW - c.w; ++x) {
                    if (canPlaceAt(ci, x, y, occ)) {
                        candidates.push_back({x, y});
                    }
                }
            }

            if (candidates.empty()) {
                throw runtime_error("No legal position available for component: " + c.id);
            }

            uniform_int_distribution<int> dist(0, (int)candidates.size() - 1);
            auto [px, py] = candidates[dist(rng)];
            c.x = px;
            c.y = py;
            stampComponent(ci, px, py, occ);
        }
    }

    void printPlacement() const {
        cout << "Placement Result\n";
        cout << "Grid: " << gridW << " x " << gridH << "\n";
        for (const auto& c : comps) {
            cout << "COMP " << c.id
                 << " size(" << c.w << "x" << c.h << ") "
                 << (c.fixed ? "fixed" : "movable")
                 << " at (" << c.x << ", " << c.y << ")\n";
        }
    }
    void writePlacementFile(const string& filename) const {
    ofstream fout(filename);
    if (!fout) {
        throw runtime_error("Cannot open output placement file: " + filename);
    }

    fout << "GRID " << gridW << " " << gridH << "\n\n";

    fout << "COMPONENTS " << comps.size() << "\n";
    for (const auto& c : comps) {
        fout << "COMPONENT " << c.id << " "
             << c.w << " " << c.h << " "
             << (c.fixed ? "fixed" : "movable") << " "
             << c.x << " " << c.y << "\n";
    }

    int totalPins = 0;
    for (const auto& c : comps) totalPins += (int)c.pins.size();

    fout << "\nPINS " << totalPins << "\n";
    for (const auto& c : comps) {
        for (const auto& p : c.pins) {
            fout << "PIN " << c.id << " " << p.name << " "
                 << p.dx << " " << p.dy << "\n";
        }
    }

    fout << "\nNETS " << nets.size() << "\n";
    for (const auto& n : nets) {
        fout << "NET " << n.id << " " << n.pins.size();
        for (const auto& ref : n.pins) {
            const auto& c = comps[ref.compIdx];
            const auto& p = c.pins[ref.pinIdx];
            fout << " " << c.id << "." << p.name;
        }
        fout << "\n";
    }
}
private:
    static string trim(const string& s) {
        size_t l = s.find_first_not_of(" \t\r\n");
        if (l == string::npos) return "";
        size_t r = s.find_last_not_of(" \t\r\n");
        return s.substr(l, r - l + 1);
    }

    static vector<string> split(const string& s) {
        vector<string> tokens;
        string tok;
        istringstream iss(s);
        while (iss >> tok) tokens.push_back(tok);
        return tokens;
    }

    void parseGrid(const vector<string>& lines, size_t& i) {
        if (i >= lines.size()) throw runtime_error("Missing GRID line.");
        auto tk = split(lines[i]);
        if (tk.size() != 3 || tk[0] != "GRID") {
            throw runtime_error("Expected: GRID <W> <H>");
        }
        gridW = stoi(tk[1]);
        gridH = stoi(tk[2]);
        if (gridW <= 0 || gridH <= 0) {
            throw runtime_error("Grid size must be positive.");
        }
        ++i;
    }

    void parseComponents(const vector<string>& lines, size_t& i) {
        if (i >= lines.size()) throw runtime_error("Missing COMPONENTS line.");
        auto tk = split(lines[i]);
        if (tk.size() != 2 || tk[0] != "COMPONENTS") {
            throw runtime_error("Expected: COMPONENTS <N>");
        }
        int n = stoi(tk[1]);
        if (n < 0) throw runtime_error("COMPONENTS count cannot be negative.");
        ++i;

        for (int k = 0; k < n; ++k, ++i) {
            if (i >= lines.size()) throw runtime_error("Unexpected EOF while reading COMPONENT lines.");
            auto t = split(lines[i]);

            // COMPONENT <id> <w> <h> <movable|fixed> [x y]
            if (t.size() != 5 && t.size() != 7) {
                throw runtime_error("Invalid COMPONENT line: " + lines[i]);
            }
            if (t[0] != "COMPONENT") {
                throw runtime_error("Expected COMPONENT line: " + lines[i]);
            }

            Component c;
            c.id = t[1];
            c.w = stoi(t[2]);
            c.h = stoi(t[3]);

            if (c.w <= 0 || c.h <= 0) {
                throw runtime_error("Component size must be positive: " + c.id);
            }

            if (t[4] == "movable") {
                c.fixed = false;
                if (t.size() != 5) {
                    throw runtime_error("Movable component should not have fixed coordinates: " + c.id);
                }
            } else if (t[4] == "fixed") {
                c.fixed = true;
                if (t.size() != 7) {
                    throw runtime_error("Fixed component must have coordinates: " + c.id);
                }
                c.x = stoi(t[5]);
                c.y = stoi(t[6]);
            } else {
                throw runtime_error("Component type must be movable or fixed: " + c.id);
            }

            if (compNameToIdx.count(c.id)) {
                throw runtime_error("Duplicate component ID: " + c.id);
            }

            compNameToIdx[c.id] = (int)comps.size();
            comps.push_back(c);
        }
    }

    void parsePins(const vector<string>& lines, size_t& i) {
        if (i >= lines.size()) throw runtime_error("Missing PINS line.");
        auto tk = split(lines[i]);
        if (tk.size() != 2 || tk[0] != "PINS") {
            throw runtime_error("Expected: PINS <P>");
        }
        int p = stoi(tk[1]);
        if (p < 0) throw runtime_error("PINS count cannot be negative.");
        ++i;

        for (int k = 0; k < p; ++k, ++i) {
            if (i >= lines.size()) throw runtime_error("Unexpected EOF while reading PIN lines.");
            auto t = split(lines[i]);

            // PIN <comp_id> <pin_name> <dx> <dy>
            if (t.size() != 5 || t[0] != "PIN") {
                throw runtime_error("Invalid PIN line: " + lines[i]);
            }

            string compId = t[1];
            string pinName = t[2];
            int dx = stoi(t[3]);
            int dy = stoi(t[4]);

            auto it = compNameToIdx.find(compId);
            if (it == compNameToIdx.end()) {
                throw runtime_error("PIN refers to unknown component: " + compId);
            }

            int ci = it->second;
            auto& c = comps[ci];

            if (c.pinNameToIdx.count(pinName)) {
                throw runtime_error("Duplicate pin name " + pinName + " on component " + compId);
            }

            if (dx < 0 || dx >= c.w || dy < 0 || dy >= c.h) {
                throw runtime_error("Pin offset out of component bounds: " + compId + "." + pinName);
            }

            Pin pin;
            pin.name = pinName;
            pin.dx = dx;
            pin.dy = dy;

            c.pinNameToIdx[pinName] = (int)c.pins.size();
            c.pins.push_back(pin);
        }
    }

    void parseNets(const vector<string>& lines, size_t& i) {
        if (i >= lines.size()) throw runtime_error("Missing NETS line.");
        auto tk = split(lines[i]);
        if (tk.size() != 2 || tk[0] != "NETS") {
            throw runtime_error("Expected: NETS <M>");
        }
        int m = stoi(tk[1]);
        if (m < 0) throw runtime_error("NETS count cannot be negative.");
        ++i;

        for (int k = 0; k < m; ++k, ++i) {
            if (i >= lines.size()) throw runtime_error("Unexpected EOF while reading NET lines.");
            auto t = split(lines[i]);

            // NET <net_id> <degree> <comp.pin> <comp.pin> ...
            if (t.size() < 4 || t[0] != "NET") {
                throw runtime_error("Invalid NET line: " + lines[i]);
            }

            string netId = t[1];
            int degree = stoi(t[2]);

            if ((int)t.size() != 3 + degree) {
                throw runtime_error("NET degree mismatch in line: " + lines[i]);
            }
            if (degree < 2) {
                throw runtime_error("NET must connect at least 2 pins: " + netId);
            }
            if (netNameToIdx.count(netId)) {
                throw runtime_error("Duplicate net ID: " + netId);
            }

            Net net;
            net.id = netId;

            for (int j = 0; j < degree; ++j) {
                auto ref = parseCompPinRef(t[3 + j]);
                net.pins.push_back(ref);
            }

            netNameToIdx[netId] = (int)nets.size();
            nets.push_back(net);
        }
    }

    NetPinRef parseCompPinRef(const string& s) {
        auto dotPos = s.find('.');
        if (dotPos == string::npos) {
            throw runtime_error("Pin reference must be comp.pin : " + s);
        }

        string compId = s.substr(0, dotPos);
        string pinName = s.substr(dotPos + 1);

        auto cit = compNameToIdx.find(compId);
        if (cit == compNameToIdx.end()) {
            throw runtime_error("Unknown component in net: " + compId);
        }

        int ci = cit->second;
        auto pit = comps[ci].pinNameToIdx.find(pinName);
        if (pit == comps[ci].pinNameToIdx.end()) {
            throw runtime_error("Unknown pin in net: " + s);
        }

        return {ci, pit->second};
    }

    void validateAll() const {
        if (gridW <= 0 || gridH <= 0) {
            throw runtime_error("Invalid grid size.");
        }

        for (const auto& c : comps) {
            if (c.pins.empty()) {
                cerr << "Warning: component " << c.id << " has no pins.\n";
            }
        }

        // Validate fixed placements
        vector<vector<int>> occ(gridH, vector<int>(gridW, -1));
        for (int ci = 0; ci < (int)comps.size(); ++ci) {
            const auto& c = comps[ci];
            if (!c.fixed) continue;

            if (!insideBoundary(c.x, c.y, c.w, c.h)) {
                throw runtime_error("Fixed component out of boundary: " + c.id);
            }
            if (!canPlaceAt(ci, c.x, c.y, occ)) {
                throw runtime_error("Fixed component overlap: " + c.id);
            }
            stampComponent(ci, c.x, c.y, occ);
        }
    }

    bool insideBoundary(int x, int y, int w, int h) const {
        return x >= 0 && y >= 0 && x + w <= gridW && y + h <= gridH;
    }

    bool canPlaceAt(int compIdx, int x, int y, const vector<vector<int>>& occ) const {
        const auto& c = comps[compIdx];
        if (!insideBoundary(x, y, c.w, c.h)) return false;

        for (int yy = y; yy < y + c.h; ++yy) {
            for (int xx = x; xx < x + c.w; ++xx) {
                if (occ[yy][xx] != -1) return false;
            }
        }
        return true;
    }

    void stampComponent(int compIdx, int x, int y, vector<vector<int>>& occ) const {
        const auto& c = comps[compIdx];
        for (int yy = y; yy < y + c.h; ++yy) {
            for (int xx = x; xx < x + c.w; ++xx) {
                occ[yy][xx] = compIdx;
            }
        }
    }

    pair<int,int> getAbsolutePinPos(int compIdx, int pinIdx) const {
        const auto& c = comps[compIdx];
        const auto& p = c.pins[pinIdx];

        if (c.x < 0 || c.y < 0) {
            throw runtime_error("Component not placed yet: " + c.id);
        }

        return {c.x + p.dx, c.y + p.dy};
    }

    long long netHPWL(const Net& net) const {
        int minX = numeric_limits<int>::max();
        int maxX = numeric_limits<int>::min();
        int minY = numeric_limits<int>::max();
        int maxY = numeric_limits<int>::min();

        for (const auto& ref : net.pins) {
            auto [ax, ay] = getAbsolutePinPos(ref.compIdx, ref.pinIdx);
            minX = min(minX, ax);
            maxX = max(maxX, ax);
            minY = min(minY, ay);
            maxY = max(maxY, ay);
        }

        return (long long)(maxX - minX) + (long long)(maxY - minY);
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <input_file>\n";
        return 1;
    }

    try {
        PlacementDB db;
        db.parseFile(argv[1]);

        db.randomLegalPlacement(12345);

        db.printPlacement();
        cout << "Total HPWL = " << db.totalHPWL() << "\n";
        db.writePlacementFile("placement_out.txt");
    }
    catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}