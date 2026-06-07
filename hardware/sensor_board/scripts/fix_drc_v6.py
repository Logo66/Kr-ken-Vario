"""
Fix DRC v6: Bridge remaining signal gaps.
1. SCL: short F.Cu track from via (10.00,19.00) to U3.13 (10.00,20.49)
2. SDA: connect U4.20 via B.Cu to main SDA network at (29.00,14.50)

Run with: "C:\Program Files\KiCad\10.0\bin\python.exe" fix_drc_v6.py
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


def fix():
    print(f"Loading PCB: {PCB_FILE}")
    board = pcbnew.LoadBoard(PCB_FILE)

    # === Fix 1: SCL gap (10.00,19.00) -> U3.13 (10.00,20.49) ===
    print("\n=== Fix 1: SCL via to U3.13 bridge ===")
    add_track(board, 10.00, 19.00, 10.00, 20.49, "I2C_SCL", pcbnew.F_Cu, 0.15)
    print("  Added SCL track: (10.00,19.00) -> (10.00,20.49)")

    # === Fix 2: SDA U4.20 connection via B.Cu ===
    print("\n=== Fix 2: SDA U4.20 via B.Cu ===")
    # Extend existing SDA stub from (19.75,25.77) up to via position
    add_track(board, 19.75, 25.77, 19.75, 25.00, "I2C_SDA", pcbnew.F_Cu, 0.15)
    # Via at (19.75, 25.00) down to B.Cu
    add_via(board, 19.75, 25.00, "I2C_SDA")
    # B.Cu L-route to existing SDA endpoint at (29.00, 14.50)
    add_track(board, 19.75, 25.00, 29.00, 25.00, "I2C_SDA", pcbnew.B_Cu, 0.15)
    add_track(board, 29.00, 25.00, 29.00, 14.50, "I2C_SDA", pcbnew.B_Cu, 0.15)
    # Via at (29.00, 14.50) back up to F.Cu, connecting to existing SDA track
    add_via(board, 29.00, 14.50, "I2C_SDA")
    print("  SDA: (19.75,25.77) F.Cu-> (19.75,25.00) via -> B.Cu -> (29.00,25.00) -> (29.00,14.50) via -> F.Cu")

    # === Save and refill zones ===
    print("\n=== Saving and filling zones ===")
    board.Save(PCB_FILE)

    board2 = pcbnew.LoadBoard(PCB_FILE)
    filler = pcbnew.ZONE_FILLER(board2)
    filler.Fill(board2.Zones())
    board2.Save(PCB_FILE)
    print("  Done!")


if __name__ == "__main__":
    fix()
