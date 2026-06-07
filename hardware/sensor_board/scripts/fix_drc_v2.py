"""
Fix DRC v2: Remove ALL vias/tracks from fix_drc.py, keep only zone FULL connection.
Then re-fill zones. The zone fill with FULL connection should handle GND pads.

Run with: "C:\Program Files\KiCad\10.0\bin\python.exe" fix_drc_v2.py
"""

import pcbnew
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_DIR   = os.path.dirname(SCRIPT_DIR)
PCB_FILE   = os.path.join(BASE_DIR, "project", "sensor_board.kicad_pcb")

MM  = pcbnew.FromMM
TMM = pcbnew.ToMM


def fix():
    print(f"Loading PCB: {PCB_FILE}")
    board = pcbnew.LoadBoard(PCB_FILE)

    # === Step 1: Remove all vias and tracks added by fix_drc.py ===
    # These are GND vias at known positions + short GND tracks to them
    # Also corner vias

    vias_added = [
        (8.00, 10.75), (12.00, 12.25), (12.25, 11.75),
        (8.00, 15.75), (7.75, 17.75), (12.00, 17.25), (12.25, 16.75),
        (7.25, 20.75), (10.00, 23.75), (10.50, 23.75),
        (15.25, 24.75), (13.50, 27.25), (13.50, 28.75),
        (16.25, 31.25), (17.25, 31.25), (18.25, 31.25), (18.75, 31.25),
        (21.50, 28.25), (19.25, 24.75), (18.75, 24.75),
        (17.75, 24.75), (17.25, 24.75), (16.75, 24.75),
        (25.00, 25.50), (7.75, 31.50),
        (2.00, 2.00), (33.00, 2.00), (2.00, 33.00), (33.00, 33.00),
        (17.50, 2.00), (2.00, 17.50), (33.00, 17.50), (17.50, 33.00),
    ]

    # Remove vias at these positions
    removed_vias = 0
    vias_to_remove = []
    for item in board.GetTracks():
        if isinstance(item, pcbnew.PCB_VIA):
            pos = item.GetPosition()
            vx, vy = TMM(pos.x), TMM(pos.y)
            for ax, ay in vias_added:
                if abs(vx - ax) < 0.05 and abs(vy - ay) < 0.05:
                    vias_to_remove.append(item)
                    break

    for v in vias_to_remove:
        board.Remove(v)
        removed_vias += 1
    print(f"  Removed {removed_vias} vias from fix_drc.py")

    # Remove short GND tracks added by fix_drc.py
    # These are tracks with width 0.15mm, net GND, that END at one of the via positions
    removed_tracks = 0
    tracks_to_remove = []
    for item in board.GetTracks():
        if isinstance(item, pcbnew.PCB_TRACK) and not isinstance(item, pcbnew.PCB_VIA):
            net = item.GetNet()
            if net and net.GetNetname() == "GND" and TMM(item.GetWidth()) < 0.16:
                end = item.GetEnd()
                start = item.GetStart()
                ex, ey = TMM(end.x), TMM(end.y)
                sx, sy = TMM(start.x), TMM(start.y)
                for ax, ay in vias_added:
                    if (abs(ex - ax) < 0.05 and abs(ey - ay) < 0.05) or \
                       (abs(sx - ax) < 0.05 and abs(sy - ay) < 0.05):
                        tracks_to_remove.append(item)
                        break

    for t in tracks_to_remove:
        board.Remove(t)
        removed_tracks += 1
    print(f"  Removed {removed_tracks} short GND tracks from fix_drc.py")

    # === Step 2: Keep F.Cu GND zone as FULL connection ===
    print("\n  F.Cu GND zone already set to FULL connection (kept)")
    for zone in board.Zones():
        if zone.GetLayer() == pcbnew.F_Cu:
            net = zone.GetNet()
            if net and net.GetNetname() == "GND":
                zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)

    # === Step 3: Save and re-fill zones ===
    print("\n  Saving and filling zones...")
    board.Save(PCB_FILE)

    board2 = pcbnew.LoadBoard(PCB_FILE)
    filler = pcbnew.ZONE_FILLER(board2)
    filler.Fill(board2.Zones())
    board2.Save(PCB_FILE)
    print("  Zones filled successfully")

    print(f"\nPCB saved to: {PCB_FILE}")
    print("\nWith FULL connection on F.Cu GND zone, the zone fill")
    print("should directly connect GND pads without thermal relief.")


if __name__ == "__main__":
    fix()
