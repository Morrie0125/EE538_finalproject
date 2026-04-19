import sys
import re
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.patches import Rectangle

SHOW_PINS = True
SHOW_PIN_LABELS = True
SHOW_NETS = True
MAX_NETS_TO_DRAW = 8   # draw only first few nets to reduce clutter


def parse_file(filename):
    with open(filename, "r") as f:
        raw_lines = [line.strip() for line in f if line.strip()]

    lines = []
    hpwl = None
    for line in raw_lines:
        if line.startswith("#"):
            if line.startswith("# cost:"):
                try:
                    hpwl = int(line.split(":", 1)[1].strip())
                except ValueError:
                    hpwl = None
            continue
        lines.append(line)

    grid_w = grid_h = None
    components = {}
    nets = []

    i = 0
    while i < len(lines):
        tokens = lines[i].split()

        if tokens[0] == "GRID":
            grid_w = int(tokens[1])
            grid_h = int(tokens[2])
            i += 1

        elif tokens[0] == "COMPONENTS":
            count = int(tokens[1])
            i += 1
            for _ in range(count):
                t = lines[i].split()
                # COMPONENT <id> <w> <h> <fixed|movable> <x> <y>
                cid = t[1]
                w = int(t[2])
                h = int(t[3])
                ctype = t[4]
                if len(t) < 7:
                    raise ValueError(
                        "Component '{0}' has no coordinates. "
                        "Please visualize a placement output, not a raw netlist.".format(cid)
                    )
                x = int(t[5])
                y = int(t[6])
                components[cid] = {
                    "w": w,
                    "h": h,
                    "type": ctype,
                    "x": x,
                    "y": y,
                    "pins": {}
                }
                i += 1

        elif tokens[0] == "PINS":
            count = int(tokens[1])
            i += 1
            for _ in range(count):
                t = lines[i].split()
                # PIN <comp_id> <pin_name> <dx> <dy>
                cid = t[1]
                pname = t[2]
                dx = int(t[3])
                dy = int(t[4])
                components[cid]["pins"][pname] = (dx, dy)
                i += 1

        elif tokens[0] == "NETS":
            count = int(tokens[1])
            i += 1
            for _ in range(count):
                t = lines[i].split()
                net_id = t[1]
                degree = int(t[2])
                refs = t[3:3 + degree]
                nets.append((net_id, refs))
                i += 1
        else:
            i += 1

    return grid_w, grid_h, components, nets, hpwl


def get_abs_pin(components, ref):
    cid, pname = ref.split(".")
    comp = components[cid]
    dx, dy = comp["pins"][pname]
    return comp["x"] + dx, comp["y"] + dy


def draw_net_chain(ax, components, net_id, refs):
    # Convert refs to absolute pin positions
    points = []
    for ref in refs:
        px, py = get_abs_pin(components, ref)
        points.append((ref, px, py))

    # Sort for a cleaner polyline: left-to-right, then bottom-to-top
    points.sort(key=lambda item: (item[1], item[2]))

    # Draw pin-to-pin chain
    for i in range(len(points) - 1):
        _, x1, y1 = points[i]
        _, x2, y2 = points[i + 1]
        ax.plot([x1, x2], [y1, y2], linewidth=1.0)

    # Put net label near the middle point
    mid = len(points) // 2
    _, mx, my = points[mid]
    ax.text(mx + 0.15, my + 0.15, net_id, fontsize=7)


def draw_layout(ax, grid_w, grid_h, components, nets, hpwl=None, title_suffix=""):
    ax.clear()
    ax.add_patch(Rectangle((0, 0), grid_w, grid_h, fill=False, linewidth=2))

    for cid, comp in components.items():
        x, y = comp["x"], comp["y"]
        w, h = comp["w"], comp["h"]
        fixed = (comp["type"] == "fixed")

        rect = Rectangle(
            (x, y), w, h,
            fill=False,
            linewidth=2,
            linestyle="-" if fixed else "--"
        )
        ax.add_patch(rect)

        ax.text(
            x + w / 2,
            y + h / 2,
            cid,
            ha="center",
            va="center",
            fontsize=9
        )

        if SHOW_PINS:
            for pname, (dx, dy) in comp["pins"].items():
                px = x + dx
                py = y + dy
                ax.plot(px, py, marker="o", markersize=3)
                if SHOW_PIN_LABELS:
                    ax.text(px + 0.08, py + 0.08, "{0}.{1}".format(cid, pname), fontsize=7)

    if SHOW_NETS:
        for net_id, refs in nets[:MAX_NETS_TO_DRAW]:
            draw_net_chain(ax, components, net_id, refs)

    ax.set_xlim(-1, grid_w + 1)
    ax.set_ylim(-1, grid_h + 1)
    ax.set_aspect("equal")
    base_title = "Placement Visualization"
    if title_suffix:
        base_title += " - " + title_suffix
    ax.set_title(base_title)
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.grid(True, alpha=0.3, linewidth=0.6)

    hpwl_text = "HPWL: N/A" if hpwl is None else "HPWL: {0}".format(hpwl)
    ax.text(
        0.02,
        0.98,
        hpwl_text,
        transform=ax.transAxes,
        va="top",
        ha="left",
        fontsize=10,
        bbox={"facecolor": "white", "alpha": 0.8, "edgecolor": "black"}
    )


def stage_index_from_path(path):
    m = re.search(r"stage_(\d+)_best", path.name)
    if not m:
        return -1
    return int(m.group(1))


def run_demo_mode():
    snap_dir = Path("demo") / "snaps"
    if not snap_dir.exists():
        print("Demo snapshots not found: {0}".format(snap_dir))
        print("Run: sa_place ... --demo first.")
        return

    files = sorted(snap_dir.glob("stage_*_best.txt"), key=stage_index_from_path)
    if not files:
        print("No demo snapshots found in {0}".format(snap_dir))
        print("Run: sa_place ... --demo first.")
        return

    frames = []
    for p in files:
        gw, gh, comps, nets, hpwl = parse_file(str(p))
        frames.append({
            "stage": stage_index_from_path(p),
            "grid_w": gw,
            "grid_h": gh,
            "components": comps,
            "nets": nets,
            "hpwl": hpwl,
        })

    fig, ax = plt.subplots(figsize=(10, 8))

    def update(frame_idx):
        fr = frames[frame_idx]
        draw_layout(
            ax,
            fr["grid_w"],
            fr["grid_h"],
            fr["components"],
            fr["nets"],
            fr["hpwl"],
            title_suffix="Demo Stage {0}".format(fr["stage"])
        )
        return []

    anim = FuncAnimation(fig, update, frames=len(frames), interval=500, repeat=True)
    _ = anim
    plt.tight_layout()
    plt.show()


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 visualize.py [placement_out.txt|--demo]")
        return

    if sys.argv[1] == "--demo":
        run_demo_mode()
        return

    filename = sys.argv[1]
    try:
        grid_w, grid_h, components, nets, hpwl = parse_file(filename)
    except Exception as e:
        print("Failed to parse placement file: {0}".format(e))
        return

    fig, ax = plt.subplots(figsize=(10, 8))
    draw_layout(ax, grid_w, grid_h, components, nets, hpwl, title_suffix="Single Snapshot")

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()