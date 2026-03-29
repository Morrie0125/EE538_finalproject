# EE538_finalproject

##Format Description

Grid
GRID <W> <H>

Components
COMPONENTS <N>
COMPONENT <comp_id> <w> <h> <movable|fixed> [x y]
-movable components do not require coordinates in the input
-fixed components must provide (x, y)

Pins
PINS <P>
PIN <comp_id> <pin_name> <dx> <dy>
-(dx, dy) is the pin location relative to the component origin

Nets
NETS <M>
NET <net_id> <degree> <comp.pin> <comp.pin> ...
-each net connects 2 or more pins
-nets are modeled as hyperedges


## Files

### `generator.cpp`
Generates random placement benchmark input files.

It creates:
- grid size
- components with random sizes
- movable / fixed components
- pins inside each component
- nets connecting pins

Output: benchmark file such as `case1.txt`

---

### `placement.cpp`
Reads the benchmark file and performs a legal initial placement.

Main features:
- parses the input file
- handles variable-size components
- supports fixed and movable components
- checks overlap and boundary legality
- performs random legal placement for movable components
- computes total pin-based HPWL
- writes placement result to `placement_out.txt`

---

### `visualize.py`
Reads `placement_out.txt` and visualizes:
- grid boundary
- component locations
- pins
- pin-to-pin net connections

This is mainly used for debugging and demonstrating placement results.

---

