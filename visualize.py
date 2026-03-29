import sys
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

SHOW_PINS = True
SHOW_PIN_LABELS = True
SHOW_NETS = True
MAX_NETS_TO_DRAW = 8   # draw only first few nets to reduce clutter


def parse_file(filename):
    with open(filename, "r") as f:
        lines = [line.strip() for line in f if line.strip() and not line.strip().startswith("#")]

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

    return grid_w, grid_h, components, nets


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


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 visualize.py placement_out.txt")
        return

    filename = sys.argv[1]
    grid_w, grid_h, components, nets = parse_file(filename)

    fig, ax = plt.subplots(figsize=(10, 8))

    # draw grid boundary
    ax.add_patch(Rectangle((0, 0), grid_w, grid_h, fill=False, linewidth=2))

    # draw components and pins
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
                    ax.text(px + 0.08, py + 0.08, f"{cid}.{pname}", fontsize=7)

    # draw nets as pin-to-pin chain
    if SHOW_NETS:
        for net_id, refs in nets[:MAX_NETS_TO_DRAW]:
            draw_net_chain(ax, components, net_id, refs)

    ax.set_xlim(-1, grid_w + 1)
    ax.set_ylim(-1, grid_h + 1)
    ax.set_aspect("equal")
    ax.set_title("Placement Visualization (Pin-to-Pin Nets)")
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.grid(True, alpha=0.3, linewidth=0.6)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()