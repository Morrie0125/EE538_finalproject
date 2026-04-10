#include <cassert>     // for assert()
#include <iostream>    // for output
#include <stdexcept>
#include <string>

#include "hpwl.h"

using hpwl::net_hpwl;
using hpwl::total_hpwl;

/*
Helper function to add a node with a single pin to the placement state.
Parameters:
    - id: node name
    - (x, y): node placement location
    - (pin_dx, pin_dy): offset of the pin from the node position

Returns: index of the newly added node
*/
static int add_node(PlacementState& state,
                    const std::string& id,
                    int x,
                    int y,
                    int pin_dx = 0,
                    int pin_dy = 0,
                    const std::string& pin_name = "p") {
  Node node;
  node.id = id;
  node.x = x;
  node.y = y;

  // Create a single pin for this node
  Pin pin;
  pin.name = pin_name;
  pin.dx = pin_dx;
  pin.dy = pin_dy;

  // Register pin in node
  node.pinNameToIdx[pin_name] = 0;
  node.pins.push_back(pin);

  // Register node in placement state
  state.nodeNameToIdx[id] = static_cast<int>(state.nodes.size());
  state.nodes.push_back(node);

  return static_cast<int>(state.nodes.size()) - 1;
}

/*
Helper function to create a NetPinRef from node and pin names.
Looks up indices from the placement state's maps.
*/
static NetPinRef ref_of(const PlacementState& state,
                        const std::string& node_id,
                        const std::string& pin_name = "p") {
  const int node_idx = state.nodeNameToIdx.at(node_id);
  const Node& node = state.nodes[node_idx];
  const int pin_idx = node.pinNameToIdx.at(pin_name);
  return NetPinRef{node_idx, pin_idx};
}

int main() {
  // ----------------------------
  // Basic HPWL tests (no offsets)
  // ----------------------------
  PlacementState s;
  s.gridW = 20;
  s.gridH = 20;

  // Add nodes with default pin at (0,0) offset
  add_node(s, "A", 1, 1);
  add_node(s, "B", 4, 1);
  add_node(s, "C", 2, 3);
  add_node(s, "D", 7, 5);

  // 2-pin net (horizontal line)
  Net n1;
  n1.id = "n1";
  n1.pins = {ref_of(s, "A"), ref_of(s, "B")};

  // 3-pin net (triangle shape)
  Net n2;
  n2.id = "n2";
  n2.pins = {ref_of(s, "A"), ref_of(s, "B"), ref_of(s, "C")};

  // 4-pin net (larger bounding box)
  Net n3;
  n3.id = "n3";
  n3.pins = {ref_of(s, "A"), ref_of(s, "B"), ref_of(s, "C"), ref_of(s, "D")};

  s.nets = {n1, n2, n3};

  // Validate per-net HPWL
  assert(net_hpwl(s, s.nets[0]) == 3);   // (1,1) to (4,1) → width=3, height=0
  assert(net_hpwl(s, s.nets[1]) == 5);   // bbox: (1,1) to (4,3) → 3+2=5
  assert(net_hpwl(s, s.nets[2]) == 10);  // bbox: (1,1) to (7,5) → 6+4=10

  // Validate total HPWL
  assert(total_hpwl(s) == 18);

  // -----------------------------------
  // Hyperedge test with pin offsets
  // -----------------------------------
  PlacementState t;
  t.gridW = 10;
  t.gridH = 10;

  // Nodes with non-zero pin offsets
  add_node(t, "U", 0, 0, 1, 0, "p0");  // pin at (1,0)
  add_node(t, "V", 3, 4, 0, 2, "p0");  // pin at (3,6)
  add_node(t, "W", 5, 1, 2, 3, "p0");  // pin at (7,4)

  Net hyper;
  hyper.id = "hyper";
  hyper.pins = {
      ref_of(t, "U", "p0"),
      ref_of(t, "V", "p0"),
      ref_of(t, "W", "p0")
  };

  t.nets = {hyper};

  /*
  Absolute pin locations:
    U → (1,0)
    V → (3,6)
    W → (7,4)

   Bounding box:
    min = (1,0), max = (7,6)
   
    HPWL = (7 - 1) + (6 - 0) = 6 + 6 = 12
  */
  assert(net_hpwl(t, t.nets[0]) == 12);
  assert(total_hpwl(t) == 12);

  std::cout << "All HPWL tests passed\n";
  return 0;
}