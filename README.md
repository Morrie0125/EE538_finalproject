# EE538_finalproject

## Run Program

Build:
- macOS/Linux: `make`
- Windows (MSYS2/MinGW): `mingw32-make`

Run interactive CLI:
- macOS/Linux: `./main`
- Windows: `.\\main.exe`

Then enter commands in the terminal:
- `help`
- `generate case1.txt 10 10 5 7 12345`
- `place case1.txt`
- `roundtrip_test`
- `exit`



## Format Description

Grid
- GRID (W) (H)

Components
- COMPONENTS <N>
- COMPONENT <comp_id> <w> <h> <movable|fixed> [x y]
- movable components do not require coordinates in the input
- fixed components must provide (x, y)

Pins
- PINS <P>
- PIN <comp_id> <pin_name> <dx> <dy>
- (dx, dy) is the pin location relative to the component origin

Nets
- NETS <M>
- NET <net_id> <degree> <comp.pin> <comp.pin> ...
- each net connects 2 or more pins
- nets are modeled as hyperedges


## Files

### `src/main.cpp`
Interactive CLI entry point.

Responsibilities:
- read one command line from terminal
- parse tokens
- print help
- dispatch to command wrappers

---

### `include/commands.h`
Shared declarations for CLI command wrappers:
- `run_generator_cli(...)`
- `run_placement_cli(...)`
- `run_roundtrip_test_cli(...)`

---

### `src/generator.cpp`
Implements the `generate` command.

It creates:
- grid size
- components with random sizes
- movable / fixed components
- pins inside each component
- nets connecting pins

Output: benchmark file such as `case1.txt`

---

### `src/placement.cpp`
Implements the `place` command.

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

### `src/test_roundtrip.cpp`
Implements the `roundtrip_test` command.

Default behavior:
- read `examples/tiny_case.txt`
- write `examples/roundtrip_out.txt`
- read it back and check grid/node/net consistency

It also supports optional paths:
- `roundtrip_test [input_file] [output_file]`

---

### `visualize.py`
Reads `placement_out.txt` and visualizes:
- grid boundary
- component locations
- pins
- pin-to-pin net connections

This is mainly used for debugging and demonstrating placement results.

---

### `include/hpwl.cpp`
Implements Half-Perimeter Wirelength (HPWL) cost computation. HPWL is defined as (max_x - min_x) + (max_y - min_y)

Main features:
- computes per-net HPWL using bounding box of pins
- computes total HPWL across all nets
- uses pin offsets relative to component positions
- validates node and pin indices before computation

This is used as the placement cost function and designed to be reusable by placement and SA optimization algorithm.

---

### `include/tests_hpwl.cpp`
Unit tests for validating HPWL implementation.

Main features:
- constructs small, hand-checkable placement states
- tests 2-pin nets, 3-pin nets, hyperedges (multi-pin nets), pin offset handling
- verifies both per-net and total HPWL
- Outputs "All HPWL tests passed" if all checks succeed (run ./hpwl_test)

This ensures correctness of HPWL implementation.

---
