"""
Fix DRC v3: Smart via/track placement with collision checking.
1. Route U5/C5 signals via B.Cu (avoid F.Cu congestion)
2. Add GND vias with proper clearance checking against existing geometry
3. Re-fill zones

Run with: "C:\Program Files\KiCad\10.0\bin\python.exe" fix_drc_v3.py
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
    ni = board.GetNetInfo()
    return ni.GetNetItem(net_name)

def get_pad_pos(board, ref, pad_num):
    for fp in board.GetFootprints():
        if fp.GetReference() == ref:
            for pad in fp.Pads():
                if pad.GetNumber() == str(pad_num):
                    p = pad.GetPosition()
                    return (TMM(p.x), TMM(p.y))
    return None


def collect_geometry(board):
    """Collect all existing track segments and via positions for collision checking."""
    tracks = []  # (x1, y1, x2, y2, width, layer, net_name)
    vias = []    # (x, y, size, net_name)

    for item in board.GetTracks():
        if isinstance(item, pcbnew.PCB_VIA):
            p = item.GetPosition()
            net = item.GetNet()
            nn = net.GetNetname() if net else ""
            try:
                w = TMM(item.GetWidth(pcbnew.F_Cu))
            except Exception:
                try:
                    w = TMM(item.GetWidth())
                except Exception:
                    w = 0.6  # default via size
            vias.append((TMM(p.x), TMM(p.y), w, nn))
        elif isinstance(item, pcbnew.PCB_TRACK):
            s = item.GetStart()
            e = item.GetEnd()
            net = item.GetNet()
            nn = net.GetNetname() if net else ""
            layer = item.GetLayer()
            tracks.append((TMM(s.x), TMM(s.y), TMM(e.x), TMM(e.y),
                          TMM(item.GetWidth()), layer, nn))

    # Also collect pad positions
    pads = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            p = pad.GetPosition()
            net = pad.GetNet()
            nn = net.GetNetname() if net else ""
            # Use bounding box size as approximation
            sz = pad.GetSize()
            pads.append((TMM(p.x), TMM(p.y), max(TMM(sz.x), TMM(sz.y)), nn))

    return tracks, vias, pads


def point_to_segment_dist(px, py, x1, y1, x2, y2):
    """Distance from point to line segment."""
    dx, dy = x2 - x1, y2 - y1
    if dx == 0 and dy == 0:
        return math.sqrt((px - x1)**2 + (py - y1)**2)
    t = max(0, min(1, ((px - x1)*dx + (py - y1)*dy) / (dx*dx + dy*dy)))
    proj_x = x1 + t * dx
    proj_y = y1 + t * dy
    return math.sqrt((px - proj_x)**2 + (py - proj_y)**2)


def is_via_safe(vx, vy, via_size, net_name, tracks, vias, pads, clearance=0.20):
    """Check if placing a via at (vx,vy) is safe (no collision with other nets)."""
    via_radius = via_size / 2.0

    # Check against existing vias
    for ex, ey, esize, enn in vias:
        if enn == net_name:
            continue  # Same net, OK
        dist = math.sqrt((vx - ex)**2 + (vy - ey)**2)
        min_dist = via_radius + esize/2.0 + clearance
        if dist < min_dist:
            return False

    # Check against existing tracks (ALL layers since via is through-hole)
    for x1, y1, x2, y2, tw, layer, tn in tracks:
        if tn == net_name:
            continue  # Same net, OK
        dist = point_to_segment_dist(vx, vy, x1, y1, x2, y2)
        min_dist = via_radius + tw/2.0 + clearance
        if dist < min_dist:
            return False

    # Check against pads of other nets
    for px, py, psize, pn in pads:
        if pn == net_name or pn == "":
            continue
        dist = math.sqrt((vx - px)**2 + (vy - py)**2)
        min_dist = via_radius + psize/2.0 + clearance
        if dist < min_dist:
            return False

    # Check board bounds
    if vx < 0.8 or vx > 34.2 or vy < 0.8 or vy > 34.2:
        return False

    return True


def is_track_safe(x1, y1, x2, y2, tw, layer, net_name, tracks, vias, pads, clearance=0.20):
    """Check if a track segment is safe (simplified: check endpoints + midpoint)."""
    track_radius = tw / 2.0

    # Check several points along the track
    for t in [0.0, 0.25, 0.5, 0.75, 1.0]:
        px = x1 + t * (x2 - x1)
        py = y1 + t * (y2 - y1)

        # Check against tracks on same layer
        for tx1, ty1, tx2, ty2, ttw, tlayer, tn in tracks:
            if tlayer != layer or tn == net_name:
                continue
            dist = point_to_segment_dist(px, py, tx1, ty1, tx2, ty2)
            min_dist = track_radius + ttw/2.0 + clearance
            if dist < min_dist:
                return False

        # Check against vias (all layers for through-hole)
        for vx, vy, vsize, vn in vias:
            if vn == net_name:
                continue
            dist = math.sqrt((px - vx)**2 + (py - vy)**2)
            min_dist = track_radius + vsize/2.0 + clearance
            if dist < min_dist:
                return False

        # Check against pads of other nets on same layer
        for ppx, ppy, psize, pn in pads:
            if pn == net_name or pn == "":
                continue
            dist = math.sqrt((px - ppx)**2 + (py - ppy)**2)
            min_dist = track_radius + psize/2.0 + clearance
            if dist < min_dist:
                return False

    return True


def add_via(board, x, y, net_name, drill=0.3, size=0.6):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(vec(x, y))
    v.SetDrill(MM(drill))
    v.SetWidth(MM(size))
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    net = get_net(board, net_name)
    if net:
        v.SetNet(net)
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


def find_safe_via_near(cx, cy, net_name, tracks, vias, pads, max_radius=3.0, step=0.25):
    """Search in expanding circles around (cx,cy) for a safe via location."""
    # Try exact position first
    if is_via_safe(cx, cy, 0.6, net_name, tracks, vias, pads):
        return (cx, cy)

    # Spiral outward
    for r_steps in range(1, int(max_radius / step) + 1):
        r = r_steps * step
        for angle_steps in range(int(2 * math.pi * r / step) + 1):
            angle = angle_steps * step / r if r > 0 else 0
            vx = round((cx + r * math.cos(angle)) / step) * step
            vy = round((cy + r * math.sin(angle)) / step) * step
            if is_via_safe(vx, vy, 0.6, net_name, tracks, vias, pads):
                return (vx, vy)

    return None


def fix():
    print(f"Loading PCB: {PCB_FILE}")
    board = pcbnew.LoadBoard(PCB_FILE)

    print("Collecting existing geometry...")
    tracks, vias, pads = collect_geometry(board)
    print(f"  {len(tracks)} tracks, {len(vias)} vias, {len(pads)} pads")

    added_vias = []
    added_tracks = []

    # === Part 1: Route U5 signals via B.Cu ===
    print("\n=== Part 1: Route U5/C5 signals ===")

    # U5 pads: 1:(5.30,31.60) I2C_SDA, 2:(5.30,32.40) I2C_SCL,
    #          3:(6.70,32.40) +3V3, 4:(6.70,31.60) GND
    # C5 pads: 1:(10.00,32.48) +3V3, 2:(10.00,31.52) GND

    # Find nearest existing track endpoints for each net
    # I2C_SDA: need to connect U5.1 (5.30, 31.60) to SDA bus
    # I2C_SCL: need to connect U5.2 (5.30, 32.40) to SCL bus
    # +3V3: need to connect U5.3 (6.70, 32.40) and C5.1 (10.00, 32.48)

    # Strategy: route on B.Cu from U5 pads to vias, then connect via In2.Cu (+3V3 plane)

    # --- I2C_SDA: U5.1 at (5.30, 31.60) ---
    # Find nearest SDA track point
    sda_target = None
    min_dist = 999
    for x1, y1, x2, y2, tw, layer, tn in tracks:
        if tn == "I2C_SDA" and layer == pcbnew.F_Cu:
            for px, py in [(x1,y1), (x2,y2)]:
                d = math.sqrt((px-5.30)**2 + (py-31.60)**2)
                if d < min_dist:
                    min_dist = d
                    sda_target = (px, py)

    if sda_target:
        print(f"  SDA nearest track point: ({sda_target[0]:.2f}, {sda_target[1]:.2f}), dist={min_dist:.1f}mm")
        # Via next to U5.1, route on B.Cu to near SDA track, via back up
        via1_pos = find_safe_via_near(4.50, 31.60, "I2C_SDA", tracks, vias, pads)
        via2_pos = find_safe_via_near(sda_target[0], sda_target[1], "I2C_SDA", tracks, vias, pads)
        if via1_pos and via2_pos:
            add_track(board, 5.30, 31.60, via1_pos[0], via1_pos[1], "I2C_SDA", pcbnew.F_Cu, 0.15)
            add_via(board, via1_pos[0], via1_pos[1], "I2C_SDA")
            add_track(board, via1_pos[0], via1_pos[1], via2_pos[0], via2_pos[1], "I2C_SDA", pcbnew.B_Cu, 0.15)
            add_via(board, via2_pos[0], via2_pos[1], "I2C_SDA")
            add_track(board, via2_pos[0], via2_pos[1], sda_target[0], sda_target[1], "I2C_SDA", pcbnew.F_Cu, 0.15)
            print(f"  SDA routed: U5.1 → via({via1_pos[0]:.2f},{via1_pos[1]:.2f}) → B.Cu → via({via2_pos[0]:.2f},{via2_pos[1]:.2f}) → F.Cu")
            added_vias.extend([via1_pos, via2_pos])
        else:
            print(f"  SDA: no safe via found!")

    # --- I2C_SCL: U5.2 at (5.30, 32.40) ---
    scl_target = None
    min_dist = 999
    for x1, y1, x2, y2, tw, layer, tn in tracks:
        if tn == "I2C_SCL" and layer == pcbnew.F_Cu:
            for px, py in [(x1,y1), (x2,y2)]:
                d = math.sqrt((px-5.30)**2 + (py-32.40)**2)
                if d < min_dist:
                    min_dist = d
                    scl_target = (px, py)

    if scl_target:
        print(f"  SCL nearest track point: ({scl_target[0]:.2f}, {scl_target[1]:.2f}), dist={min_dist:.1f}mm")
        via1_pos = find_safe_via_near(4.50, 32.40, "I2C_SCL", tracks, vias, pads)
        via2_pos = find_safe_via_near(scl_target[0], scl_target[1], "I2C_SCL", tracks, vias, pads)
        if via1_pos and via2_pos:
            add_track(board, 5.30, 32.40, via1_pos[0], via1_pos[1], "I2C_SCL", pcbnew.F_Cu, 0.15)
            add_via(board, via1_pos[0], via1_pos[1], "I2C_SCL")
            add_track(board, via1_pos[0], via1_pos[1], via2_pos[0], via2_pos[1], "I2C_SCL", pcbnew.B_Cu, 0.15)
            add_via(board, via2_pos[0], via2_pos[1], "I2C_SCL")
            add_track(board, via2_pos[0], via2_pos[1], scl_target[0], scl_target[1], "I2C_SCL", pcbnew.F_Cu, 0.15)
            print(f"  SCL routed: U5.2 → via → B.Cu → via → F.Cu")
            added_vias.extend([via1_pos, via2_pos])
        else:
            print(f"  SCL: no safe via found!")

    # --- +3V3: U5.3 at (6.70, 32.40) and C5.1 at (10.00, 32.48) ---
    # Connect both to +3V3 via In2.Cu plane
    v3v3_target = None
    min_dist = 999
    for x1, y1, x2, y2, tw, layer, tn in tracks:
        if tn == "+3V3" and layer == pcbnew.F_Cu:
            for px, py in [(x1,y1), (x2,y2)]:
                d = math.sqrt((px-6.70)**2 + (py-32.40)**2)
                if d < min_dist:
                    min_dist = d
                    v3v3_target = (px, py)

    # U5.3 → via to In2.Cu (+3V3 plane)
    via_pos = find_safe_via_near(6.70, 33.20, "+3V3", tracks, vias, pads)
    if via_pos:
        add_track(board, 6.70, 32.40, via_pos[0], via_pos[1], "+3V3", pcbnew.F_Cu, 0.15)
        add_via(board, via_pos[0], via_pos[1], "+3V3")
        print(f"  +3V3 U5.3: via at ({via_pos[0]:.2f}, {via_pos[1]:.2f}) → In2.Cu plane")
        added_vias.append(via_pos)

    # C5.1 → via to In2.Cu (+3V3 plane)
    via_pos = find_safe_via_near(10.00, 33.20, "+3V3", tracks, vias, pads)
    if via_pos:
        add_track(board, 10.00, 32.48, via_pos[0], via_pos[1], "+3V3", pcbnew.F_Cu, 0.15)
        add_via(board, via_pos[0], via_pos[1], "+3V3")
        print(f"  +3V3 C5.1: via at ({via_pos[0]:.2f}, {via_pos[1]:.2f}) → In2.Cu plane")
        added_vias.append(via_pos)

    # Update geometry after adding U5/C5 stuff
    tracks, vias, pads = collect_geometry(board)

    # === Part 2: Add GND stitching vias (collision-checked) ===
    print("\n=== Part 2: GND stitching vias ===")

    # For each unconnected GND pad area, find safe via location
    gnd_areas = [
        # (search_center_x, search_center_y, description)
        # U1 area
        (8.50, 10.00, "U1-left"),
        (12.50, 12.00, "U1-right"),
        # U2 area
        (8.50, 15.00, "U2-left"),
        (12.50, 17.00, "U2-right"),
        # U3 area
        (7.50, 20.00, "U3-left"),
        (12.00, 23.00, "U3-right"),
        # U4 north
        (14.00, 25.50, "U4-NW"),
        (20.50, 25.50, "U4-NE"),
        # U4 south
        (15.50, 31.00, "U4-SW"),
        (19.50, 31.00, "U4-SE"),
        # U4 west
        (13.50, 28.00, "U4-W"),
        # U4 east
        (21.00, 28.00, "U4-E"),
        # C4 area
        (25.00, 25.00, "C4"),
        # Corner stitching
        (2.00, 2.00, "corner-NW"),
        (33.00, 2.00, "corner-NE"),
        (2.00, 33.00, "corner-SW"),
        (33.00, 33.00, "corner-SE"),
        (17.50, 1.50, "edge-N"),
        (1.50, 17.50, "edge-W"),
        (33.50, 17.50, "edge-E"),
        (17.50, 33.50, "edge-S"),
    ]

    placed_gnd_vias = 0
    for cx, cy, desc in gnd_areas:
        via_pos = find_safe_via_near(cx, cy, "GND", tracks, vias, pads, max_radius=2.5, step=0.50)
        if via_pos:
            add_via(board, via_pos[0], via_pos[1], "GND")
            print(f"  GND via at ({via_pos[0]:.2f}, {via_pos[1]:.2f}) [{desc}]")
            placed_gnd_vias += 1
            # Update geometry
            vias.append((via_pos[0], via_pos[1], 0.6, "GND"))
        else:
            print(f"  WARNING: No safe GND via near ({cx:.1f}, {cy:.1f}) [{desc}]")

    print(f"\n  Placed {placed_gnd_vias} GND stitching vias")

    # === Part 3: Save and fill zones ===
    print("\n=== Part 3: Save and fill zones ===")
    board.Save(PCB_FILE)

    board2 = pcbnew.LoadBoard(PCB_FILE)
    filler = pcbnew.ZONE_FILLER(board2)
    filler.Fill(board2.Zones())
    board2.Save(PCB_FILE)
    print("  Zones filled successfully")

    print(f"\nDone! PCB saved to: {PCB_FILE}")


if __name__ == "__main__":
    fix()
