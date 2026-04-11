#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../include/io_engine.h"

using namespace std;

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

static NetPinRef parseCompPinRef(const string& s, const PlacementState& state) {
    auto dotPos = s.find('.');
    if (dotPos == string::npos) {
        throw runtime_error("Pin reference must be comp.pin: " + s);
    }

    string compId = s.substr(0, dotPos);
    string pinName = s.substr(dotPos + 1);

    auto cit = state.nodeNameToIdx.find(compId);
    if (cit == state.nodeNameToIdx.end()) {
        throw runtime_error("Unknown component in net: " + compId);
    }

    int ci = cit->second;
    auto pit = state.nodes[ci].pinNameToIdx.find(pinName);
    if (pit == state.nodes[ci].pinNameToIdx.end()) {
        throw runtime_error("Unknown pin in net: " + s);
    }

    return {ci, pit->second};
}

bool read_netlist(const string& path, PlacementState& state) {
    try {
        ifstream fin(path);
        if (!fin) return false;

        state = PlacementState();

        vector<string> lines;
        string line;
        while (getline(fin, line)) {
            auto sharpPos = line.find('#');
            if (sharpPos != string::npos) {
                line = line.substr(0, sharpPos);
            }
            line = trim(line);
            if (!line.empty()) lines.push_back(line);
        }

        size_t i = 0;

        // GRID
        {
            auto tk = split(lines.at(i++));
            if (tk.size() != 3 || tk[0] != "GRID") {
                throw runtime_error("Expected: GRID <W> <H>");
            }
            state.gridW = stoi(tk[1]);
            state.gridH = stoi(tk[2]);
        }

        // COMPONENTS
        int n = 0;
        {
            auto tk = split(lines.at(i++));
            if (tk.size() != 2 || tk[0] != "COMPONENTS") {
                throw runtime_error("Expected: COMPONENTS <N>");
            }
            n = stoi(tk[1]);
        }

        for (int k = 0; k < n; ++k) {
            auto t = split(lines.at(i++));

            if (t.size() != 5 && t.size() != 7) {
                throw runtime_error("Invalid COMPONENT line");
            }
            if (t[0] != "COMPONENT") {
                throw runtime_error("Expected COMPONENT line");
            }

            Node node;
            node.id = t[1];
            node.w = stoi(t[2]);
            node.h = stoi(t[3]);

            if (t[4] == "movable") {
                node.fixed = false;
            } else if (t[4] == "fixed") {
                node.fixed = true;
                if (t.size() != 7) {
                    throw runtime_error("Fixed component must have coordinates");
                }
                node.x = stoi(t[5]);
                node.y = stoi(t[6]);
            } else {
                throw runtime_error("Component type must be movable or fixed");
            }

            state.nodeNameToIdx[node.id] = (int)state.nodes.size();
            state.nodes.push_back(node);
        }

        // PINS
        int p = 0;
        {
            auto tk = split(lines.at(i++));
            if (tk.size() != 2 || tk[0] != "PINS") {
                throw runtime_error("Expected: PINS <P>");
            }
            p = stoi(tk[1]);
        }

        for (int k = 0; k < p; ++k) {
            auto t = split(lines.at(i++));

            if (t.size() != 5 || t[0] != "PIN") {
                throw runtime_error("Invalid PIN line");
            }

            string compId = t[1];
            string pinName = t[2];
            int dx = stoi(t[3]);
            int dy = stoi(t[4]);

            int ci = state.nodeNameToIdx.at(compId);

            Pin pin;
            pin.name = pinName;
            pin.dx = dx;
            pin.dy = dy;

            state.nodes[ci].pinNameToIdx[pinName] = (int)state.nodes[ci].pins.size();
            state.nodes[ci].pins.push_back(pin);
        }

        // NETS
        int m = 0;
        {
            auto tk = split(lines.at(i++));
            if (tk.size() != 2 || tk[0] != "NETS") {
                throw runtime_error("Expected: NETS <M>");
            }
            m = stoi(tk[1]);
        }

        for (int k = 0; k < m; ++k) {
            auto t = split(lines.at(i++));

            if (t.size() < 4 || t[0] != "NET") {
                throw runtime_error("Invalid NET line");
            }

            Net net;
            net.id = t[1];
            int degree = stoi(t[2]);

            if ((int)t.size() != 3 + degree) {
                throw runtime_error("NET degree mismatch");
            }

            for (int j = 0; j < degree; ++j) {
                net.pins.push_back(parseCompPinRef(t[3 + j], state));
            }

            state.nets.push_back(net);
        }

        return true;
    } catch (...) {
        return false;
    }
}