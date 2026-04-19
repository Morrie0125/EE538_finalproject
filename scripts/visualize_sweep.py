import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt


REQUIRED_RESULT_COLUMNS = {
    "level",
    "T0",
    "alpha",
    "steps_per_T",
    "best_cost",
    "improvement_pct",
    "runtime_ms",
    "heuristic_mode",
}

FIXED_STEPS_PER_T_DEFAULT = 200


def parse_bool_text(value):
    return str(value).strip().lower() in {"1", "true", "yes", "y"}


def read_results_csv(path):
    rows = []
    with open(path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        missing = REQUIRED_RESULT_COLUMNS.difference(reader.fieldnames or [])
        if missing:
            missing_text = ", ".join(sorted(missing))
            raise ValueError("results CSV missing columns: {0}".format(missing_text))

        for raw in reader:
            try:
                rows.append(
                    {
                        "level": raw["level"].strip(),
                        "t0": float(raw["T0"]),
                        "alpha": float(raw["alpha"]),
                        "steps": int(float(raw["steps_per_T"])),
                        "best_cost": float(raw["best_cost"]),
                        "improvement": float(raw["improvement_pct"]),
                        "runtime_ms": float(raw["runtime_ms"]),
                        "heuristic_mode": raw["heuristic_mode"].strip(),
                        "legality_ok": parse_bool_text(raw.get("legality_ok", "true")),
                    }
                )
            except Exception as exc:
                raise ValueError("failed to parse row: {0}".format(exc)) from exc

    if not rows:
        raise ValueError("results CSV is empty: {0}".format(path))
    return rows


def maybe_read_summary_csv(path):
    if path is None:
        return []
    p = Path(path)
    if not p.exists():
        return []

    rows = []
    with open(p, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for raw in reader:
            rows.append(raw)
    return rows


def constant_marker_sizes(count, size=120.0):
    return [size for _ in range(count)]


def filter_rows(rows, level_filter, fixed_steps):
    return [
        r
        for r in rows
        if (level_filter == "all" or r["level"] == level_filter)
        and (fixed_steps is None or r["steps"] == fixed_steps)
    ]


def aggregate_by_params(rows):
    grouped = {}
    for r in rows:
        key = (r["t0"], r["alpha"], r["heuristic_mode"])
        if key not in grouped:
            grouped[key] = {
                "t0": r["t0"],
                "alpha": r["alpha"],
                "heuristic_mode": r["heuristic_mode"],
                "best_cost": float("inf"),
                "sum_best_cost": 0.0,
                "best_improvement": float("-inf"),
                "sum_improvement": 0.0,
                "count": 0,
                "sum_runtime_ms": 0.0,
                "legal_count": 0,
                "levels": set(),
            }

        g = grouped[key]
        g["best_cost"] = min(g["best_cost"], r["best_cost"])
        g["sum_best_cost"] += r["best_cost"]
        g["best_improvement"] = max(g["best_improvement"], r["improvement"])
        g["sum_improvement"] += r["improvement"]
        g["count"] += 1
        g["sum_runtime_ms"] += r["runtime_ms"]
        g["legal_count"] += 1 if r["legality_ok"] else 0
        g["levels"].add(r["level"])

    stats = []
    for _, g in grouped.items():
        count = max(1, g["count"])
        stats.append(
            {
                "t0": g["t0"],
                "alpha": g["alpha"],
                "heuristic_mode": g["heuristic_mode"],
                "best_cost": g["best_cost"],
                "mean_best_cost": g["sum_best_cost"] / float(count),
                "best_improvement": g["best_improvement"],
                "mean_improvement": g["sum_improvement"] / float(count),
                "mean_runtime_ms": g["sum_runtime_ms"] / float(count),
                "legality_pass_rate": 100.0 * g["legal_count"] / float(count),
                "runs": g["count"],
                "levels": ",".join(sorted(g["levels"])),
            }
        )

    stats.sort(
        key=lambda x: (
            x["best_improvement"],
            x["mean_improvement"],
            -x["mean_runtime_ms"],
        ),
        reverse=True,
    )
    return stats


def write_rank_csv(path, stats, topk, fixed_steps):
    columns = [
        "rank",
        "t0",
        "alpha",
        "steps_per_T",
        "heuristic_mode",
        "best_cost",
        "mean_best_cost",
        "best_improvement_pct",
        "mean_improvement_pct",
        "mean_runtime_ms",
        "legality_pass_rate",
        "runs",
        "levels",
    ]
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=columns)
        writer.writeheader()
        for idx, row in enumerate(stats[:topk], start=1):
            writer.writerow(
                {
                    "rank": idx,
                    "t0": "{0:.6f}".format(row["t0"]),
                    "alpha": "{0:.6f}".format(row["alpha"]),
                    "steps_per_T": fixed_steps,
                    "heuristic_mode": row["heuristic_mode"],
                    "best_cost": "{0:.3f}".format(row["best_cost"]),
                    "mean_best_cost": "{0:.3f}".format(row["mean_best_cost"]),
                    "best_improvement_pct": "{0:.6f}".format(row["best_improvement"]),
                    "mean_improvement_pct": "{0:.6f}".format(row["mean_improvement"]),
                    "mean_runtime_ms": "{0:.3f}".format(row["mean_runtime_ms"]),
                    "legality_pass_rate": "{0:.4f}".format(row["legality_pass_rate"]),
                    "runs": row["runs"],
                    "levels": row["levels"],
                }
            )


def plot_topk_best(stats, out_dir, topk):
    top = stats[:topk]
    labels = [
        "T0={0:.1f}\na={1:.3f}".format(r["t0"], r["alpha"])
        for r in top
    ]
    values = [r["best_improvement"] for r in top]

    fig, ax = plt.subplots(figsize=(max(10, 0.8 * len(top)), 6))
    bars = ax.bar(range(len(top)), values)
    ax.set_xticks(range(len(top)))
    ax.set_xticklabels(labels, rotation=45, ha="right")
    ax.set_ylabel("best improvement_pct")
    ax.set_title("Top-{0} SA Params by Best Improvement".format(len(top)))
    ax.grid(True, axis="y", alpha=0.3)

    for bar, val in zip(bars, values):
        ax.text(bar.get_x() + bar.get_width() / 2.0, val, "{0:.2f}".format(val), ha="center", va="bottom", fontsize=8)

    fig.tight_layout()
    target = out_dir / "topk_best_improvement.png"
    fig.savefig(target, dpi=160)
    return target, fig


def plot_param_space_best(stats, out_dir):
    x = [r["t0"] for r in stats]
    y = [r["alpha"] for r in stats]
    c = [r["best_improvement"] for r in stats]
    sizes = constant_marker_sizes(len(stats))

    fig, ax = plt.subplots(figsize=(8, 6))
    sc = ax.scatter(x, y, c=c, s=sizes, cmap="viridis", alpha=0.85)
    cbar = fig.colorbar(sc, ax=ax)
    cbar.set_label("best_improvement_pct")

    ax.set_title("Best Improvement in SA Parameter Space")
    ax.set_xlabel("T0")
    ax.set_ylabel("alpha")
    ax.grid(True, alpha=0.3)

    fig.tight_layout()
    target = out_dir / "param_space_best_improvement.png"
    fig.savefig(target, dpi=160)
    return target, fig


def plot_param_space_3d_best_cost(stats, out_dir):
    x = [r["t0"] for r in stats]
    y = [r["alpha"] for r in stats]
    z = [r["best_cost"] for r in stats]

    fig = plt.figure(figsize=(9, 7))
    ax = fig.add_subplot(111, projection="3d")
    sc = ax.scatter(x, y, z, c=z, cmap="viridis_r", s=56, alpha=0.9)

    ax.set_title("3D Sweep: T0 / alpha / best_cost")
    ax.set_xlabel("T0")
    ax.set_ylabel("alpha")
    ax.set_zlabel("best_cost")

    cbar = fig.colorbar(sc, ax=ax, pad=0.1, shrink=0.8)
    cbar.set_label("best_cost (lower is better)")

    fig.tight_layout()
    target = out_dir / "param_space_3d_best_cost.png"
    fig.savefig(target, dpi=170)
    return target, fig


def print_text_summary(filtered_rows, stats, summary_rows, level_filter, fixed_steps):
    legal_count = sum(1 for r in filtered_rows if r["legality_ok"])
    legal_rate = 100.0 * legal_count / float(len(filtered_rows)) if filtered_rows else 0.0

    best = stats[0]
    print("rows (after filters): {0}".format(len(filtered_rows)))
    print("level filter: {0}".format(level_filter))
    print("steps_per_T filter: {0}".format(fixed_steps))
    print("legality pass rate: {0:.2f}%".format(legal_rate))
    print(
        "best params: T0={0:.3f}, alpha={1:.5f}, mode={2}, best_cost={3:.3f}".format(
            best["t0"], best["alpha"], best["heuristic_mode"], best["best_cost"]
        )
    )
    print(
        "best improvement: {0:.4f}% | mean improvement: {1:.4f}% | mean runtime: {2:.2f} ms".format(
            best["best_improvement"], best["mean_improvement"], best["mean_runtime_ms"]
        )
    )

    if summary_rows:
        print("summary rows: {0}".format(len(summary_rows)))


def build_arg_parser():
    parser = argparse.ArgumentParser(description="Visualize sa_test sweep CSV outputs")
    parser.add_argument("--results", default="sweeps/results.csv", help="Path to results.csv")
    parser.add_argument("--summary", default="sweeps/summary.csv", help="Path to summary.csv")
    parser.add_argument("--out", default="sweeps/plots", help="Output directory for figures")
    parser.add_argument("--level", default="all", choices=["all", "easy", "mid", "hard"], help="Filter rows by level")
    parser.add_argument("--steps", type=int, default=FIXED_STEPS_PER_T_DEFAULT, help="Filter steps_per_T (default: 200)")
    parser.add_argument("--topk", type=int, default=20, help="Top-k parameter sets to export/plot")
    parser.add_argument("--show", action="store_true", help="Display figures interactively")
    return parser


def main():
    args = build_arg_parser().parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    rows = read_results_csv(args.results)
    summary_rows = maybe_read_summary_csv(args.summary)
    filtered_rows = filter_rows(rows, args.level, args.steps)
    stats = aggregate_by_params(filtered_rows)
    if not stats:
        raise ValueError("no rows matched filters: level={0}, steps={1}".format(args.level, args.steps))

    topk = max(1, args.topk)
    rank_csv_path = out_dir / "top_params_by_best_improvement.csv"
    write_rank_csv(rank_csv_path, stats, topk, args.steps)

    fig_paths = []
    fig_refs = []

    fig_path, fig_ref = plot_topk_best(stats, out_dir, topk)
    fig_paths.append(fig_path)
    fig_refs.append(fig_ref)

    fig_path, fig_ref = plot_param_space_best(stats, out_dir)
    fig_paths.append(fig_path)
    fig_refs.append(fig_ref)

    fig_path, fig_ref = plot_param_space_3d_best_cost(stats, out_dir)
    fig_paths.append(fig_path)
    fig_refs.append(fig_ref)

    print_text_summary(filtered_rows, stats, summary_rows, args.level, args.steps)
    print("rank csv:")
    print("- {0}".format(rank_csv_path))
    print("saved figures:")
    for p in fig_paths:
        print("- {0}".format(p))

    if args.show:
        plt.show()
    else:
        for fig in fig_refs:
            plt.close(fig)


if __name__ == "__main__":
    main()
