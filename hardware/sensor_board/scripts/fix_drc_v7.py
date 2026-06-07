"""
Fix DRC v7: Move SDA via from (19.75,25.00) to (19.75,25.20) to clear BOOTN_N track.
BOOTN_N runs at y=24.53 — need 0.15mm clearance from via edge.

Run with: "C:\Program Files\KiCad\10.0\bin\python.exe" fix_drc_v7.py
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

def match_pos(px, py, tx, ty, tol=0.05):
    return abs(px - tx) < tol and abs(py - ty) < tol

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


def fix():
    print(f"Loading PCB: {PCB_FILE}")
    board = pcbnew.LoadBoard(PCB_FILE)

    # Remove v6 additions that need repositioning
    print("\n=== Remove v6 SDA via and tracks ===")
    to_remove = []
    all_items = list(board.GetTracks())
    for item in all_items:
        if isinstance(item, pcbnew.PCB_VIA):
            p = item.GetPosition()
            vx, vy = TMM(p.x), TMM(p.y)
            net = item.GetNet()
            nn = net.GetNetname() if net else ""
            if nn == "I2C_SDA" and match_pos(vx, vy, 19.75, 25.00):
                to_remove.append(item)
                print(f"  Remove SDA via at ({vx:.2f},{vy:.2f})")
        elif isinstance(item, pcbnew.PCB_TRACK):
            net = item.GetNet()
            nn = net.GetNetname() if net else ""
            if nn == "I2C_SDA":
                s, e = item.GetStart(), item.GetEnd()
                sx, sy = TMM(s.x), TMM(s.y)
                ex, ey = TMM(e.x), TMM(e.y)
                # F.Cu stub: (19.75,25.77) -> (19.75,25.00)
                if match_pos(sx, sy, 19.75, 25.77) and match_pos(ex, ey, 19.75, 25.00) or \
                   match_pos(ex, ey, 19.75, 25.77) and match_pos(sx, sy, 19.75, 25.00):
                    to_remove.append(item)
                    print(f"  Remove SDA F.Cu: ({sx:.2f},{sy:.2f})->({ex:.2f},{ey:.2f})")
                # B.Cu: (19.75,25.00) -> (29.00,25.00)
                if item.GetLayer() == pcbnew.B_Cu:
                    if match_pos(sx, sy, 19.75, 25.00) and match_pos(ex, ey, 29.00, 25.00) or \
                       match_pos(ex, ey, 19.75, 25.00) and match_pos(sx, sy, 29.00, 25.00):
                        to_remove.append(item)
                        print(f"  Remove SDA B.Cu: ({sx:.2f},{sy:.2f})->({ex:.2f},{ey:.2f})")
                    # B.Cu: (29.00,25.00) -> (29.00,14.50)
                    if match_pos(sx, sy, 29.00, 25.00) and match_pos(ex, ey, 29.00, 14.50) or \
                       match_pos(ex, ey, 29.00, 25.00) and match_pos(sx, sy, 29.00, 14.50):
                        to_remove.append(item)
                        print(f"  Remove SDA B.Cu: ({sx:.2f},{sy:.2f})->({ex:.2f},{ey:.2f})")

    for item in to_remove:
        board.Remove(item)

    # Save and reload
    board.Save(PCB_FILE)
    del board
    board = pcbnew.LoadBoard(PCB_FILE)

    # Re-add with corrected via position at y=25.20
    print("\n=== Add corrected SDA route ===")
    # F.Cu stub from existing endpoint (19.75,25.77) to new via position
    add_track(board, 19.75, 25.77, 19.75, 25.20, "I2C_SDA", pcbnew.F_Cu, 0.15)
    # Via at (19.75, 25.20) — clearance to BOOTN_N at y=24.53: 0.67-0.375=0.295mm OK
    add_via(board, 19.75, 25.20, "I2C_SDA")
    # B.Cu L-route
    add_track(board, 19.75, 25.20, 29.00, 25.20, "I2C_SDA", pcbnew.B_Cu, 0.15)
    add_track(board, 29.00, 25.20, 29.00, 14.50, "I2C_SDA", pcbnew.B_Cu, 0.15)
    print("  SDA via moved to (19.75,25.20)")
    print("  B.Cu: (19.75,25.20) -> (29.00,25.20) -> (29.00,14.50)")

    # Save and fill zones
    print("\n=== Save and fill zones ===")
    board.Save(PCB_FILE)
    board2 = pcbnew.LoadBoard(PCB_FILE)
    filler = pcbnew.ZONE_FILLER(board2)
    filler.Fill(board2.Zones())
    board2.Save(PCB_FILE)
    print("  Done!")


if __name__ == "__main__":
    fix()
