"""
Revert all GND pad vias added by fix_gnd_pads.py.
Remove vias and short tracks at known positions.

Run with: "C:\Program Files\KiCad\10.0\bin\python.exe" revert_gnd_vias.py
"""

import pcbnew
import os
import math

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_DIR   = os.path.dirname(SCRIPT_DIR)
PCB_FILE   = os.path.join(BASE_DIR, "project", "sensor_board.kicad_pcb")

MM  = pcbnew.FromMM
TMM = pcbnew.ToMM

# Via positions added by fix_gnd_pads.py
ADDED_VIAS = [
    (9.79, 10.13),   # U1.3
    (10.63, 12.85),  # U1.8
    (11.39, 11.83),  # U1.9 - SHORTS with In2.Cu SCL!
    (9.37, 15.15),   # U2.3
    (7.98, 16.97),   # U2.5 - track crosses SDA
    (10.21, 17.87),  # U2.8
    (11.82, 16.93),  # U2.9 - track crosses SCL
    (7.71, 20.14),   # U3.1 - track crosses SDA
    (10.00, 23.36),  # U3.6
    (14.81, 26.01),  # U4.1
    (14.59, 27.09),  # U4.2
    (14.59, 28.91),  # U4.5
    (15.92, 30.10),  # U4.8
    (16.83, 29.10),  # U4.10 - too close to CAP_BNO
    (18.75, 29.43),  # U4.12 - too close to INT_BNO
    (19.19, 29.33),  # U4.13 - too close to INT_BNO
    (20.44, 28.31),  # U4.17 - too close to pad 16
    (18.87, 25.94),  # U4.21
    (16.52, 25.84),  # U4.26 - too close to pad 27
]

# Pad positions that had tracks to vias
PAD_POSITIONS = [
    (9.60, 10.65),   # U1.3
    (10.40, 12.35),  # U1.8
    (10.85, 11.70),  # U1.9
    (9.60, 15.65),   # U2.3
    (9.15, 16.70),   # U2.5
    (10.40, 17.35),  # U2.8
    (10.85, 16.70),  # U2.9
    (8.74, 20.75),   # U3.1
    (10.00, 22.51),  # U3.6
    (15.25, 26.34),  # U4.1
    (15.11, 27.25),  # U4.2
    (15.11, 28.75),  # U4.5
    (16.25, 29.66),  # U4.8
    (17.25, 29.66),  # U4.10
    (18.25, 29.66),  # U4.12
    (18.75, 29.66),  # U4.13
    (19.89, 28.25),  # U4.17
    (19.25, 26.34),  # U4.21
    (16.75, 26.34),  # U4.26
]


def fix():
    print(f"Loading PCB: {PCB_FILE}")
    board = pcbnew.LoadBoard(PCB_FILE)

    all_items = list(board.GetTracks())
    to_remove = []

    # Remove vias at known positions
    for item in all_items:
        if isinstance(item, pcbnew.PCB_VIA):
            p = item.GetPosition()
            vx, vy = TMM(p.x), TMM(p.y)
            for ax, ay in ADDED_VIAS:
                if abs(vx - ax) < 0.05 and abs(vy - ay) < 0.05:
                    to_remove.append(item)
                    break
        elif isinstance(item, pcbnew.PCB_TRACK):
            net = item.GetNet()
            nn = net.GetNetname() if net else ""
            if nn != "GND":
                continue
            s, e = item.GetStart(), item.GetEnd()
            sx, sy = TMM(s.x), TMM(s.y)
            ex, ey = TMM(e.x), TMM(e.y)
            w = TMM(item.GetWidth())
            if abs(w - 0.15) > 0.01:
                continue
            # Check if track connects a pad position to a via position
            for px, py in PAD_POSITIONS:
                for vx, vy in ADDED_VIAS:
                    if (abs(sx-px)<0.05 and abs(sy-py)<0.05 and abs(ex-vx)<0.05 and abs(ey-vy)<0.05) or \
                       (abs(ex-px)<0.05 and abs(ey-py)<0.05 and abs(sx-vx)<0.05 and abs(sy-vy)<0.05):
                        to_remove.append(item)
                        break
                else:
                    continue
                break

    for item in to_remove:
        board.Remove(item)
    print(f"  Removed {len(to_remove)} items")

    # Save and refill
    board.Save(PCB_FILE)
    board2 = pcbnew.LoadBoard(PCB_FILE)
    filler = pcbnew.ZONE_FILLER(board2)
    filler.Fill(board2.Zones())
    board2.Save(PCB_FILE)
    print("  Done!")


if __name__ == "__main__":
    fix()
