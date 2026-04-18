# SA Spec

## Goal
Minimize total HPWL while keeping the placement legal.

## State
Use `PlacementState` from `include/types.h` as the shared placement database.
All SA-related code should operate on that state.

## Objective
Cost is `C(s) = total_hpwl(s)` for a legal placement `s`.
Fixed components never move.

## Moves and Constraints
Allowed moves:
- relocate one movable component
- swap two movable components

Hard constraints:
- stay inside the grid
- no overlap
- fixed components do not move

Illegal move handling:
- resample up to `K = 3` times per SA iteration
- if all attempts fail, count the iteration as rejected

## Delta
For a proposed move from `s` to `s'`:
- `Δ = C(s') - C(s)`

Cost mode is selected by CLI:
- `full`: recompute `total_hpwl` for `s'`
- `delta`: use adjacency + `delta_hpwl`

## Acceptance
Metropolis rule with temperature `T > 0`:
- if `Δ <= 0`, accept
- else accept with probability `exp(-Δ / T)`

Rejected moves must be reverted.

## Cooling
Use geometric cooling with outer temperature stages.

- start with `T0`
- each stage runs a fixed number of inner iterations at the same `T`
- after each stage, set `T = alpha * T`

Required parameters:
- `T0 > 0`
- `0 < alpha < 1`

## Stop Condition
Stop when the global attempt budget is exhausted:
- `max_iters` total SA iterations

Optional early stop:
- if `T < 1e-9`, stop

## CLI
Add one command:
- `sa_place <input> <output> <seed> <max_iters> <t0> <alpha> [--cost full|delta] [--moves_per_temp N] [--illegal_retry K]`

Defaults:
- `--cost full`
- `--moves_per_temp 100`
- `--illegal_retry 3`

## Log
Log once per temperature stage.

Required fields:
- stage index
- temperature
- attempted moves
- accepted moves
- accepted uphill moves
- current HPWL
- best HPWL so far
- cost mode

## Output
Write the final placement to the requested output file and print a short summary with initial HPWL, final HPWL, and accepted move count.

Per-stage fields:
- `stage_idx`
- `temperature`
- `attempted_moves`
- `accepted_moves`
- `accepted_uphill_moves`
- `best_hpwl_so_far`
- `current_hpwl`
- `cost_mode` (`full` or `delta`)

Minimal output options:
- Print one line per stage to stdout, and/or
- write CSV with the same fields.

## 10. Brief Implementation Notes
- Keep first version simple and deterministic under fixed `seed`.
- Ensure apply/revert path is correct before optimizing speed.
- For `delta` mode, include a debug validation switch (optional) to occasionally compare delta-based update vs full recompute during development/testing.
- Reuse existing tests where possible and add a small SA smoke test (runs few iterations and checks legality + non-crash).
