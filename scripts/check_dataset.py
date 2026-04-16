#!/usr/bin/env python3
"""
T18 - Dataset sanity checker for EE538 placement benchmarks.

Checks that a benchmark file is well-formed:
  - GRID is present and positive
  - No duplicate component IDs
  - No duplicate pin names within a component
  - Pin offsets are within component bounds
  - All components referenced by nets/pins actually exist
  - Each net has at least 2 pins
  - Net pin references (comp.pin) resolve to real components + pins
  - Fixed components have valid coordinates within grid

Usage:
    python3 scripts/check_dataset.py bench/tiny_10.txt
    python3 scripts/check_dataset.py bench/          # check all .txt in a directory
"""

import sys
import os
import re


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def parse_bench(path):
    """Parse a benchmark file. Returns (grid, nodes, nets, errors)."""
    errors = []
    grid_w, grid_h = None, None
    nodes = {}      # id -> {w, h, fixed, x, y, pins: {name -> (dx,dy)}}
    nets = []       # list of {id, refs: [(comp_id, pin_name)]}

    with open(path) as f:
        raw_lines = f.readlines()

    # Strip comments and blank lines
    lines = []
    for ln in raw_lines:
        line = ln.split("#")[0].strip()
        if line:
            lines.append(line)

    i = 0
    n_lines = len(lines)

    # --- GRID ---
    if i >= n_lines or not lines[i].startswith("GRID"):
        errors.append("Missing or malformed GRID line")
        return grid_w, grid_h, nodes, nets, errors

    tok = lines[i].split()
    if len(tok) != 3:
        errors.append(f"GRID line malformed: '{lines[i]}'")
    else:
        grid_w, grid_h = int(tok[1]), int(tok[2])
        if grid_w <= 0 or grid_h <= 0:
            errors.append(f"GRID dimensions must be positive, got {grid_w}x{grid_h}")
    i += 1

    # --- COMPONENTS ---
    if i >= n_lines or not lines[i].startswith("COMPONENTS"):
        errors.append("Missing COMPONENTS header")
        return grid_w, grid_h, nodes, nets, errors

    tok = lines[i].split()
    num_comps = int(tok[1])
    i += 1

    for _ in range(num_comps):
        if i >= n_lines:
            errors.append("Unexpected EOF while reading COMPONENT lines")
            break
        tok = lines[i].split()
        i += 1

        if tok[0] != "COMPONENT":
            errors.append(f"Expected COMPONENT, got: '{' '.join(tok)}'")
            continue
        if len(tok) not in (5, 7):
            errors.append(f"COMPONENT line has wrong token count: '{' '.join(tok)}'")
            continue

        cid, w, h, kind = tok[1], int(tok[2]), int(tok[3]), tok[4]

        if cid in nodes:
            errors.append(f"Duplicate component ID: '{cid}'")
            continue
        if w <= 0 or h <= 0:
            errors.append(f"Component '{cid}' has non-positive size ({w}x{h})")

        node = {"w": w, "h": h, "fixed": False, "x": -1, "y": -1, "pins": {}}

        if kind == "fixed":
            if len(tok) != 7:
                errors.append(f"Fixed component '{cid}' missing coordinates")
            else:
                node["fixed"] = True
                node["x"], node["y"] = int(tok[5]), int(tok[6])
                if grid_w is not None:
                    if node["x"] < 0 or node["y"] < 0 or \
                       node["x"] + w > grid_w or node["y"] + h > grid_h:
                        errors.append(
                            f"Fixed component '{cid}' at ({node['x']},{node['y']}) "
                            f"size ({w}x{h}) is out of grid {grid_w}x{grid_h}"
                        )
        elif kind != "movable":
            errors.append(f"Component '{cid}' type must be 'movable' or 'fixed', got '{kind}'")

        nodes[cid] = node

    # --- PINS ---
    if i >= n_lines or not lines[i].startswith("PINS"):
        errors.append("Missing PINS header")
        return grid_w, grid_h, nodes, nets, errors

    tok = lines[i].split()
    num_pins = int(tok[1])
    i += 1

    for _ in range(num_pins):
        if i >= n_lines:
            errors.append("Unexpected EOF while reading PIN lines")
            break
        tok = lines[i].split()
        i += 1

        if tok[0] != "PIN" or len(tok) != 5:
            errors.append(f"Malformed PIN line: '{' '.join(tok)}'")
            continue

        cid, pname, dx, dy = tok[1], tok[2], int(tok[3]), int(tok[4])

        if cid not in nodes:
            errors.append(f"PIN references unknown component '{cid}'")
            continue

        node = nodes[cid]
        if pname in node["pins"]:
            errors.append(f"Duplicate pin '{pname}' on component '{cid}'")
            continue
        if dx < 0 or dx >= node["w"] or dy < 0 or dy >= node["h"]:
            errors.append(
                f"Pin '{cid}.{pname}' offset ({dx},{dy}) out of component "
                f"bounds ({node['w']}x{node['h']})"
            )
        node["pins"][pname] = (dx, dy)

    # Warn about components with no pins
    for cid, node in nodes.items():
        if not node["pins"]:
            errors.append(f"WARNING: Component '{cid}' has no pins")

    # --- NETS ---
    if i >= n_lines or not lines[i].startswith("NETS"):
        errors.append("Missing NETS header")
        return grid_w, grid_h, nodes, nets, errors

    tok = lines[i].split()
    num_nets = int(tok[1])
    i += 1

    net_ids = set()
    for _ in range(num_nets):
        if i >= n_lines:
            errors.append("Unexpected EOF while reading NET lines")
            break
        tok = lines[i].split()
        i += 1

        if tok[0] != "NET" or len(tok) < 4:
            errors.append(f"Malformed NET line: '{' '.join(tok)}'")
            continue

        net_id, degree_str = tok[1], tok[2]
        degree = int(degree_str)

        if net_id in net_ids:
            errors.append(f"Duplicate net ID: '{net_id}'")
        net_ids.add(net_id)

        if degree < 2:
            errors.append(f"Net '{net_id}' has degree {degree} (must be >= 2)")

        expected_tokens = 3 + degree
        if len(tok) != expected_tokens:
            errors.append(
                f"Net '{net_id}' declares degree {degree} but has "
                f"{len(tok) - 3} pin refs"
            )
            continue

        refs = []
        for ref_str in tok[3:]:
            if "." not in ref_str:
                errors.append(
                    f"Net '{net_id}' pin ref '{ref_str}' missing '.' separator"
                )
                continue
            cid, pname = ref_str.split(".", 1)
            if cid not in nodes:
                errors.append(
                    f"Net '{net_id}' references unknown component '{cid}'"
                )
            elif pname not in nodes[cid]["pins"]:
                errors.append(
                    f"Net '{net_id}' references unknown pin '{cid}.{pname}'"
                )
            refs.append((cid, pname))

        nets.append({"id": net_id, "refs": refs})

    return grid_w, grid_h, nodes, nets, errors


