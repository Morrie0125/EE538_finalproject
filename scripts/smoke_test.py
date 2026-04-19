#!/usr/bin/env python3
"""
T19 — Mid E2E Smoke Test

Runs the full pipeline on a structured benchmark:
  build → baseline placement → SA placement → legality check → HPWL comparison

Deliverables:
  - outputs/smoke/baseline_out.txt   (baseline placement result)
  - outputs/smoke/sa_out.txt         (SA best placement result)
  - outputs/smoke/logs/              (SA convergence CSV + log)
  - outputs/smoke/smoke_summary.txt  (HPWL comparison table)

Usage:
  python3 scripts/smoke_test.py
  python3 scripts/smoke_test.py --bench bench/clustered_small.txt
  python3 scripts/smoke_test.py --bench bench/tiny_30.txt --iters 2000
"""

import argparse
import os
import pty
import re
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR  = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
BINARY      = PROJECT_DIR / "main"

# ---------------------------------------------------------------------------
# Legality checker for placement output files
# ---------------------------------------------------------------------------

def check_placement_legality(placement_path: Path, source_path: Path) -> list[str]:
    """
    Parses a placement output file and checks:
      1. All components are within grid bounds.
      2. No two components overlap (cell-level occupancy).
    Returns a list of error strings (empty = legal).
    """
    errors = []

    with open(placement_path) as f:
        lines = [ln.split("#")[0].strip() for ln in f if ln.split("#")[0].strip()]

    grid_w, grid_h = None, None
    components = {}  # id -> {x, y, w, h}

    i = 0
    while i < len(lines):
        tok = lines[i].split()
        if tok[0] == "GRID":
            grid_w, grid_h = int(tok[1]), int(tok[2])
        elif tok[0] == "COMPONENT" and len(tok) >= 7:
            cid = tok[1]
            w, h = int(tok[2]), int(tok[3])
            # movable/fixed token is tok[4]; coords follow
            if len(tok) >= 7:
                x, y = int(tok[5]), int(tok[6])
                components[cid] = {"x": x, "y": y, "w": w, "h": h}
        i += 1

    if grid_w is None:
        errors.append("GRID declaration not found in placement output")
        return errors

    # Check boundary
    for cid, c in components.items():
        if c["x"] < 0 or c["y"] < 0:
            errors.append(f"Component {cid} at ({c['x']},{c['y']}) is out of bounds (negative)")
        if c["x"] + c["w"] > grid_w or c["y"] + c["h"] > grid_h:
            errors.append(
                f"Component {cid} at ({c['x']},{c['y']}) w={c['w']} h={c['h']} "
                f"exceeds grid {grid_w}x{grid_h}"
            )

    # Check overlap via occupancy grid
    occupancy = {}  # (x, y) -> cid
    for cid, c in components.items():
        for dx in range(c["w"]):
            for dy in range(c["h"]):
                cell = (c["x"] + dx, c["y"] + dy)
                if cell in occupancy:
                    errors.append(
                        f"Overlap at cell {cell}: {occupancy[cell]} and {cid}"
                    )
                else:
                    occupancy[cell] = cid

    return errors


# ---------------------------------------------------------------------------
# Runner helpers
# ---------------------------------------------------------------------------

def run_repl_cmd(cmd: str, cwd: Path, timeout: int = 300) -> subprocess.CompletedProcess:
    """
    Send one command to the main REPL using a PTY (pseudo-terminal).
    This is necessary on macOS because readline(3) returns EOF immediately
    when stdin is a plain pipe rather than a TTY.
    Strategy: open a PTY pair, attach it as stdin so readline sees a TTY,
    wait for the '> ' prompt before writing, then collect all output.
    """
    master_fd, slave_fd = pty.openpty()

    proc = subprocess.Popen(
        [str(BINARY)],
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        cwd=str(cwd),
        close_fds=True,
    )
    os.close(slave_fd)

    output_chunks: list[bytes] = []
    deadline = time.monotonic() + timeout
    sent_cmd = False
    sent_exit = False

    try:
        while proc.poll() is None and time.monotonic() < deadline:
            rlist, _, _ = select.select([master_fd], [], [], 0.1)
            if rlist:
                try:
                    chunk = os.read(master_fd, 4096)
                    output_chunks.append(chunk)
                    text_so_far = b"".join(output_chunks).decode(errors="replace")

                    # Wait for first prompt before sending our command
                    if not sent_cmd and "> " in text_so_far:
                        os.write(master_fd, (cmd + "\n").encode())
                        sent_cmd = True

                    # Wait for second prompt (command finished) then exit
                    elif sent_cmd and not sent_exit:
                        prompts = text_so_far.count("> ")
                        if prompts >= 2:
                            os.write(master_fd, b"exit\n")
                            sent_exit = True

                except OSError:
                    break  # PTY closed

        if proc.poll() is None:
            proc.kill()
        proc.wait(timeout=10)

    finally:
        try:
            os.close(master_fd)
        except OSError:
            pass

    pty_text = b"".join(output_chunks).decode(errors="replace")

    return subprocess.CompletedProcess(
        args=[str(BINARY)],
        returncode=proc.returncode or 0,
        stdout=pty_text,
        stderr="",
    )


