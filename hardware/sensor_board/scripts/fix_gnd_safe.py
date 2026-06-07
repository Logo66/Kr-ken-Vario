"""
Fix GND pads: Add ONLY the 8 verified-safe via+track combos.
These were tested in the full batch and had zero DRC violations.

Run with: "C:\Program Files\KiCad\10.0\bin\python.exe" fix_gnd_safe.py
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

    # 8 verified-safe via positions: (pad_x, pad_y, via_x, via_y, label)
    safe_fixes = [
        (10.40, 12.35, 10.63, 12.85, "U1.8"),   # GND pad 8 of U1
        (9.60, 15.65, 9.37, 15.15, "U2.3"),      # GND pad 3 of U2
        (10.00, 22.51, 10.00, 23.36, "U3.6"),    # GND pad 6 of U3
        (15.25, 26.34, 14.81, 26.01, "U4.1"),    # GND pad 1 of U4
        (15.11, 27.25, 14.59, 27.09, "U4.2"),    # GND pad 2 of U4
        (15.11, 28.75, 14.59, 28.91, "U4.5"),    # GND pad 5 of U4
        (16.25, 29.66, 15.92, 30.10, "U4.8"),    # GND pad 8 of U4
        (19.25, 26.34, 18.87, 25.94, "U4.21"),   # GND pad 21 of U4
    ]

    for px, py, vx, vy, label in safe_fixes:
        add_track(board, px, py, vx, vy, "GND", pcbnew.F_Cu, 0.15)
        add_via(board, vx, vy, "GND")
        print(f"  {label}: track ({px},{py})->({vx},{vy}) + via")

    print(f"\nAdded {len(safe_fixes)} safe via+track combos")

    # Save and fill zones
    print("Saving and filling zones...")
    board.Save(PCB_FILE)
    board2 = pcbnew.LoadBoard(PCB_FILE)
    filler = pcbnew.ZONE_FILLER(board2)
    filler.Fill(board2.Zones())
    board2.Save(PCB_FILE)
    print("Done!")


if __name__ == "__main__":
    fix()
