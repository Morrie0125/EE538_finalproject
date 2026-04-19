#!/usr/bin/env python3
"""
T22 — Experiment Runner + Summary CSV

Runs baseline + SA placement across multiple benchmarks and seeds,
then consolidates results into results.csv.

Output structure:
  outputs/<bench_stem>/seed_<seed>/baseline_out.txt
  outputs/<bench_stem>/seed_<seed>/sa_out.txt
  outputs/<bench_stem>/seed_<seed>/logs/
  results.csv

Usage:
  python3 scripts/run_experiments.py
  python3 scripts/run_experiments.py --iters 8000 --t0 300 --alpha 0.95 --cost delta
  python3 scripts/run_experiments.py --dry-run   # print plan without running
"""

import argparse
import csv
import os
import sys
import time
from pathlib import Path

# Reuse PTY runner and legality checker from smoke_test
sys.path.insert(0, str(Path(__file__).resolve().parent))
from smoke_test import run_repl_cmd, check_placement_legality, extract_int

import shutil
import subprocess

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
SCRIPT_DIR  = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
BINARY      = PROJECT_DIR / "main"

BENCHMARKS = [
    "bench/clustered_small.txt",
    "bench/clustered_medium.txt",
    "bench/hub_small.txt",
    "bench/hub_medium.txt",
]

SEEDS = [42, 123]

RESULTS_CSV = PROJECT_DIR / "results.csv"
OUTPUTS_DIR = PROJECT_DIR / "outputs"

CSV_FIELDS = [
    "bench",
    "seed",
    "baseline_hpwl",
    "sa_best_hpwl",
    "improve_pct",
    "sa_runtime_ms",
]


# ---------------------------------------------------------------------------
# Single experiment
# ---------------------------------------------------------------------------

