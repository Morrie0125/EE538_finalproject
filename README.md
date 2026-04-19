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
- `sa_place examples/tiny_case.txt sa_out.txt 42 300 100 0.95 --cost full`
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
- This target links both `test/test_move_engine.cpp` and `src/generator.cpp`.

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

## SA Place (New)

Command
- `sa_place <input> <output> <seed> <max_iters> <t0> <alpha> [--cost full|delta] [--moves_per_temp N] [--illegal_retry K] [--relocate_ratio R]`

Defaults
- `--cost full`
- `--moves_per_temp 100`
- `--illegal_retry 3`
- `--relocate_ratio 0.5` (API reserved; currently not used for sampling)

Behavior summary
- Objective: minimize total HPWL with legal placement only.
- Output: writes best-so-far historical placement (not last state).
- Step budget: `max_iters` counts SA steps; each step may try multiple illegal proposals up to retry limit.
- Stage logging: every temperature stage writes one record, including partial final stage.

Logs
- SA automatically creates `logs/`.
- Per run, two files are generated with source+timestamp naming:
    - `logs/sa_place_<timestamp>.csv`
    - `logs/sa_place_<timestamp>.log`
- Key stage info and final summary are also printed to terminal.

Demo mode
- `generate --demo [easy|mid|hard|large]` creates a demo input in `demo/demo_input_<mode>.txt` from the JSON preset for that mode. Default mode is `easy`.
- `sa_place --demo [easy|mid|hard|large]` internally calls `generate --demo <mode>`, uses the JSON preset for its SA defaults and output paths, and writes one best snapshot per stage to `demo/snaps/stage_<idx>_best.txt`.
- `visualize --demo` plays stage snapshots in order as an animation and overlays HPWL on the plot.

Demo presets
- The actual demo parameters now live in `demo/easy.json`, `demo/mid.json`, `demo/hard.json`, and `demo/large.json`.
- Each JSON file controls both the generator inputs and the SA defaults for that mode.
- Edit those files to tune demo difficulty or runtime without touching the C++ sources.

## Project Structure (Short)

- `src/main.cpp`: CLI entry.
- `src/generator.cpp`: random benchmark generator.
- `src/placement.cpp`: parse + legal placement + placement output.
- `src/sa.cpp`: simulated annealing engine + `sa_place` command.
- `src/sa_logger.cpp`: reusable SA log formatting and file output.
- `src/hpwl.cpp`: full HPWL calculation.
- `src/adjacency.cpp`: node-to-net adjacency.
- `src/delta_hpwl.cpp`: incremental HPWL delta.
- `test/test_move_engine.cpp`: regression/debug move-engine tests.
- `test/test_roundtrip.cpp`: parser-writer roundtrip test.
- `scripts/visualize.py`: placement visualization.

---