# ---------------------------------------------------------------------------
# Stats helper
# ---------------------------------------------------------------------------

def compute_stats(nodes, nets):
    degrees = [len(net["refs"]) for net in nets]
    avg_fanout = sum(degrees) / len(degrees) if degrees else 0.0
    return {
        "num_nodes": len(nodes),
        "num_nets": len(nets),
        "avg_fanout": avg_fanout,
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def check_file(path):
    print(f"\nChecking: {path}")
    grid_w, grid_h, nodes, nets, errors = parse_bench(path)

    warnings = [e for e in errors if e.startswith("WARNING")]
    real_errors = [e for e in errors if not e.startswith("WARNING")]

    stats = compute_stats(nodes, nets)
    print(f"  Grid  : {grid_w}x{grid_h}")
    print(f"  Nodes : {stats['num_nodes']}")
    print(f"  Nets  : {stats['num_nets']}")
    print(f"  Avg fanout (net degree): {stats['avg_fanout']:.2f}")

    if warnings:
        for w in warnings:
            print(f"  [WARN]  {w}")

    if real_errors:
        for e in real_errors:
            print(f"  [ERROR] {e}")
        print(f"  RESULT: FAIL ({len(real_errors)} error(s))")
        return False
    else:
        print("  RESULT: OK")
        return True


def main():
    targets = sys.argv[1:] if len(sys.argv) > 1 else ["bench"]

    files_to_check = []
    for target in targets:
        if os.path.isdir(target):
            for fname in sorted(os.listdir(target)):
                if fname.endswith(".txt"):
                    files_to_check.append(os.path.join(target, fname))
        elif os.path.isfile(target):
            files_to_check.append(target)
        else:
            print(f"[SKIP] Not found: {target}")

    if not files_to_check:
        print("No benchmark files found.")
        sys.exit(1)

    all_ok = True
    for f in files_to_check:
        ok = check_file(f)
        if not ok:
            all_ok = False

    print("\n" + ("=" * 50))
    if all_ok:
        print("All files passed sanity check.")
    else:
        print("Some files have errors. See above.")
        sys.exit(1)


if __name__ == "__main__":
    main()
