#ifndef PLACEMENT_ENGINE_H
#define PLACEMENT_ENGINE_H

#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "types.h"

enum class PlacementMoveType {
    NONE,
    RELOCATE,
    SWAP,
};

class PlacementDB : public PlacementState {
public:
    std::vector<Node>& comps;
    std::unordered_map<std::string, int>& compNameToIdx;
    std::unordered_map<std::string, int> netNameToIdx;

    PlacementDB();

    void parseFile(const std::string& filename);
    long long totalHPWL() const;
    void randomLegalPlacement(unsigned seed = 12345);

    bool moveComponent(int compIdx, int newX, int newY);
    bool swapComponents(int a, int b);
    bool restoreLastMove();

    size_t checkpoint() const;
    bool rollbackTo(size_t cp);
    void commitMoves();
    size_t historySize() const;
    bool rebuildOccupancy();

    bool isPlacementLegal() const;
    void printPlacement() const;
    void writePlacementFile(const std::string& filename) const;

private:
    std::vector<std::vector<int>> occ;

    struct MoveRecord {
        PlacementMoveType type = PlacementMoveType::NONE;

        int compA = -1;
        int compB = -1;

        int oldAx = -1;
        int oldAy = -1;
        int oldBx = -1;
        int oldBy = -1;

        int newAx = -1;
        int newAy = -1;
        int newBx = -1;
        int newBy = -1;
    };
    std::vector<MoveRecord> moveHistory;

    static std::string trim(const std::string& s);
    static std::vector<std::string> split(const std::string& s);

    void parseGrid(const std::vector<std::string>& lines, size_t& i);
    void parseComponents(const std::vector<std::string>& lines, size_t& i);
    void parsePins(const std::vector<std::string>& lines, size_t& i);
    void parseNets(const std::vector<std::string>& lines, size_t& i);
    NetPinRef parseCompPinRef(const std::string& s);
    void validateAll() const;

    bool insideBoundary(int x, int y, int w, int h) const;
    bool canPlaceAt(int compIdx, int x, int y, const std::vector<std::vector<int>>& occ) const;
    void stampComponent(int compIdx, int x, int y, std::vector<std::vector<int>>& occ) const;
    void unstampComponent(int compIdx, int x, int y);
    bool canPlaceAtCurrentOcc(int compIdx, int x, int y) const;

    std::pair<int, int> getAbsolutePinPos(int compIdx, int pinIdx) const;
    long long netHPWL(const Net& net) const;
};

std::vector<int> collect_movable_nodes(const PlacementState& state);
bool apply_random_relocate_move(PlacementDB& db,
                                const std::vector<int>& movables,
                                std::mt19937& rng,
                                std::vector<int>& moved_nodes);
bool apply_random_swap_move(PlacementDB& db,
                            const std::vector<int>& movables,
                            std::mt19937& rng,
                            std::vector<int>& moved_nodes);
bool apply_heuristic_relocate_move(PlacementDB& db,
                                   const std::vector<int>& movables,
                                   std::mt19937& rng,
                                   std::vector<int>& moved_nodes);
bool apply_heuristic_swap_move(PlacementDB& db,
                               const std::vector<int>& movables,
                               std::mt19937& rng,
                               std::vector<int>& moved_nodes);

// Runs random legal placement on an input netlist and writes placement output.
// Returns true on success and stores the final total HPWL in total_hpwl.
bool run_placement_engine(const std::string& input_path,
                          const std::string& output_path,
                          long long& total_hpwl,
                          unsigned seed = 12345);

#endif
