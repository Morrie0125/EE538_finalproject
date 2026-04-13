#include <cassert>
#include <iostream>
#include "adjacency.h"

int main() {
  PlacementState s;
  s.nodes.resize(3);
  s.nets.resize(2);

  s.nodes[0].id = "A";
  s.nodes[1].id = "B";
  s.nodes[2].id = "C";

  s.nets[0].id = "n0";
  s.nets[0].pins = {NetPinRef{0, 0}, NetPinRef{1, 0}};

  s.nets[1].id = "n1";
  s.nets[1].pins = {NetPinRef{1, 0}, NetPinRef{2, 0}};

  auto adj = adjacency::build_adjacency(s);
  adjacency::validate_adjacency(s, adj);

  assert(adj.node_to_nets[0].size() == 1 && adj.node_to_nets[0][0] == 0);
  assert(adj.node_to_nets[1].size() == 2);
  assert(adj.node_to_nets[2].size() == 1 && adj.node_to_nets[2][0] == 1);
  assert(adj.net_hpwl_cache.size() == 2);

  std::cout << "All adjacency tests passed\n";
}