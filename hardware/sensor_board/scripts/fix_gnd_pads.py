"""
Fix GND pad connectivity: Add via+track combos near each unconnected GND pad.
Each via connects through In1.Cu GND plane, bypassing the zone fill gap.

Unconnected GND pads:
  U1: pads 3(9.60,10.65), 8(10.40,12.35), 9(10.85,11.70)
  U2: pads 3(9.60,15.65), 5(9.15,16.70), 8(10.40,17.35), 9(10.85,16.70)
  U3: pads 1(8.74,20.75), 6(10.00,22.51)
  U4: pads 1(15.25,26.34), 2(15.11,27.25), 5(15.11,28.75),
      8(16.25,29.66), 10(17.25,29.66), 12(18.25,29.66), 13(18.75,29.66),
      17(19.89,28.25), 21(19.25,26.34), 26(16.75,26.34)

Run with: "C:\Program Files\KiCad\10.0\bin\python.exe" fix_gnd_pads.py
"""

import pcbnew
import os
import math

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_DIR   = os.path.dirname(SCRIPT_DIR)
PCB_FILE   = os.path.join(BASE_DIR, "project", "sensor_board.kicad_pcb")

MM  = pcbnew.FromMM
TMM = pcbnew.ToMM

def vec(x_mm, y_mm):
    return pcbnew.VECTOR2I(MM(x_mm), MM(y_mm))

def get_net(board, net_name):
    return board.GetNetInfo().GetNetItem(net_name)

def add_via(board, x, y, net_name, drill=0.3, size=0.6):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(vec(x, y))
    v.SetDrill(MM(drill))
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    net = get_net(board, net_name)
    if net:
        v.SetNet(net)
    try:
        v.SetWidth(pcbnew.F_Cu, MM(size))
    except Exception:
        try:
            v.SetWidth(MM(size))
        except Exception:
            pass
    board.Add(v)

def add_track(board, x1, y1, x2, y2, net_name, layer=pcbnew.F_Cu, width=0.15):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(vec(x1, y1))
    t.SetEnd(vec(x2, y2))
    t.SetWidth(MM(width))
    t.SetLayer(layer)
    net = get_net(board, net_name)
    if net:
        t.SetNet(net)
    board.Add(t)


def check_clearance(board, vx, vy, via_r=0.30, min_clear=0.15):
    """Check if a via at (vx,vy) has clearance from all non-GND tracks."""
    for item in board.GetTracks():
        if isinstance(item, pcbnew.PCB_TRACK) and not isinstance(item, pcbnew.PCB_VIA):
            net = item.GetNet()
            nn = net.GetNetname() if net else ""
            if nn == "GND":
                continue
            if item.GetLayer() != pcbnew.F_Cu:
                continue
            # Check distance from via center to track segment
            s = item.GetStart()
            e = item.GetEnd()
            sx, sy = TMM(s.x), TMM(s.y)
            ex, ey = TMM(e.x), TMM(e.y)
            w = TMM(item.GetWidth()) / 2.0
            d = point_to_segment_dist(vx, vy, sx, sy, ex, ey)
            clearance = d - via_r - w
            if clearance < min_clear:
                return False, nn, clearance
    return True, "", 999


def point_to_segment_dist(px, py, ax, ay, bx, by):
    """Distance from point (px,py) to line segment (ax,ay)-(bx,by)."""
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.sqrt((px-ax)**2 + (py-ay)**2)
    t = max(0, min(1, ((px-ax)*dx + (py-ay)*dy) / (dx*dx + dy*dy)))
    cx = ax + t * dx
    cy = ay + t * dy
    return math.sqrt((px-cx)**2 + (py-cy)**2)


