import sys
import re
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.collections import LineCollection, PatchCollection
from matplotlib.patches import Patch, Rectangle

SHOW_PINS = True
SHOW_PIN_LABELS = True
SHOW_NETS = True
MAX_NETS_TO_DRAW = 8   # draw only first few nets to reduce clutter
FIXED_FACE_COLOR = "#f2a3a3"
MOVABLE_FACE_COLOR = "none"
FIXED_EDGE_COLOR = "#b14747"
MOVABLE_EDGE_COLOR = "black"
DEMO_BASE_INTERVAL_MS = 500


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


def collect_net_segments(components, nets):
    segments = []
    for _, refs in nets[:MAX_NETS_TO_DRAW]:
        points = []
        for ref in refs:
            px, py = get_abs_pin(components, ref)
            points.append((px, py))

        points.sort(key=lambda item: (item[0], item[1]))
        for i in range(len(points) - 1):
            x1, y1 = points[i]
            x2, y2 = points[i + 1]
            segments.append([(x1, y1), (x2, y2)])
    return segments


def make_component_patches(components, component_ids):
    patches = []
    for cid in component_ids:
        comp = components[cid]
        patches.append(Rectangle((comp["x"], comp["y"]), comp["w"], comp["h"]))
    return patches


def component_label_pos(comp):
    return comp["x"] + comp["w"] / 2.0, comp["y"] + comp["h"] / 2.0


def collect_pin_refs(components, component_ids):
    refs = []
    for cid in component_ids:
        for pname in components[cid]["pins"].keys():
            refs.append((cid, pname))
    return refs


def pin_offset_list(components, pin_refs):
    out = []
    for cid, pname in pin_refs:
        comp = components[cid]
        dx, dy = comp["pins"][pname]
        out.append((comp["x"] + dx, comp["y"] + dy))
    return out


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


