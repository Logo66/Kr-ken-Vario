"""
Fix DRC issues on existing routed PCB:
1. Add GND stitching vias (connect F.Cu zone to In1.Cu/B.Cu planes)
2. Change F.Cu GND zone to FULL pad connection (fix starved thermals)
3. Re-fill zones
4. NOTE: U5/C5 unconnected signals (I2C_SDA, I2C_SCL, +3V3) need manual routing

Run with: "C:\Program Files\KiCad\10.0\bin\python.exe" fix_drc.py
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


def get_pad_pos(board, ref, pad_num):
    """Get pad position from board by reference and pad number."""
    for fp in board.GetFootprints():
        if fp.GetReference() == ref:
            for pad in fp.Pads():
                if pad.GetNumber() == str(pad_num):
                    p = pad.GetPosition()
                    return (TMM(p.x), TMM(p.y))
    return None


def get_net(board, net_name):
    """Get net info by name."""
    ni = board.GetNetInfo()
    return ni.GetNetItem(net_name)


def add_via(board, x, y, net_name, drill=0.3, size=0.6):
    """Add a through-hole via."""
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
    """Add a track segment."""
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

    # Print current pad positions for reference
    print("\nCurrent pad positions:")
    for ref in ["U1", "U2", "U3", "U4", "U5", "C4", "C5"]:
        for fp in board.GetFootprints():
            if fp.GetReference() == ref:
                pos = fp.GetPosition()
                print(f"  {ref} center: ({TMM(pos.x):.2f}, {TMM(pos.y):.2f})")
                break

    # === Fix 1: Change F.Cu GND zone to FULL pad connection ===
    print("\nFix 1: Setting F.Cu GND zone to FULL connection...")
    for zone in board.Zones():
        if zone.GetLayer() == pcbnew.F_Cu:
            net = zone.GetNet()
            if net and net.GetNetname() == "GND":
                zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
                print("  F.Cu GND zone → FULL connection (no thermal relief)")

    # === Fix 2: Add GND stitching vias ===
    print("\nFix 2: Adding GND stitching vias...")

    # Get positions of problematic GND pads
    gnd_pad_positions = []

    # U1 (BMP581 #1) - pads 3, 8, 9
    for pn in ["3", "8", "9"]:
        pos = get_pad_pos(board, "U1", pn)
        if pos:
            gnd_pad_positions.append(("U1", pn, pos))

    # U2 (BMP581 #2) - pads 3, 5, 8, 9
    for pn in ["3", "5", "8", "9"]:
        pos = get_pad_pos(board, "U2", pn)
        if pos:
            gnd_pad_positions.append(("U2", pn, pos))

    # U3 (LSM6DSO32) - pads 1, 6, 7
    for pn in ["1", "6", "7"]:
        pos = get_pad_pos(board, "U3", pn)
        if pos:
            gnd_pad_positions.append(("U3", pn, pos))

    # U4 (BNO085) - many GND pads
    for pn in ["1", "2", "5", "8", "10", "12", "13", "17", "21", "22", "24", "25", "26"]:
        pos = get_pad_pos(board, "U4", pn)
        if pos:
            gnd_pad_positions.append(("U4", pn, pos))

    # C4 pad 2
    pos = get_pad_pos(board, "C4", "2")
    if pos:
        gnd_pad_positions.append(("C4", "2", pos))

    # U5 pad 4
    pos = get_pad_pos(board, "U5", "4")
    if pos:
        gnd_pad_positions.append(("U5", "4", pos))

    # Place GND vias near each IC cluster (not on pads, but nearby)
    # Strategy: place vias at safe offsets from component centers
    via_positions = set()

    for ref, pn, (px, py) in gnd_pad_positions:
        # Offset via 1.5mm away from pad toward nearest board edge
        vx, vy = px, py

        if ref in ["U1", "U2"]:
            # BMP581: small IC, place vias left and right
            if pn in ["3"]:  # top-left pad → via left
                vx, vy = px - 1.5, py
            elif pn in ["8"]:  # bottom-right → via right
                vx, vy = px + 1.5, py
            elif pn in ["5"]:  # left-bottom → via below
                vx, vy = px - 1.5, py + 1.0
            elif pn in ["9"]:  # right-top → via right
                vx, vy = px + 1.5, py

        elif ref == "U3":
            if pn == "1":  # left-top → via left
                vx, vy = px - 1.5, py
            elif pn in ["6", "7"]:  # bottom → via below
                vx, vy = px, py + 1.2

        elif ref == "U4":
            # BNO085 large IC — place vias outside the package
            if pn in ["1", "21", "22", "24", "25", "26"]:  # north side
                vx, vy = px, py - 1.5
            elif pn in ["2", "5"]:  # west side
                vx, vy = px - 1.5, py
            elif pn in ["8", "10", "12", "13"]:  # south side
                vx, vy = px, py + 1.5
            elif pn == "17":  # east side
                vx, vy = px + 1.5, py

        elif ref == "C4":
            vx, vy = px + 1.0, py

        elif ref == "U5":
            vx, vy = px + 1.0, py

        # Round to 0.25mm grid
        vx = round(vx * 4) / 4
        vy = round(vy * 4) / 4

        # Avoid duplicates (within 0.5mm)
        too_close = False
        for ex, ey in via_positions:
            if abs(ex - vx) < 0.5 and abs(ey - vy) < 0.5:
                too_close = True
                break

        if not too_close and 0.5 < vx < 34.5 and 0.5 < vy < 34.5:
            via_positions.add((vx, vy))
            # Add short track from pad to via location
            add_track(board, px, py, vx, vy, "GND", pcbnew.F_Cu, 0.15)
            add_via(board, vx, vy, "GND")
            print(f"  Via at ({vx:.2f}, {vy:.2f}) for {ref}.{pn}")

    # Add general stitching vias in corners and edges
    corner_vias = [
        (2.0, 2.0), (33.0, 2.0), (2.0, 33.0), (33.0, 33.0),
        (17.5, 2.0), (2.0, 17.5), (33.0, 17.5), (17.5, 33.0),
    ]
    for cx, cy in corner_vias:
        too_close = False
        for ex, ey in via_positions:
            if abs(ex - cx) < 1.0 and abs(ey - cy) < 1.0:
                too_close = True
                break
        if not too_close:
            add_via(board, cx, cy, "GND")
            print(f"  Corner via at ({cx:.2f}, {cy:.2f})")

    # === Fix 3: Fill zones ===
    print("\nFix 3: Filling zones...")
    board.Save(PCB_FILE)

    # Reload and fill
    board2 = pcbnew.LoadBoard(PCB_FILE)
    filler = pcbnew.ZONE_FILLER(board2)
    filler.Fill(board2.Zones())
    board2.Save(PCB_FILE)
    print("  Zones filled successfully")

    print(f"\nPCB saved to: {PCB_FILE}")
    print("\nREMAINING MANUAL WORK:")
    print("  - U5 pads 1 (I2C_SDA), 2 (I2C_SCL), 3 (+3V3) need routing")
    print("  - C5 pad 1 (+3V3) needs routing")
    print("  - Check if GND vias resolved all unconnected GND pads")


if __name__ == "__main__":
    fix()
