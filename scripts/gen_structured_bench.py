#!/usr/bin/env python3
"""
T17 - Structured benchmark generator for EE538 placement project.

Generates two types of structured netlists:
  clustered  -- nodes grouped into clusters; dense intra-cluster nets + sparse inter-cluster nets
  hub        -- a few high-fanout hub nodes connected to many others

Usage:
    python3 scripts/gen_structured_bench.py            # generate all 4 defaults
    python3 scripts/gen_structured_bench.py --help
"""

import argparse
import os
import random


# ---------------------------------------------------------------------------
# Low-level writer
# ---------------------------------------------------------------------------

def write_bench(path, grid_w, grid_h, nodes, nets):
    """
    nodes: list of dict {id, w, h, fixed, x, y, pins: [{name, dx, dy}]}
    nets:  list of dict {id, refs: [(node_id, pin_name), ...]}
    """
    os.makedirs(os.path.dirname(path) if os.path.dirname(path) else ".", exist_ok=True)
    total_pins = sum(len(n["pins"]) for n in nodes)

    with open(path, "w") as f:
        f.write(f"GRID {grid_w} {grid_h}\n\n")

        f.write(f"COMPONENTS {len(nodes)}\n")
        for n in nodes:
            if n["fixed"]:
                f.write(f"COMPONENT {n['id']} {n['w']} {n['h']} fixed {n['x']} {n['y']}\n")
            else:
                f.write(f"COMPONENT {n['id']} {n['w']} {n['h']} movable\n")

        f.write(f"\nPINS {total_pins}\n")
        for n in nodes:
            for p in n["pins"]:
                f.write(f"PIN {n['id']} {p['name']} {p['dx']} {p['dy']}\n")

        f.write(f"\nNETS {len(nets)}\n")
        for net in nets:
            refs_str = " ".join(f"{nid}.{pname}" for nid, pname in net["refs"])
            f.write(f"NET {net['id']} {len(net['refs'])} {refs_str}\n")

    print(f"  Written: {path}  (grid={grid_w}x{grid_h}, "
          f"nodes={len(nodes)}, nets={len(nets)}, pins={total_pins})")


def make_unit_node(node_id):
    """1x1 movable node with a single pin at (0,0)."""
    return {
        "id": node_id,
        "w": 1, "h": 1,
        "fixed": False, "x": -1, "y": -1,
        "pins": [{"name": "P0", "dx": 0, "dy": 0}],
    }


# ---------------------------------------------------------------------------
# Clustered benchmark
# ---------------------------------------------------------------------------

def gen_clustered(path, grid_w, grid_h, num_nodes, num_nets,
                  num_clusters, intra_ratio=0.80, seed=42):
    """
    num_nodes   -- total movable nodes (all unit-size)
    num_nets    -- total nets to generate
    num_clusters-- number of logical clusters
    intra_ratio -- fraction of nets that are intra-cluster
    seed        -- RNG seed for reproducibility
    """
    rng = random.Random(seed)

    nodes = [make_unit_node(f"U{i}") for i in range(num_nodes)]

    # Assign each node to a cluster
    cluster_members = [[] for _ in range(num_clusters)]
    for i, node in enumerate(nodes):
        cluster_members[i % num_clusters].append(i)

    nets = []
    num_intra = int(num_nets * intra_ratio)
    num_inter = num_nets - num_intra

    # Intra-cluster nets: pick a random cluster, sample 2-4 nodes from it
    for ni in range(num_intra):
        while True:
            cl = rng.randrange(num_clusters)
            members = cluster_members[cl]
            if len(members) < 2:
                continue
            degree = min(rng.randint(2, 4), len(members))
            chosen = rng.sample(members, degree)
            refs = [(nodes[ci]["id"], "P0") for ci in chosen]
            nets.append({"id": f"N{ni}", "refs": refs})
            break

    # Inter-cluster nets: pick 2 different clusters, 1 node each
    for ni in range(num_inter):
        net_id = f"N{num_intra + ni}"
        cl_pair = rng.sample(range(num_clusters), 2)
        refs = []
        for cl in cl_pair:
            ci = rng.choice(cluster_members[cl])
            refs.append((nodes[ci]["id"], "P0"))
        nets.append({"id": net_id, "refs": refs})

    rng.shuffle(nets)
    write_bench(path, grid_w, grid_h, nodes, nets)


