"""
Fix DRC v5: Fix the v4 routing errors.
Problem: v4 SDA/SCL routes have crossings and shorts.
Solution:
  - SDA: route LEFT first, then DOWN, RIGHT along y=34.10, UP to existing SDA endpoint
  - SCL: short F.Cu stub DOWN, via to B.Cu, B.Cu track to existing SCL endpoint, via back
  - Keep v4 +3V3 vias/tracks and GND vias (they're fine)

Run with: "C:\Program Files\KiCad\10.0\bin\python.exe" fix_drc_v5.py
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

def match_pos(actual, target, tol=0.05):
    return abs(actual[0] - target[0]) < tol and abs(actual[1] - target[1]) < tol

def match_track(item, p1, p2, tol=0.05):
    """Check if track matches endpoints (in either direction)."""
    s = item.GetStart()
    e = item.GetEnd()
    sx, sy = TMM(s.x), TMM(s.y)
    ex, ey = TMM(e.x), TMM(e.y)
    return (match_pos((sx,sy), p1, tol) and match_pos((ex,ey), p2, tol)) or \
           (match_pos((sx,sy), p2, tol) and match_pos((ex,ey), p1, tol))


def fix():
    print(f"Loading PCB: {PCB_FILE}")
    board = pcbnew.LoadBoard(PCB_FILE)

    # === Step 1: Remove v4 SDA and SCL tracks ===
    print("\n=== Step 1: Remove v4 SDA/SCL tracks ===")

    # v4 SDA tracks to remove:
    v4_sda_tracks = [
        ((5.30, 31.60), (5.30, 34.00)),   # vertical down (crosses SCL)
        ((5.30, 34.00), (8.15, 34.00)),    # horizontal right
        ((8.15, 34.00), (8.15, 22.87)),    # vertical up
    ]
    # v4 SCL tracks to remove:
    v4_scl_tracks = [
        ((5.30, 32.40), (0.50, 32.40)),   # horizontal left
        ((0.50, 32.40), (0.50, 34.50)),    # vertical down
        ((0.50, 34.50), (10.00, 34.50)),   # horizontal right
        ((10.00, 34.50), (10.00, 19.00)),  # vertical up (through C5!)
    ]

    all_items = list(board.GetTracks())
    to_remove = []
    for item in all_items:
        if isinstance(item, pcbnew.PCB_TRACK) and not isinstance(item, pcbnew.PCB_VIA):
            net = item.GetNet()
            nn = net.GetNetname() if net else ""
            if nn == "I2C_SDA":
                for p1, p2 in v4_sda_tracks:
                    if match_track(item, p1, p2):
                        to_remove.append(item)
                        break
            elif nn == "I2C_SCL":
                for p1, p2 in v4_scl_tracks:
                    if match_track(item, p1, p2):
                        to_remove.append(item)
                        break

    for item in to_remove:
        board.Remove(item)
    print(f"  Removed {len(to_remove)} v4 tracks")

    # Also remove dangling SCL via at (10.00, 21.25) if it exists
    all_items = list(board.GetTracks())
    for item in all_items:
        if isinstance(item, pcbnew.PCB_VIA):
            p = item.GetPosition()
            vx, vy = TMM(p.x), TMM(p.y)
            net = item.GetNet()
            nn = net.GetNetname() if net else ""
            if nn == "I2C_SCL" and match_pos((vx, vy), (10.00, 21.25)):
                board.Remove(item)
                print(f"  Removed dangling SCL via at ({vx:.2f}, {vy:.2f})")

    # Save and reload
    print("  Saving and reloading...")
    board.Save(PCB_FILE)
    del board
    board = pcbnew.LoadBoard(PCB_FILE)

    # === Step 2: Add new SDA route on F.Cu ===
    print("\n=== Step 2: New SDA route (F.Cu) ===")
    # SDA: U5.1(5.30,31.60) -> LEFT -> DOWN -> RIGHT along y=34.10 -> UP to (8.15,22.87)
    add_track(board, 5.30, 31.60, 3.50, 31.60, "I2C_SDA", pcbnew.F_Cu, 0.15)
    add_track(board, 3.50, 31.60, 3.50, 34.10, "I2C_SDA", pcbnew.F_Cu, 0.15)
    add_track(board, 3.50, 34.10, 8.15, 34.10, "I2C_SDA", pcbnew.F_Cu, 0.15)
    add_track(board, 8.15, 34.10, 8.15, 22.87, "I2C_SDA", pcbnew.F_Cu, 0.15)
    print("  SDA: (5.30,31.60) -> (3.50,31.60) -> (3.50,34.10) -> (8.15,34.10) -> (8.15,22.87)")

    # === Step 3: Add new SCL route via B.Cu ===
    print("\n=== Step 3: New SCL route (F.Cu + B.Cu) ===")
    # SCL: U5.2(5.30,32.40) -> DOWN to via -> B.Cu to SCL endpoint -> via back
    # F.Cu stub
    add_track(board, 5.30, 32.40, 5.30, 33.50, "I2C_SCL", pcbnew.F_Cu, 0.15)
    # Via at (5.30, 33.50)
    add_via(board, 5.30, 33.50, "I2C_SCL")
    # B.Cu L-route: down at x=5.30, then right at y=19.00
    add_track(board, 5.30, 33.50, 5.30, 19.00, "I2C_SCL", pcbnew.B_Cu, 0.15)
    add_track(board, 5.30, 19.00, 10.00, 19.00, "I2C_SCL", pcbnew.B_Cu, 0.15)
    # Via at (10.00, 19.00) to connect to existing SCL track on F.Cu
    add_via(board, 10.00, 19.00, "I2C_SCL")
    print("  SCL: (5.30,32.40) F.Cu-> (5.30,33.50) via -> B.Cu -> (10.00,19.00) via -> F.Cu")

    # === Step 4: Verify zone settings ===
    print("\n=== Step 4: Zone settings ===")
    for zone in board.Zones():
        if zone.GetLayer() == pcbnew.F_Cu:
            net = zone.GetNet()
            if net and net.GetNetname() == "GND":
                zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
                print("  F.Cu GND zone: FULL connection")

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
