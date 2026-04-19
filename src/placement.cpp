#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "../include/commands.h"
#include "../include/placement_engine.h"

using namespace std;

namespace {

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

}  // namespace

PlacementDB::PlacementDB()
    : comps(nodes),
      compNameToIdx(nodeNameToIdx) {}

void PlacementDB::parseFile(const string& filename) {
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

long long PlacementDB::totalHPWL() const {
    long long total = 0;
    for (const auto& net : nets) {
        total += netHPWL(net);
    }
    return total;
}

void PlacementDB::randomLegalPlacement(unsigned seed) {
    for (const auto& c : comps) {
        if (c.fixed) {
            if (!insideBoundary(c.x, c.y, c.w, c.h)) {
                throw runtime_error("Fixed component " + c.id + " is out of boundary.");
            }
        }
    }

    occ.assign(gridH, vector<int>(gridW, -1));

    for (int ci = 0; ci < static_cast<int>(comps.size()); ++ci) {
        const auto& c = comps[ci];
        if (!c.fixed) {
            continue;
        }
        if (!canPlaceAt(ci, c.x, c.y, occ)) {
            throw runtime_error("Fixed component overlap detected: " + c.id);
        }
        stampComponent(ci, c.x, c.y, occ);
    }

    mt19937 rng(seed);

    for (int ci = 0; ci < static_cast<int>(comps.size()); ++ci) {
        auto& c = comps[ci];
        if (c.fixed) {
            continue;
        }

        vector<pair<int, int>> candidates;
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

        uniform_int_distribution<int> dist(0, static_cast<int>(candidates.size()) - 1);
        const auto [px, py] = candidates[dist(rng)];
        c.x = px;
        c.y = py;
        stampComponent(ci, px, py, occ);
    }
}

bool PlacementDB::moveComponent(int compIdx, int newX, int newY) {
    if (compIdx < 0 || compIdx >= static_cast<int>(comps.size())) {
        return false;
    }

    auto& c = comps[compIdx];
    if (c.fixed) {
        return false;
    }
    if (c.x < 0 || c.y < 0) {
        return false;
    }

    const int oldX = c.x;
    const int oldY = c.y;

    unstampComponent(compIdx, oldX, oldY);
    if (!canPlaceAtCurrentOcc(compIdx, newX, newY)) {
        stampComponent(compIdx, oldX, oldY, occ);
        return false;
    }

    c.x = newX;
    c.y = newY;
    stampComponent(compIdx, newX, newY, occ);

    MoveRecord rec;
    rec.type = PlacementMoveType::RELOCATE;
    rec.compA = compIdx;
    rec.oldAx = oldX;
    rec.oldAy = oldY;
    rec.newAx = newX;
    rec.newAy = newY;
    moveHistory.push_back(rec);
    return true;
}

bool PlacementDB::swapComponents(int a, int b) {
    if (a == b) {
        return false;
    }
    if (a < 0 || a >= static_cast<int>(comps.size())) {
        return false;
    }
    if (b < 0 || b >= static_cast<int>(comps.size())) {
        return false;
    }
    if (comps[a].fixed || comps[b].fixed) {
        return false;
    }

    auto& ca = comps[a];
    auto& cb = comps[b];

    if (ca.x < 0 || ca.y < 0 || cb.x < 0 || cb.y < 0) {
        return false;
    }

    const int ax = ca.x;
    const int ay = ca.y;
    const int bx = cb.x;
    const int by = cb.y;

    unstampComponent(a, ax, ay);
    unstampComponent(b, bx, by);

    const bool okA = canPlaceAtCurrentOcc(a, bx, by);
    const bool okB = canPlaceAtCurrentOcc(b, ax, ay);

    if (!okA || !okB) {
        stampComponent(a, ax, ay, occ);
        stampComponent(b, bx, by, occ);
        return false;
    }

    ca.x = bx;
    ca.y = by;
    cb.x = ax;
    cb.y = ay;

    stampComponent(a, ca.x, ca.y, occ);
    stampComponent(b, cb.x, cb.y, occ);

    MoveRecord rec;
    rec.type = PlacementMoveType::SWAP;
    rec.compA = a;
    rec.compB = b;
    rec.oldAx = ax;
    rec.oldAy = ay;
    rec.oldBx = bx;
    rec.oldBy = by;
    rec.newAx = bx;
    rec.newAy = by;
    rec.newBx = ax;
    rec.newBy = ay;
    moveHistory.push_back(rec);
    return true;
}

bool PlacementDB::restoreLastMove() {
    if (moveHistory.empty()) {
        return false;
    }

    const MoveRecord rec = moveHistory.back();
    moveHistory.pop_back();

    if (rec.type == PlacementMoveType::RELOCATE) {
        const int a = rec.compA;
        if (a < 0 || a >= static_cast<int>(comps.size())) {
            return false;
        }

        auto& ca = comps[a];
        unstampComponent(a, ca.x, ca.y);
        ca.x = rec.oldAx;
        ca.y = rec.oldAy;
        stampComponent(a, ca.x, ca.y, occ);
        return true;
    }

    if (rec.type == PlacementMoveType::SWAP) {
        const int a = rec.compA;
        const int b = rec.compB;
        if (a < 0 || a >= static_cast<int>(comps.size())) {
            return false;
        }
        if (b < 0 || b >= static_cast<int>(comps.size())) {
            return false;
        }

        auto& ca = comps[a];
        auto& cb = comps[b];

        unstampComponent(a, ca.x, ca.y);
        unstampComponent(b, cb.x, cb.y);

        ca.x = rec.oldAx;
        ca.y = rec.oldAy;
        cb.x = rec.oldBx;
        cb.y = rec.oldBy;

        stampComponent(a, ca.x, ca.y, occ);
        stampComponent(b, cb.x, cb.y, occ);
        return true;
    }

    return false;
}

size_t PlacementDB::checkpoint() const {
    return moveHistory.size();
}

bool PlacementDB::rollbackTo(size_t cp) {
    if (cp > moveHistory.size()) {
        return false;
    }

    while (moveHistory.size() > cp) {
        if (!restoreLastMove()) {
            return false;
        }
    }
    return true;
}

void PlacementDB::commitMoves() {
    moveHistory.clear();
}

size_t PlacementDB::historySize() const {
    return moveHistory.size();
}

bool PlacementDB::isPlacementLegal() const {
    vector<vector<int>> occ_local(gridH, vector<int>(gridW, -1));

    for (int ci = 0; ci < static_cast<int>(comps.size()); ++ci) {
        const auto& c = comps[ci];
        if (c.x < 0 || c.y < 0) {
            return false;
        }
        if (!insideBoundary(c.x, c.y, c.w, c.h)) {
            return false;
        }

        for (int yy = c.y; yy < c.y + c.h; ++yy) {
            for (int xx = c.x; xx < c.x + c.w; ++xx) {
                if (occ_local[yy][xx] != -1) {
                    return false;
                }
                occ_local[yy][xx] = ci;
            }
        }
    }
    return true;
}

void PlacementDB::printPlacement() const {
    cout << "Placement Result\n";
    cout << "Grid: " << gridW << " x " << gridH << "\n";
    for (const auto& c : comps) {
        cout << "COMP " << c.id
             << " size(" << c.w << "x" << c.h << ") "
             << (c.fixed ? "fixed" : "movable")
             << " at (" << c.x << ", " << c.y << ")\n";
    }
}

void PlacementDB::writePlacementFile(const string& filename) const {
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
    for (const auto& c : comps) {
        totalPins += static_cast<int>(c.pins.size());
    }

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
            const auto& c = comps[ref.nodeIdx];
            const auto& p = c.pins[ref.pinIdx];
            fout << " " << c.id << "." << p.name;
        }
        fout << "\n";
    }
}