# ---------------------------------------------------------------------------
# Hub-heavy benchmark
# ---------------------------------------------------------------------------

def gen_hub(path, grid_w, grid_h, num_nodes, num_nets,
            num_hubs, hub_net_ratio=0.50, hub_degree_range=(4, 7), seed=99):
    """
    num_nodes        -- total movable nodes (all unit-size)
    num_nets         -- total nets to generate
    num_hubs         -- how many high-fanout hub nodes
    hub_net_ratio    -- fraction of nets that include at least one hub
    hub_degree_range -- (min, max) degree for hub-containing nets
    seed             -- RNG seed
    """
    rng = random.Random(seed)

    nodes = [make_unit_node(f"U{i}") for i in range(num_nodes)]

    hub_indices = list(range(num_hubs))           # first num_hubs nodes are hubs
    regular_indices = list(range(num_hubs, num_nodes))

    nets = []
    num_hub_nets = int(num_nets * hub_net_ratio)
    num_reg_nets = num_nets - num_hub_nets

    # Hub nets: pick one hub + several regular nodes
    for ni in range(num_hub_nets):
        hub_ci = rng.choice(hub_indices)
        degree = rng.randint(*hub_degree_range)
        # at least 1 slot for hub itself; fill rest with regulars
        num_reg = min(degree - 1, len(regular_indices))
        regulars = rng.sample(regular_indices, num_reg) if num_reg > 0 else []
        chosen = [hub_ci] + regulars
        refs = [(nodes[ci]["id"], "P0") for ci in chosen]
        nets.append({"id": f"N{ni}", "refs": refs})

    # Regular nets: degree 2-3 among non-hub nodes (fallback to all if too few)
    pool = regular_indices if len(regular_indices) >= 2 else list(range(num_nodes))
    for ni in range(num_reg_nets):
        net_id = f"N{num_hub_nets + ni}"
        degree = min(rng.randint(2, 3), len(pool))
        chosen = rng.sample(pool, degree)
        refs = [(nodes[ci]["id"], "P0") for ci in chosen]
        nets.append({"id": net_id, "refs": refs})

    rng.shuffle(nets)
    write_bench(path, grid_w, grid_h, nodes, nets)


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Generate structured EE538 placement benchmarks (T17)."
    )
    parser.add_argument("--outdir", default="bench",
                        help="Output directory for benchmark files (default: bench/)")
    args = parser.parse_args()

    d = args.outdir
    print("Generating structured benchmarks ...")

    # --- Clustered ---
    gen_clustered(
        path=f"{d}/clustered_small.txt",
        grid_w=15, grid_h=15,
        num_nodes=20, num_nets=30,
        num_clusters=4,
        intra_ratio=0.80,
        seed=2001,
    )
    gen_clustered(
        path=f"{d}/clustered_medium.txt",
        grid_w=25, grid_h=25,
        num_nodes=50, num_nets=80,
        num_clusters=6,
        intra_ratio=0.80,
        seed=2002,
    )

    # --- Hub-heavy ---
    gen_hub(
        path=f"{d}/hub_small.txt",
        grid_w=15, grid_h=15,
        num_nodes=20, num_nets=30,
        num_hubs=3,
        hub_net_ratio=0.50,
        hub_degree_range=(4, 6),
        seed=3001,
    )
    gen_hub(
        path=f"{d}/hub_medium.txt",
        grid_w=25, grid_h=25,
        num_nodes=50, num_nets=80,
        num_hubs=5,
        hub_net_ratio=0.50,
        hub_degree_range=(5, 8),
        seed=3002,
    )

    print("Done.")


if __name__ == "__main__":
    main()
