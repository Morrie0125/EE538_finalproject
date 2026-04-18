#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "placement.cpp"

using namespace std;

static void write_test_case(const string& filename) {
    ofstream fout(filename);
    if (!fout) {
        throw runtime_error("Failed to create test case file: " + filename);
    }

    fout << "GRID 8 8\n";

    fout << "COMPONENTS 4\n";
    fout << "COMPONENT U0 2 2 fixed 0 0\n";
    fout << "COMPONENT U1 2 2 movable\n";
    fout << "COMPONENT U2 1 2 movable\n";
    fout << "COMPONENT U3 2 1 movable\n";

    fout << "PINS 4\n";
    fout << "PIN U0 P0 0 0\n";
    fout << "PIN U1 P0 0 0\n";
    fout << "PIN U2 P0 0 0\n";
    fout << "PIN U3 P0 0 0\n";

    fout << "NETS 1\n";
    fout << "NET N0 4 U0.P0 U1.P0 U2.P0 U3.P0\n";
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

int main() {
    const string filename = "move_test_case.txt";
    write_test_case(filename);

    PlacementDB db;
    db.parseFile(filename);
    db.randomLegalPlacement(12345);

    // Test 1: initial placement should be legal
    assert(db.isPlacementLegal());

    int fixedIdx = find_fixed_idx(db);
    assert(fixedIdx != -1);

    vector<int> movables = find_movable_indices(db);
    assert(movables.size() >= 2);

    int a = movables[0];
    int b = movables[1];

    // Test 2: fixed + movable cannot swap
    {
        int fx = db.comps[fixedIdx].x;
        int fy = db.comps[fixedIdx].y;
        int ax = db.comps[a].x;
        int ay = db.comps[a].y;

        bool ok = db.swapComponents(fixedIdx, a);
        assert(ok == false);
        assert(db.isPlacementLegal());
        assert(db.comps[fixedIdx].x == fx);
        assert(db.comps[fixedIdx].y == fy);
        assert(db.comps[a].x == ax);
        assert(db.comps[a].y == ay);
    }

// Test 3: movable + movable swap
{
    int ax = db.comps[a].x;
    int ay = db.comps[a].y;
    int bx = db.comps[b].x;
    int by = db.comps[b].y;

    cout << "Before swap:\n";
    cout << db.comps[a].id << " = (" << ax << ", " << ay << ")\n";
    cout << db.comps[b].id << " = (" << bx << ", " << by << ")\n";

    db.writePlacementFile("before_swap.txt");

    bool ok = db.swapComponents(a, b);
    cout << "swap result = " << ok << endl;

    cout << "After swap attempt:\n";
    cout << db.comps[a].id << " = (" << db.comps[a].x << ", " << db.comps[a].y << ")\n";
    cout << db.comps[b].id << " = (" << db.comps[b].x << ", " << db.comps[b].y << ")\n";

    if (ok) {
        assert(db.comps[a].x == bx);
        assert(db.comps[a].y == by);
        assert(db.comps[b].x == ax);
        assert(db.comps[b].y == ay);
        assert(db.isPlacementLegal());

        db.writePlacementFile("after_swap.txt");
    } else {
        assert(db.comps[a].x == ax);
        assert(db.comps[a].y == ay);
        assert(db.comps[b].x == bx);
        assert(db.comps[b].y == by);
        assert(db.isPlacementLegal());
    }
}

    // Test 4: swap itself should be illegal
    {
        int ax = db.comps[a].x;
        int ay = db.comps[a].y;

        bool ok = db.swapComponents(a, a);
        assert(ok == false);
        assert(db.comps[a].x == ax);
        assert(db.comps[a].y == ay);
        assert(db.isPlacementLegal());
    }
    cout << "All move engine tests passed.\n";
    
    return 0;
}