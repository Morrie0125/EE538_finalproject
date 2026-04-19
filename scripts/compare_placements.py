import argparse
import os
import sys
from pathlib import Path

if "--no-show" in sys.argv:
    os.environ.setdefault("MPLCONFIGDIR", "/tmp/mplconfig")
    os.environ.setdefault("XDG_CACHE_HOME", "/tmp")
    os.makedirs(os.environ["MPLCONFIGDIR"], exist_ok=True)
    import matplotlib

    matplotlib.use("Agg")

import matplotlib.pyplot as plt

from visualize import draw_layout, parse_file


def title_from_path(path):
    return Path(path).stem.replace("_", " ")


def main():
    parser = argparse.ArgumentParser(
        description="Compare two placement output files side by side."
    )
    parser.add_argument("left", help="First placement output file, for example placement_out.txt")
    parser.add_argument("right", help="Second placement output file, for example hub_small_sa.txt")
    parser.add_argument("--left-title", default=None, help="Custom title for the left plot")
    parser.add_argument("--right-title", default=None, help="Custom title for the right plot")
    parser.add_argument("--save", default=None, help="Optional PNG path to save the comparison")
    parser.add_argument("--no-show", action="store_true", help="Save only; do not open a plot window")
    args = parser.parse_args()

    left = parse_file(args.left)
    right = parse_file(args.right)

    fig, axes = plt.subplots(2, 2, figsize=(14, 12), gridspec_kw={"height_ratios": [4.0, 1.0]})
    left_ax, right_ax = axes[0]
    left_info_ax, right_info_ax = axes[1]

    left_title = args.left_title or title_from_path(args.left)
    right_title = args.right_title or title_from_path(args.right)

    draw_layout(left_ax, *left, title_suffix=left_title, info_ax=left_info_ax)
    draw_layout(right_ax, *right, title_suffix=right_title, info_ax=right_info_ax)

    fig.tight_layout()

    if args.save:
        fig.savefig(args.save, dpi=160)
        print("Saved comparison to {0}".format(args.save))

    if not args.no_show:
        plt.show()


if __name__ == "__main__":
    main()
