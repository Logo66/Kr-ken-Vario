"""
Fix DRC v4: Clean revert of v3, then minimal targeted fixes.
1. Remove ALL v3 additions (vias + tracks) in ONE pass
2. Save and reload board
3. Route U5 on F.Cu along board edge (safe path)
4. Connect +3V3 via In2.Cu plane vias
5. Add only a few safe GND corner vias
6. Keep F.Cu GND zone FULL connection

Run with: "C:\Program Files\KiCad\10.0\bin\python.exe" fix_drc_v4.py
"""

import pcbnew
import os

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
    # KiCad 10: SetWidth requires layer, use SetWidth on all layers
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


def fix():
    print(f"Loading PCB: {PCB_FILE}")
    board = pcbnew.LoadBoard(PCB_FILE)

    # === Step 1: Remove ALL v3 additions in ONE pass ===
    print("\n=== Step 1: Remove v3 additions ===")

    # Known v3 via positions
    v3_vias = [
        (4.50, 31.60), (11.05, 25.77),   # SDA vias
        (4.50, 32.40), (10.00, 20.49),    # SCL vias
        (6.70, 33.20), (10.00, 33.20),    # +3V3 vias
        # GND stitching vias
        (8.50, 10.00), (11.50, 12.50), (9.50, 15.00), (12.00, 17.00),
        (7.50, 20.00), (13.00, 23.00), (13.50, 25.00), (21.50, 25.50),
        (15.50, 31.00), (20.00, 31.50), (13.50, 28.00), (21.00, 28.00),
        (24.00, 25.50), (2.00, 2.00), (33.00, 2.00), (2.00, 33.00),
        (33.00, 33.00), (17.50, 1.50), (1.50, 17.50), (33.50, 17.50),
        (17.50, 33.50),
    ]

    v3_nets = {"I2C_SDA", "I2C_SCL", "+3V3"}

    # Collect everything to remove in one pass
    to_remove = []
    all_tracks = list(board.GetTracks())  # snapshot the list
    for item in all_tracks:
        if isinstance(item, pcbnew.PCB_VIA):
            p = item.GetPosition()
            vx, vy = TMM(p.x), TMM(p.y)
            for ax, ay in v3_vias:
                if abs(vx - ax) < 0.05 and abs(vy - ay) < 0.05:
                    to_remove.append(item)
                    break
        elif isinstance(item, pcbnew.PCB_TRACK):
            layer = item.GetLayer()
            net = item.GetNet()
            nn = net.GetNetname() if net else ""
            w = TMM(item.GetWidth())
            s = item.GetStart()
            e = item.GetEnd()
            sx, sy = TMM(s.x), TMM(s.y)
            ex, ey = TMM(e.x), TMM(e.y)

            # B.Cu tracks added by v3
            if layer == pcbnew.B_Cu and nn in v3_nets and abs(w - 0.15) < 0.01:
                to_remove.append(item)
                continue

            # F.Cu stubs from v3 (connect pads to via positions)
            for ax, ay in v3_vias:
                if (abs(sx - ax) < 0.05 and abs(sy - ay) < 0.05) or \
                   (abs(ex - ax) < 0.05 and abs(ey - ay) < 0.05):
                    if nn in v3_nets and abs(w - 0.15) < 0.01:
                        to_remove.append(item)
                        break

    # Remove all collected items
    via_count = 0
    track_count = 0
    for item in to_remove:
        if isinstance(item, pcbnew.PCB_VIA):
            via_count += 1
        else:
            track_count += 1
        board.Remove(item)
    print(f"  Removed {via_count} vias and {track_count} tracks")

    # Save and reload to get clean state
    print("  Saving and reloading board...")
    board.Save(PCB_FILE)
    del board
    board = pcbnew.LoadBoard(PCB_FILE)

    # === Step 2: Route U5 along board edge on F.Cu ===
    print("\n=== Step 2: Route U5/C5 signals ===")

    # Find nearest SDA endpoint on F.Cu
    nearest_sda = None
    min_d = 999
    for item in board.GetTracks():
        if isinstance(item, pcbnew.PCB_TRACK) and not isinstance(item, pcbnew.PCB_VIA):
            net = item.GetNet()
            if net and net.GetNetname() == "I2C_SDA" and item.GetLayer() == pcbnew.F_Cu:
                for p in [item.GetStart(), item.GetEnd()]:
                    px, py = TMM(p.x), TMM(p.y)
                    d = abs(px - 5.30) + abs(py - 31.60)
                    if d < min_d:
                        min_d = d
                        nearest_sda = (px, py)

    if nearest_sda:
        print(f"  SDA nearest: ({nearest_sda[0]:.2f}, {nearest_sda[1]:.2f})")
        # Route along bottom edge: U5.1 down to y=34, right along y=34, up to SDA point
        add_track(board, 5.30, 31.60, 5.30, 34.00, "I2C_SDA", pcbnew.F_Cu, 0.15)
        add_track(board, 5.30, 34.00, nearest_sda[0], 34.00, "I2C_SDA", pcbnew.F_Cu, 0.15)
        add_track(board, nearest_sda[0], 34.00, nearest_sda[0], nearest_sda[1], "I2C_SDA", pcbnew.F_Cu, 0.15)
        print(f"  SDA: U5.1 -> edge route -> ({nearest_sda[0]:.2f},{nearest_sda[1]:.2f})")
    else:
        print("  WARNING: No SDA track found on F.Cu!")

    # Find nearest SCL endpoint on F.Cu
    nearest_scl = None
    min_d = 999
    for item in board.GetTracks():
        if isinstance(item, pcbnew.PCB_TRACK) and not isinstance(item, pcbnew.PCB_VIA):
            net = item.GetNet()
            if net and net.GetNetname() == "I2C_SCL" and item.GetLayer() == pcbnew.F_Cu:
                for p in [item.GetStart(), item.GetEnd()]:
                    px, py = TMM(p.x), TMM(p.y)
                    d = abs(px - 5.30) + abs(py - 32.40)
                    if d < min_d:
                        min_d = d
                        nearest_scl = (px, py)

    if nearest_scl:
        print(f"  SCL nearest: ({nearest_scl[0]:.2f}, {nearest_scl[1]:.2f})")
        # Route left to edge, down, right along bottom, up
        add_track(board, 5.30, 32.40, 0.50, 32.40, "I2C_SCL", pcbnew.F_Cu, 0.15)
        add_track(board, 0.50, 32.40, 0.50, 34.50, "I2C_SCL", pcbnew.F_Cu, 0.15)
        add_track(board, 0.50, 34.50, nearest_scl[0], 34.50, "I2C_SCL", pcbnew.F_Cu, 0.15)
        add_track(board, nearest_scl[0], 34.50, nearest_scl[0], nearest_scl[1], "I2C_SCL", pcbnew.F_Cu, 0.15)
        print(f"  SCL: U5.2 -> left -> bottom edge -> ({nearest_scl[0]:.2f},{nearest_scl[1]:.2f})")
    else:
        print("  WARNING: No SCL track found on F.Cu!")

    # +3V3: U5.3 (6.70, 32.40) and C5.1 (10.00, 32.48) — via down to In2.Cu
    add_track(board, 6.70, 32.40, 6.70, 33.50, "+3V3", pcbnew.F_Cu, 0.15)
    add_via(board, 6.70, 33.50, "+3V3")
    print(f"  +3V3 U5.3: via at (6.70, 33.50)")

    # C5.1 — connect to In2.Cu +3V3 plane
    add_track(board, 10.00, 32.48, 10.00, 33.50, "+3V3", pcbnew.F_Cu, 0.15)
    add_via(board, 10.00, 33.50, "+3V3")
    print(f"  +3V3 C5.1: via at (10.00, 33.50)")

    # === Step 3: GND corner vias (safe, away from everything) ===
    print("\n=== Step 3: GND corner vias ===")
    safe_gnd_vias = [
        (1.50, 1.50, "NW"), (33.50, 1.50, "NE"),
        (1.50, 34.00, "SW"), (33.50, 34.00, "SE"),
        (17.50, 1.50, "N"), (1.50, 17.50, "W"),
    ]
    for vx, vy, label in safe_gnd_vias:
        add_via(board, vx, vy, "GND")
        print(f"  GND via at ({vx}, {vy}) [{label}]")

    # === Step 4: F.Cu GND zone FULL connection ===
    print("\n=== Step 4: F.Cu GND zone FULL connection ===")
    for zone in board.Zones():
        if zone.GetLayer() == pcbnew.F_Cu:
            net = zone.GetNet()
            if net and net.GetNetname() == "GND":
                zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
                print("  Set F.Cu GND zone to FULL connection")

    # === Step 5: Save and fill zones ===
    print("\n=== Step 5: Save and fill zones ===")
    board.Save(PCB_FILE)

    board2 = pcbnew.LoadBoard(PCB_FILE)
    filler = pcbnew.ZONE_FILLER(board2)
    filler.Fill(board2.Zones())
    board2.Save(PCB_FILE)
    print("  Done!")


if __name__ == "__main__":
    fix()