def extract_int(text: str, pattern: str) -> int | None:
    m = re.search(pattern, text)
    return int(m.group(1)) if m else None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="T19 Mid E2E Smoke Test")
    parser.add_argument("--bench",  default="bench/clustered_small.txt",
                        help="Benchmark file path (relative to project root)")
    parser.add_argument("--seed",   type=int,   default=42)
    parser.add_argument("--iters",  type=int,   default=5000,
                        help="SA max iterations")
    parser.add_argument("--t0",     type=float, default=200.0,
                        help="SA initial temperature")
    parser.add_argument("--alpha",  type=float, default=0.95,
                        help="SA cooling rate")
    parser.add_argument("--cost",   default="full", choices=["full", "delta"],
                        help="SA cost mode")
    parser.add_argument("--outdir", default="outputs/smoke",
                        help="Output directory (relative to project root)")
    args = parser.parse_args()

    bench_path = PROJECT_DIR / args.bench
    output_dir = PROJECT_DIR / args.outdir
    sa_out_arg = str(Path(args.outdir) / "sa_out.txt")
    baseline_out = output_dir / "baseline_out.txt"
    sa_out        = output_dir / "sa_out.txt"
    summary_path  = output_dir / "smoke_summary.txt"

    sep = "=" * 60
    print(sep)
    print("T19 — Mid E2E Smoke Test")
    print(sep)
    print(f"  Benchmark : {bench_path.relative_to(PROJECT_DIR)}")
    print(f"  SA params : seed={args.seed} iters={args.iters} "
          f"T0={args.t0} alpha={args.alpha} cost={args.cost}")
    print(f"  Output dir: {output_dir.relative_to(PROJECT_DIR)}")

    failed = False

    # ------------------------------------------------------------------
    # Step 1 — Build
    # ------------------------------------------------------------------
    print(f"\n[1/5] Building project (make main)...")
    result = subprocess.run(
        ["make", "main"],
        cwd=str(PROJECT_DIR),
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"  FAIL — build error:\n{result.stderr}")
        sys.exit(1)
    print("  OK")

    if not BINARY.exists():
        print(f"  FAIL — binary not found at {BINARY}")
        sys.exit(1)

    # ------------------------------------------------------------------
    # Step 2 — Baseline placement
    # ------------------------------------------------------------------
    print(f"\n[2/5] Baseline placement...")
    output_dir.mkdir(parents=True, exist_ok=True)

    result = run_repl_cmd(f"place {args.bench}", cwd=PROJECT_DIR)
    if result.returncode != 0:
        print(f"  FAIL — place command returned {result.returncode}")
        print(result.stderr)
        sys.exit(1)

    baseline_hpwl = extract_int(result.stdout, r"Total HPWL\s*=\s*(\d+)")
    if baseline_hpwl is None:
        print(f"  FAIL — could not parse baseline HPWL from output:")
        print(result.stdout)
        sys.exit(1)

    # place always writes placement_out.txt in cwd
    src = PROJECT_DIR / "placement_out.txt"
    if src.exists():
        shutil.copy(src, baseline_out)
        print(f"  OK  Baseline HPWL = {baseline_hpwl:,}")
        print(f"      Saved → {baseline_out.relative_to(PROJECT_DIR)}")
    else:
        print("  WARN — placement_out.txt not found after place command")

    # ------------------------------------------------------------------
    # Step 3 — SA placement
    # ------------------------------------------------------------------
    print(f"\n[3/5] SA placement (this may take a moment)...")
    sa_cmd = (
        f"sa_place {args.bench} {sa_out_arg} "
        f"{args.seed} {args.iters} {args.t0} {args.alpha} "
        f"--cost {args.cost}"
    )
    result = run_repl_cmd(sa_cmd, cwd=PROJECT_DIR, timeout=300)
    if result.returncode != 0:
        print(f"  FAIL — sa_place returned {result.returncode}")
        print(result.stderr)
        sys.exit(1)

    sa_hpwl = extract_int(result.stdout, r"SA done: best=(\d+)")
    if sa_hpwl is None:
        print(f"  FAIL — could not parse SA HPWL from output:")
        print(result.stdout)
        sys.exit(1)

    print(f"  OK  SA best HPWL = {sa_hpwl:,}")
    print(f"      Saved → {sa_out.relative_to(PROJECT_DIR)}")

    # Copy SA logs
    logs_src = PROJECT_DIR / "logs"
    if logs_src.exists():
        logs_dst = output_dir / "logs"
        if logs_dst.exists():
            shutil.rmtree(logs_dst)
        shutil.copytree(logs_src, logs_dst)
        n_logs = len(list(logs_dst.iterdir()))
        print(f"      Logs  → {logs_dst.relative_to(PROJECT_DIR)}/ ({n_logs} files)")

    # ------------------------------------------------------------------
    # Step 4 — Legality check
    # ------------------------------------------------------------------
    print(f"\n[4/5] Legality check on outputs...")

    for label, path in [("Baseline", baseline_out), ("SA", sa_out)]:
        if not path.exists():
            print(f"  SKIP {label} — file not found")
            continue
        errs = check_placement_legality(path, bench_path)
        if errs:
            print(f"  FAIL {label} legality ({len(errs)} issue(s)):")
            for e in errs[:5]:
                print(f"       {e}")
            failed = True
        else:
            print(f"  OK   {label} placement is legal")

    # ------------------------------------------------------------------
    # Step 5 — HPWL comparison
    # ------------------------------------------------------------------
    print(f"\n[5/5] HPWL comparison...")
    improve   = baseline_hpwl - sa_hpwl
    pct       = 100.0 * improve / baseline_hpwl if baseline_hpwl > 0 else 0.0
    improved  = sa_hpwl < baseline_hpwl

    print(f"  Baseline HPWL : {baseline_hpwl:>10,}")
    print(f"  SA best HPWL  : {sa_hpwl:>10,}")
    print(f"  Improvement   : {improve:>+10,}  ({pct:+.1f}%)")

    if not improved:
        print("  WARN — SA did not improve HPWL; try --iters or --t0 values")
        failed = True
    else:
        print("  OK   SA improves over baseline")

    # ------------------------------------------------------------------
    # Save summary
    # ------------------------------------------------------------------
    with open(summary_path, "w") as f:
        f.write("T19 Smoke Test Summary\n")
        f.write("=" * 42 + "\n")
        f.write(f"Benchmark     : {bench_path.name}\n")
        f.write(f"SA params     : seed={args.seed}, iters={args.iters}, "
                f"T0={args.t0}, alpha={args.alpha}, cost={args.cost}\n")
        f.write("-" * 42 + "\n")
        f.write(f"Baseline HPWL : {baseline_hpwl}\n")
        f.write(f"SA best HPWL  : {sa_hpwl}\n")
        f.write(f"Improvement   : {improve} ({pct:.1f}%)\n")
        f.write("-" * 42 + "\n")
        f.write(f"Result        : {'PASS' if not failed else 'FAIL'}\n")
    print(f"\n  Summary → {summary_path.relative_to(PROJECT_DIR)}")

    # ------------------------------------------------------------------
    # Final verdict
    # ------------------------------------------------------------------
    print()
    print(sep)
    if failed:
        print("SMOKE TEST FAILED — see details above")
        print(sep)
        sys.exit(1)
    else:
        print("SMOKE TEST PASSED")
        print(sep)
    return 0


if __name__ == "__main__":
    main()