def fix():
    print(f"Loading PCB: {PCB_FILE}")
    board = pcbnew.LoadBoard(PCB_FILE)

    # Component centers for computing outward directions
    centers = {
        "U1": (10.00, 11.50),
        "U2": (10.00, 16.50),
        "U3": (10.00, 21.50),
        "U4": (17.50, 28.00),
    }

    # Unconnected GND pads: (ref, pad_num, pad_x, pad_y)
    pads = [
        ("U1", 3, 9.60, 10.65),
        ("U1", 8, 10.40, 12.35),
        ("U1", 9, 10.85, 11.70),
        ("U2", 3, 9.60, 15.65),
        ("U2", 5, 9.15, 16.70),
        ("U2", 8, 10.40, 17.35),
        ("U2", 9, 10.85, 16.70),
        ("U3", 1, 8.74, 20.75),
        ("U3", 6, 10.00, 22.51),
        ("U4", 1, 15.25, 26.34),
        ("U4", 2, 15.11, 27.25),
        ("U4", 5, 15.11, 28.75),
        ("U4", 8, 16.25, 29.66),
        ("U4", 10, 17.25, 29.66),
        ("U4", 12, 18.25, 29.66),
        ("U4", 13, 18.75, 29.66),
        ("U4", 17, 19.89, 28.25),
        ("U4", 21, 19.25, 26.34),
        ("U4", 26, 16.75, 26.34),
    ]

    added = 0
    skipped = 0

    for ref, pad_num, px, py in pads:
        cx, cy = centers[ref]
        # Direction outward from center
        dx = px - cx
        dy = py - cy
        length = math.sqrt(dx*dx + dy*dy)
        if length > 0:
            dx /= length
            dy /= length
        else:
            dx, dy = 0, 1

        # Try via positions at increasing distances
        placed = False
        for dist in [0.55, 0.70, 0.85, 1.00, 1.20]:
            vx = px + dx * dist
            vy = py + dy * dist
            # Stay inside board (0.5 to 34.5)
            vx = max(0.6, min(34.4, vx))
            vy = max(0.6, min(34.4, vy))
            ok, conflict_net, clearance = check_clearance(board, vx, vy)
            if ok:
                add_track(board, px, py, vx, vy, "GND", pcbnew.F_Cu, 0.15)
                add_via(board, vx, vy, "GND")
                print(f"  {ref}.{pad_num}: via at ({vx:.2f},{vy:.2f}) dist={dist:.2f}mm")
                added += 1
                placed = True
                break

        if not placed:
            # Try perpendicular directions
            for angle in [90, -90, 45, -45, 135, -135]:
                rad = math.radians(angle)
                cos_a = math.cos(rad)
                sin_a = math.sin(rad)
                ndx = dx * cos_a - dy * sin_a
                ndy = dx * sin_a + dy * cos_a
                for dist in [0.55, 0.70, 0.85, 1.00]:
                    vx = px + ndx * dist
                    vy = py + ndy * dist
                    vx = max(0.6, min(34.4, vx))
                    vy = max(0.6, min(34.4, vy))
                    ok, _, _ = check_clearance(board, vx, vy)
                    if ok:
                        add_track(board, px, py, vx, vy, "GND", pcbnew.F_Cu, 0.15)
                        add_via(board, vx, vy, "GND")
                        print(f"  {ref}.{pad_num}: via at ({vx:.2f},{vy:.2f}) angle={angle} dist={dist:.2f}mm")
                        added += 1
                        placed = True
                        break
                if placed:
                    break

        if not placed:
            print(f"  {ref}.{pad_num}: SKIPPED (no clear position found)")
            skipped += 1

    print(f"\nAdded {added} via+track combos, skipped {skipped}")

    # Save and fill zones
    print("\nSaving and filling zones...")
    board.Save(PCB_FILE)
    board2 = pcbnew.LoadBoard(PCB_FILE)
    filler = pcbnew.ZONE_FILLER(board2)
    filler.Fill(board2.Zones())
    board2.Save(PCB_FILE)
    print("Done!")


if __name__ == "__main__":
    fix()