def draw_layout(ax, grid_w, grid_h, components, nets, hpwl=None, title_suffix="", info_ax=None):
    ax.clear()
    ax.add_patch(Rectangle((0, 0), grid_w, grid_h, fill=False, linewidth=2))

    for cid, comp in components.items():
        x, y = comp["x"], comp["y"]
        w, h = comp["w"], comp["h"]
        fixed = (comp["type"] == "fixed")
        face_color = FIXED_FACE_COLOR if fixed else MOVABLE_FACE_COLOR
        edge_color = FIXED_EDGE_COLOR if fixed else MOVABLE_EDGE_COLOR

        rect = Rectangle(
            (x, y), w, h,
            fill=fixed,
            facecolor=face_color,
            edgecolor=edge_color,
            alpha=0.55,
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

    if info_ax is not None:
        info_ax.clear()
        info_ax.axis("off")
        legend_handles = [
            Patch(facecolor=FIXED_FACE_COLOR, edgecolor=FIXED_EDGE_COLOR, alpha=0.55, label="Fixed"),
            Patch(facecolor="none", edgecolor=MOVABLE_EDGE_COLOR, alpha=1.0, label="Movable"),
        ]
        info_ax.legend(handles=legend_handles, loc="upper left")
        hpwl_text = format_hpwl_text(hpwl, hpwl)
        info_ax.text(
            0.02,
            0.78,
            hpwl_text,
            transform=info_ax.transAxes,
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


def format_hpwl_text(current_hpwl, initial_hpwl):
    if current_hpwl is None:
        return "HPWL: N/A"
    if initial_hpwl is None or initial_hpwl <= 0:
        return "HPWL: {0}".format(current_hpwl)

    ratio_pct = 100.0 * float(current_hpwl) / float(initial_hpwl)
    return "HPWL: {0} ({1:.1f}% of initial)".format(current_hpwl, ratio_pct)


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

    filtered_frames = []
    last_hpwl = None
    for idx, fr in enumerate(frames):
        if idx == 0 or fr["hpwl"] != last_hpwl:
            filtered_frames.append(fr)
            last_hpwl = fr["hpwl"]

    if not filtered_frames:
        print("No valid demo frames found after filtering.")
        return

    skipped_count = len(frames) - len(filtered_frames)
    frames = filtered_frames
    if skipped_count > 0:
        print("Skipped {0} unchanged stage snapshots (same HPWL).".format(skipped_count))

    fig, (ax, info_ax) = plt.subplots(
        1,
        2,
        figsize=(12, 8),
        gridspec_kw={"width_ratios": [4.0, 1.4]}
    )
    frame_count = len(frames)
    interval_ms = DEMO_BASE_INTERVAL_MS
    initial_hpwl = frames[0]["hpwl"]

    first = frames[0]
    grid_w = first["grid_w"]
    grid_h = first["grid_h"]
    component_ids = list(first["components"].keys())
    fixed_flags = [first["components"][cid]["type"] == "fixed" for cid in component_ids]

    ax.add_patch(Rectangle((0, 0), grid_w, grid_h, fill=False, linewidth=2))
    ax.set_xlim(-1, grid_w + 1)
    ax.set_ylim(-1, grid_h + 1)
    ax.set_aspect("equal")
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.grid(True, alpha=0.3, linewidth=0.6)

    info_ax.axis("off")
    legend_handles = [
        Patch(facecolor=FIXED_FACE_COLOR, edgecolor=FIXED_EDGE_COLOR, alpha=0.55, label="Fixed"),
        Patch(facecolor="none", edgecolor=MOVABLE_EDGE_COLOR, alpha=1.0, label="Movable"),
    ]
    info_ax.legend(handles=legend_handles, loc="upper left")

    patches = make_component_patches(first["components"], component_ids)
    comp_collection = PatchCollection(patches, match_original=False, linewidths=2, alpha=0.55)
    comp_collection.set_facecolor([FIXED_FACE_COLOR if fixed else "none" for fixed in fixed_flags])
    comp_collection.set_edgecolor([FIXED_EDGE_COLOR if fixed else MOVABLE_EDGE_COLOR for fixed in fixed_flags])
    ax.add_collection(comp_collection)

    comp_texts = []
    for cid in component_ids:
        cx, cy = component_label_pos(first["components"][cid])
        comp_texts.append(ax.text(cx, cy, cid, ha="center", va="center", fontsize=9))

    pin_refs = collect_pin_refs(first["components"], component_ids) if SHOW_PINS else []
    pin_scatter = None
    pin_texts = []
    if SHOW_PINS:
        pin_scatter = ax.scatter([], [], s=10, c="black", marker="o")
        first_offsets = pin_offset_list(first["components"], pin_refs)
        pin_scatter.set_offsets(first_offsets)
        if SHOW_PIN_LABELS:
            for (cid, pname), (px, py) in zip(pin_refs, first_offsets):
                pin_texts.append(ax.text(px + 0.08, py + 0.08, "{0}.{1}".format(cid, pname), fontsize=7))

    net_collection = None
    if SHOW_NETS:
        net_collection = LineCollection([], linewidths=1.0, colors="tab:blue", alpha=0.9)
        net_collection.set_segments(collect_net_segments(first["components"], first["nets"]))
        ax.add_collection(net_collection)

    title_text = ax.set_title("Placement Visualization")
    stage_text = info_ax.text(
        0.02,
        0.78,
        "Stage: {0}".format(first["stage"]),
        transform=info_ax.transAxes,
        va="top",
        ha="left",
        fontsize=10,
        bbox={"facecolor": "white", "alpha": 0.8, "edgecolor": "black"}
    )
    hpwl_text = info_ax.text(
        0.02,
        0.66,
        format_hpwl_text(first["hpwl"], initial_hpwl),
        transform=info_ax.transAxes,
        va="top",
        ha="left",
        fontsize=10,
        bbox={"facecolor": "white", "alpha": 0.8, "edgecolor": "black"}
    )

    animated_artists = [comp_collection, title_text, stage_text, hpwl_text]
    animated_artists.extend(comp_texts)
    if pin_scatter is not None:
        animated_artists.append(pin_scatter)
    animated_artists.extend(pin_texts)
    if net_collection is not None:
        animated_artists.append(net_collection)

    for artist in animated_artists:
        if hasattr(artist, "set_animated"):
            artist.set_animated(True)

    def update(frame_idx):
        fr = frames[frame_idx]
        comps = fr["components"]

        comp_collection.set_paths(make_component_patches(comps, component_ids))
        for text_artist, cid in zip(comp_texts, component_ids):
            cx, cy = component_label_pos(comps[cid])
            text_artist.set_position((cx, cy))

        if pin_scatter is not None:
            offsets = pin_offset_list(comps, pin_refs)
            pin_scatter.set_offsets(offsets)
            if SHOW_PIN_LABELS:
                for text_artist, (cid, pname), (px, py) in zip(pin_texts, pin_refs, offsets):
                    text_artist.set_position((px + 0.08, py + 0.08))
                    text_artist.set_text("{0}.{1}".format(cid, pname))

        if net_collection is not None:
            net_collection.set_segments(collect_net_segments(comps, fr["nets"]))

        stage_text.set_text("Stage: {0}".format(fr["stage"]))
        hpwl_text.set_text(format_hpwl_text(fr["hpwl"], initial_hpwl))
        return animated_artists

    anim = FuncAnimation(fig, update, frames=frame_count, interval=interval_ms, repeat=True, blit=True)
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

    fig, (ax, info_ax) = plt.subplots(
        1,
        2,
        figsize=(12, 8),
        gridspec_kw={"width_ratios": [4.0, 1.4]}
    )
    draw_layout(ax, grid_w, grid_h, components, nets, hpwl, title_suffix="Single Snapshot", info_ax=info_ax)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()