def run_one(bench_rel: str, seed: int, iters: int, t0: float,
            alpha: float, cost: str, dry_run: bool) -> dict:
    """
    Run baseline + SA for one (bench, seed) pair.
    Returns a result dict with CSV_FIELDS keys.
    """
    bench_path = PROJECT_DIR / bench_rel
    bench_stem = bench_path.stem
    run_dir    = OUTPUTS_DIR / bench_stem / f"seed_{seed}"
    baseline_out = run_dir / "baseline_out.txt"
    sa_out       = run_dir / "sa_out.txt"
    sa_out_arg   = str(Path("outputs") / bench_stem / f"seed_{seed}" / "sa_out.txt")

    tag = f"  [{bench_stem} / seed={seed}]"

    if dry_run:
        print(f"{tag}  (dry-run, skipping)")
        return {
            "bench": bench_stem, "seed": seed,
            "baseline_hpwl": "N/A", "sa_best_hpwl": "N/A",
            "improve_pct": "N/A", "sa_runtime_ms": "N/A",
            "legal_baseline": "N/A", "legal_sa": "N/A",
        }

    run_dir.mkdir(parents=True, exist_ok=True)

    # ---- Baseline ----
    print(f"{tag}  baseline ...", end="", flush=True)
    result = run_repl_cmd(f"place {bench_rel}", cwd=PROJECT_DIR)
    baseline_hpwl = extract_int(result.stdout, r"Total HPWL\s*=\s*(\d+)")
    if baseline_hpwl is None:
        print(f"  FAIL (could not parse HPWL)")
        return None

    src = PROJECT_DIR / "placement_out.txt"
    if src.exists():
        shutil.copy(src, baseline_out)

    legal_baseline = "OK"
    if baseline_out.exists():
        errs = check_placement_legality(baseline_out, bench_path)
        legal_baseline = "OK" if not errs else f"FAIL({len(errs)})"

    print(f"  HPWL={baseline_hpwl:,}", end="", flush=True)

    # ---- SA ----
    print(f"  |  SA ...", end="", flush=True)
    sa_cmd = (
        f"sa_place {bench_rel} {sa_out_arg} "
        f"{seed} {iters} {t0} {alpha} --cost {cost}"
    )
    logs_src = PROJECT_DIR / "logs"
    existing_logs = set(logs_src.iterdir()) if logs_src.exists() else set()
    t_start = time.monotonic()
    result  = run_repl_cmd(sa_cmd, cwd=PROJECT_DIR, timeout=600)
    elapsed_ms = int((time.monotonic() - t_start) * 1000)

    sa_hpwl = extract_int(result.stdout, r"SA done: best=(\d+)")
    if sa_hpwl is None:
        print(f"  FAIL (could not parse SA HPWL)")
        return None

    legal_sa = "OK"
    if sa_out.exists():
        errs = check_placement_legality(sa_out, bench_path)
        legal_sa = "OK" if not errs else f"FAIL({len(errs)})"

    # Copy SA logs
    if logs_src.exists():
        logs_dst = run_dir / "logs"
        if logs_dst.exists():
            shutil.rmtree(logs_dst)
        logs_dst.mkdir(parents=True, exist_ok=True)
        for log_path in sorted(set(logs_src.iterdir()) - existing_logs):
            if log_path.is_file():
                shutil.copy(log_path, logs_dst / log_path.name)

    improve_pct = round(100.0 * (baseline_hpwl - sa_hpwl) / baseline_hpwl, 2) \
                  if baseline_hpwl > 0 else 0.0
    symbol = "▲" if sa_hpwl < baseline_hpwl else "▼"
    print(f"  best={sa_hpwl:,}  {symbol}{abs(improve_pct):.1f}%  {elapsed_ms}ms")

    return {
        "bench":          bench_stem,
        "seed":           seed,
        "baseline_hpwl":  baseline_hpwl,
        "sa_best_hpwl":   sa_hpwl,
        "improve_pct":    improve_pct,
        "sa_runtime_ms":  elapsed_ms,
        "_legal_baseline": legal_baseline,
        "_legal_sa":       legal_sa,
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="T22 Experiment Runner")
    parser.add_argument("--iters",   type=int,   default=8000)
    parser.add_argument("--t0",      type=float, default=300.0)
    parser.add_argument("--alpha",   type=float, default=0.95)
    parser.add_argument("--cost",    default="delta", choices=["full", "delta"])
    parser.add_argument("--dry-run", action="store_true",
                        help="Print experiment plan without running")
    args = parser.parse_args()

    sep = "=" * 60
    total = len(BENCHMARKS) * len(SEEDS)

    print(sep)
    print("T22 — Experiment Runner")
    print(sep)
    print(f"  Benchmarks : {len(BENCHMARKS)}  ×  Seeds: {SEEDS}  =  {total} runs")
    print(f"  SA params  : iters={args.iters} T0={args.t0} "
          f"alpha={args.alpha} cost={args.cost}")
    print(f"  Output     : outputs/<bench>/seed_<seed>/")
    print(f"  CSV        : results.csv")
    if args.dry_run:
        print("  ** DRY RUN — no commands will execute **")
    print()

    # Build first
    if not args.dry_run:
        print("[build] make main ...", end="", flush=True)
        r = subprocess.run(["make", "main"], cwd=str(PROJECT_DIR),
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f"\n  FAIL\n{r.stderr}")
            sys.exit(1)
        if not BINARY.exists():
            print(f"\n  FAIL — binary not found")
            sys.exit(1)
        print("  OK\n")

    # Run experiments
    rows = []
    failed = []

    for i, bench_rel in enumerate(BENCHMARKS):
        for seed in SEEDS:
            n = len(rows) + len(failed) + 1
            print(f"[{n}/{total}]", end="")
            row = run_one(bench_rel, seed, args.iters, args.t0,
                          args.alpha, args.cost, args.dry_run)
            if row is None:
                failed.append((bench_rel, seed))
            else:
                rows.append(row)

    # Write results.csv
    if rows and not args.dry_run:
        with open(RESULTS_CSV, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
            writer.writeheader()
            writer.writerows({field: row[field] for field in CSV_FIELDS} for row in rows)
        print(f"\nSaved → results.csv  ({len(rows)} rows)")

    # Summary table
    print()
    print(sep)
    print(f"{'bench':<22} {'seed':>5} {'baseline':>10} {'sa_best':>10} {'improve%':>9} {'ms':>7} {'legal':>8}")
    print("-" * 60)
    for r in rows:
        legal_sa = r.get("_legal_sa", "N/A")
        legal = "✓" if legal_sa == "OK" else ("-" if legal_sa == "N/A" else "✗")
        base  = f"{r['baseline_hpwl']:,}" if isinstance(r["baseline_hpwl"], int) else str(r["baseline_hpwl"])
        sa    = f"{r['sa_best_hpwl']:,}"  if isinstance(r["sa_best_hpwl"],  int) else str(r["sa_best_hpwl"])
        imp   = f"{r['improve_pct']:.1f}%" if isinstance(r["improve_pct"], float) else str(r["improve_pct"])
        ms    = f"{r['sa_runtime_ms']:,}"  if isinstance(r["sa_runtime_ms"], int) else str(r["sa_runtime_ms"])
        print(f"  {r['bench']:<20} {r['seed']:>5} {base:>10} {sa:>10} {imp:>9} {ms:>7}  {legal}")

    if failed:
        print(f"\nFailed runs ({len(failed)}):")
        for b, s in failed:
            print(f"  {b}  seed={s}")

    real_rows    = [r for r in rows if isinstance(r["sa_best_hpwl"], int)]
    all_improved = all(r["sa_best_hpwl"] < r["baseline_hpwl"] for r in real_rows)
    all_legal    = all(r.get("_legal_sa") == "OK" for r in real_rows)

    print()
    if real_rows:
        print(f"  SA improves on all benches : {'YES ✓' if all_improved else 'NO ✗'}")
        print(f"  All SA outputs legal       : {'YES ✓' if all_legal    else 'NO ✗'}")
    else:
        print("  (dry-run — no results to validate)")
    print(sep)

    if not args.dry_run and (failed or not all_legal):
        sys.exit(1)


if __name__ == "__main__":
    main()
