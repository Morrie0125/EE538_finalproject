# EE538 Final Project

## Quick Start

Build
- macOS/Linux: `make`
- Windows (MSYS2/MinGW): `mingw32-make`

Run CLI
- macOS/Linux: `./main`
- Windows: `.\main.exe`

Common commands
- `help`
- `generate case1.txt 10 10 5 7 12345`
- `place case1.txt`
- `roundtrip_test`
- `exit`

## Input Format

- `GRID <W> <H>`
- `COMPONENTS <N>`
- `COMPONENT <id> <w> <h> <movable|fixed> [x y]`
- `PINS <P>`
- `PIN <comp_id> <pin_name> <dx> <dy>`
- `NETS <M>`
- `NET <net_id> <degree> <comp.pin> <comp.pin> ...`

Notes
- `movable` components do not need `(x, y)`.
- `fixed` components must provide `(x, y)`.
- Nets are hyperedges (degree >= 2).

## Move Engine Test (Updated)

Build target
- `make move_engine_test`
- This target links both `src/test_move_engine.cpp` and `src/generator.cpp`.

Modes
- Regression (summary + per-seed result, low noise):
    - `./move_engine_test regression [start_seed] [count]`
- Debug (single seed, step-by-step log):
    - `./move_engine_test debug <seed>`
    - log file: `move_debug_seed_<seed>.log`

Behavior
- Regression internally samples legal generator parameters from seed.
- Each case reports: seed + PASS/FAIL + reason.
- Debug uses the same seed-to-parameters logic, so failing seeds are reproducible.

## Project Structure (Short)

- `src/main.cpp`: CLI entry.
- `src/generator.cpp`: random benchmark generator.
- `src/placement.cpp`: parse + legal placement + placement output.
- `src/hpwl.cpp`: full HPWL calculation.
- `src/adjacency.cpp`: node-to-net adjacency.
- `src/delta_hpwl.cpp`: incremental HPWL delta.
- `src/test_move_engine.cpp`: regression/debug move-engine tests.
- `src/test_roundtrip.cpp`: parser-writer roundtrip test.
- `scripts/visualize.py`: placement visualization.

---