string PlacementDB::trim(const string& s) {
    size_t l = s.find_first_not_of(" \t\r\n");
    if (l == string::npos) {
        return "";
    }
    size_t r = s.find_last_not_of(" \t\r\n");
    return s.substr(l, r - l + 1);
}

vector<string> PlacementDB::split(const string& s) {
    vector<string> tokens;
    string tok;
    istringstream iss(s);
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

void PlacementDB::parseGrid(const vector<string>& lines, size_t& i) {
    if (i >= lines.size()) {
        throw runtime_error("Missing GRID line.");
    }
    const auto tk = split(lines[i]);
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

void PlacementDB::parseComponents(const vector<string>& lines, size_t& i) {
    if (i >= lines.size()) {
        throw runtime_error("Missing COMPONENTS line.");
    }
    const auto tk = split(lines[i]);
    if (tk.size() != 2 || tk[0] != "COMPONENTS") {
        throw runtime_error("Expected: COMPONENTS <N>");
    }
    const int n = stoi(tk[1]);
    if (n < 0) {
        throw runtime_error("COMPONENTS count cannot be negative.");
    }
    ++i;

    for (int k = 0; k < n; ++k, ++i) {
        if (i >= lines.size()) {
            throw runtime_error("Unexpected EOF while reading COMPONENT lines.");
        }
        const auto t = split(lines[i]);

        if (t.size() != 5 && t.size() != 7) {
            throw runtime_error("Invalid COMPONENT line: " + lines[i]);
        }
        if (t[0] != "COMPONENT") {
            throw runtime_error("Expected COMPONENT line: " + lines[i]);
        }

        Node c;
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

        if (compNameToIdx.count(c.id) != 0) {
            throw runtime_error("Duplicate component ID: " + c.id);
        }

        compNameToIdx[c.id] = static_cast<int>(comps.size());
        comps.push_back(c);
    }
}

void PlacementDB::parsePins(const vector<string>& lines, size_t& i) {
    if (i >= lines.size()) {
        throw runtime_error("Missing PINS line.");
    }
    const auto tk = split(lines[i]);
    if (tk.size() != 2 || tk[0] != "PINS") {
        throw runtime_error("Expected: PINS <P>");
    }
    const int p = stoi(tk[1]);
    if (p < 0) {
        throw runtime_error("PINS count cannot be negative.");
    }
    ++i;

    for (int k = 0; k < p; ++k, ++i) {
        if (i >= lines.size()) {
            throw runtime_error("Unexpected EOF while reading PIN lines.");
        }
        const auto t = split(lines[i]);

        if (t.size() != 5 || t[0] != "PIN") {
            throw runtime_error("Invalid PIN line: " + lines[i]);
        }

        const string compId = t[1];
        const string pinName = t[2];
        const int dx = stoi(t[3]);
        const int dy = stoi(t[4]);

        const auto it = compNameToIdx.find(compId);
        if (it == compNameToIdx.end()) {
            throw runtime_error("PIN refers to unknown component: " + compId);
        }

        const int ci = it->second;
        auto& c = comps[ci];

        if (c.pinNameToIdx.count(pinName) != 0) {
            throw runtime_error("Duplicate pin name " + pinName + " on component " + compId);
        }

        if (dx < 0 || dx >= c.w || dy < 0 || dy >= c.h) {
            throw runtime_error("Pin offset out of component bounds: " + compId + "." + pinName);
        }

        Pin pin;
        pin.name = pinName;
        pin.dx = dx;
        pin.dy = dy;

        c.pinNameToIdx[pinName] = static_cast<int>(c.pins.size());
        c.pins.push_back(pin);
    }
}

void PlacementDB::parseNets(const vector<string>& lines, size_t& i) {
    if (i >= lines.size()) {
        throw runtime_error("Missing NETS line.");
    }
    const auto tk = split(lines[i]);
    if (tk.size() != 2 || tk[0] != "NETS") {
        throw runtime_error("Expected: NETS <M>");
    }
    const int m = stoi(tk[1]);
    if (m < 0) {
        throw runtime_error("NETS count cannot be negative.");
    }
    ++i;

    for (int k = 0; k < m; ++k, ++i) {
        if (i >= lines.size()) {
            throw runtime_error("Unexpected EOF while reading NET lines.");
        }
        const auto t = split(lines[i]);

        if (t.size() < 4 || t[0] != "NET") {
            throw runtime_error("Invalid NET line: " + lines[i]);
        }

        const string netId = t[1];
        const int degree = stoi(t[2]);

        if (static_cast<int>(t.size()) != 3 + degree) {
            throw runtime_error("NET degree mismatch in line: " + lines[i]);
        }
        if (degree < 2) {
            throw runtime_error("NET must connect at least 2 pins: " + netId);
        }
        if (netNameToIdx.count(netId) != 0) {
            throw runtime_error("Duplicate net ID: " + netId);
        }

        Net net;
        net.id = netId;

        for (int j = 0; j < degree; ++j) {
            net.pins.push_back(parseCompPinRef(t[3 + j]));
        }

        netNameToIdx[netId] = static_cast<int>(nets.size());
        nets.push_back(net);
    }
}

NetPinRef PlacementDB::parseCompPinRef(const string& s) {
    const auto dotPos = s.find('.');
    if (dotPos == string::npos) {
        throw runtime_error("Pin reference must be comp.pin : " + s);
    }

    const string compId = s.substr(0, dotPos);
    const string pinName = s.substr(dotPos + 1);

    const auto cit = compNameToIdx.find(compId);
    if (cit == compNameToIdx.end()) {
        throw runtime_error("Unknown component in net: " + compId);
    }

    const int ci = cit->second;
    const auto pit = comps[ci].pinNameToIdx.find(pinName);
    if (pit == comps[ci].pinNameToIdx.end()) {
        throw runtime_error("Unknown pin in net: " + s);
    }

    return {ci, pit->second};
}

void PlacementDB::validateAll() const {
    if (gridW <= 0 || gridH <= 0) {
        throw runtime_error("Invalid grid size.");
    }

    for (const auto& c : comps) {
        if (c.pins.empty()) {
            cerr << "Warning: component " << c.id << " has no pins.\n";
        }
    }

    vector<vector<int>> occ_local(gridH, vector<int>(gridW, -1));
    for (int ci = 0; ci < static_cast<int>(comps.size()); ++ci) {
        const auto& c = comps[ci];
        if (!c.fixed) {
            continue;
        }

        if (!insideBoundary(c.x, c.y, c.w, c.h)) {
            throw runtime_error("Fixed component out of boundary: " + c.id);
        }
        if (!canPlaceAt(ci, c.x, c.y, occ_local)) {
            throw runtime_error("Fixed component overlap: " + c.id);
        }
        stampComponent(ci, c.x, c.y, occ_local);
    }
}

bool PlacementDB::insideBoundary(int x, int y, int w, int h) const {
    return x >= 0 && y >= 0 && x + w <= gridW && y + h <= gridH;
}

bool PlacementDB::canPlaceAt(int compIdx, int x, int y, const vector<vector<int>>& local_occ) const {
    const auto& c = comps[compIdx];
    if (!insideBoundary(x, y, c.w, c.h)) {
        return false;
    }

    for (int yy = y; yy < y + c.h; ++yy) {
        for (int xx = x; xx < x + c.w; ++xx) {
            if (local_occ[yy][xx] != -1) {
                return false;
            }
        }
    }
    return true;
}

void PlacementDB::stampComponent(int compIdx, int x, int y, vector<vector<int>>& local_occ) const {
    const auto& c = comps[compIdx];
    for (int yy = y; yy < y + c.h; ++yy) {
        for (int xx = x; xx < x + c.w; ++xx) {
            local_occ[yy][xx] = compIdx;
        }
    }
}

void PlacementDB::unstampComponent(int compIdx, int x, int y) {
    const auto& c = comps[compIdx];
    for (int yy = y; yy < y + c.h; ++yy) {
        for (int xx = x; xx < x + c.w; ++xx) {
            if (occ[yy][xx] == compIdx) {
                occ[yy][xx] = -1;
            }
        }
    }
}

bool PlacementDB::canPlaceAtCurrentOcc(int compIdx, int x, int y) const {
    const auto& c = comps[compIdx];
    if (!insideBoundary(x, y, c.w, c.h)) {
        return false;
    }

    for (int yy = y; yy < y + c.h; ++yy) {
        for (int xx = x; xx < x + c.w; ++xx) {
            if (occ[yy][xx] != -1) {
                return false;
            }
        }
    }
    return true;
}

pair<int, int> PlacementDB::getAbsolutePinPos(int compIdx, int pinIdx) const {
    const auto& c = comps[compIdx];
    const auto& p = c.pins[pinIdx];

    if (c.x < 0 || c.y < 0) {
        throw runtime_error("Component not placed yet: " + c.id);
    }

    return {c.x + p.dx, c.y + p.dy};
}

long long PlacementDB::netHPWL(const Net& net) const {
    int minX = numeric_limits<int>::max();
    int maxX = numeric_limits<int>::min();
    int minY = numeric_limits<int>::max();
    int maxY = numeric_limits<int>::min();

    for (const auto& ref : net.pins) {
        const auto [ax, ay] = getAbsolutePinPos(ref.nodeIdx, ref.pinIdx);
        minX = min(minX, ax);
        maxX = max(maxX, ax);
        minY = min(minY, ay);
        maxY = max(maxY, ay);
    }

    return static_cast<long long>(maxX - minX) + static_cast<long long>(maxY - minY);
}

vector<int> collect_movable_nodes(const PlacementState& state) {
    vector<int> out;
    for (int i = 0; i < static_cast<int>(state.nodes.size()); ++i) {
        if (!state.nodes[i].fixed) {
            out.push_back(i);
        }
    }
    return out;
}

bool apply_random_relocate_move(PlacementDB& db,
                                const vector<int>& movables,
                                mt19937& rng,
                                vector<int>& moved_nodes) {
    if (movables.empty()) {
        return false;
    }

    uniform_int_distribution<int> pick_comp(0, static_cast<int>(movables.size()) - 1);
    const int idx = movables[pick_comp(rng)];
    const Node& node = db.nodes[idx];

    uniform_int_distribution<int> pick_x(0, db.gridW - node.w);
    uniform_int_distribution<int> pick_y(0, db.gridH - node.h);
    const int nx = pick_x(rng);
    const int ny = pick_y(rng);

    if (!db.moveComponent(idx, nx, ny)) {
        return false;
    }

    moved_nodes = {idx};
    return true;
}

bool apply_random_swap_move(PlacementDB& db,
                            const vector<int>& movables,
                            mt19937& rng,
                            vector<int>& moved_nodes) {
    int a = -1;
    int b = -1;
    if (!sample_distinct_pair(movables, rng, a, b)) {
        return false;
    }

    if (!db.swapComponents(a, b)) {
        return false;
    }

    moved_nodes = {a, b};
    return true;
}

bool run_placement_engine(const string& input_path,
                          const string& output_path,
                          long long& total_hpwl,
                          unsigned seed) {
    try {
        PlacementDB db;
        db.parseFile(input_path);
        db.randomLegalPlacement(seed);
        total_hpwl = db.totalHPWL();
        db.writePlacementFile(output_path);
        return true;
    } catch (...) {
        return false;
    }
}

int run_placement_cli(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <input_file>\n";
        return 1;
    }

    try {
        PlacementDB db;
        db.parseFile(argv[1]);
        db.randomLegalPlacement(12345);
        db.printPlacement();

        const long long total_cost = db.totalHPWL();
        cout << "Total HPWL = " << total_cost << "\n";
        db.writePlacementFile("placement_out.txt");
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
