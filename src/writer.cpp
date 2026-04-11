#include <fstream>

#include "../include/io_engine.h"

using namespace std;

bool write_placement(const string& path,
                     const PlacementState& state,
                     long long cost,
                     const string& meta) {
    ofstream fout(path);
    if (!fout) return false;

    fout << "GRID " << state.gridW << " " << state.gridH << "\n\n";

    if (!meta.empty()) {
        fout << "# meta: " << meta << "\n";
        fout << "# cost: " << cost << "\n\n";
    }

    fout << "COMPONENTS " << state.nodes.size() << "\n";
    for (const auto& node : state.nodes) {
        fout << "COMPONENT " << node.id << " "
             << node.w << " " << node.h << " "
             << (node.fixed ? "fixed" : "movable") << " "
             << node.x << " " << node.y << "\n";
    }

    int totalPins = 0;
    for (const auto& node : state.nodes) {
        totalPins += (int)node.pins.size();
    }

    fout << "\nPINS " << totalPins << "\n";
    for (const auto& node : state.nodes) {
        for (const auto& pin : node.pins) {
            fout << "PIN " << node.id << " " << pin.name << " "
                 << pin.dx << " " << pin.dy << "\n";
        }
    }

    fout << "\nNETS " << state.nets.size() << "\n";
    for (const auto& net : state.nets) {
        fout << "NET " << net.id << " " << net.pins.size();
        for (const auto& ref : net.pins) {
            const auto& node = state.nodes[ref.nodeIdx];
            const auto& pin = node.pins[ref.pinIdx];
            fout << " " << node.id << "." << pin.name;
        }
        fout << "\n";
    }

    return true;
}