"""Diagnostic: dump SDA/SCL track endpoints and existing vias to plan routing."""
import pcbnew, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_DIR   = os.path.dirname(SCRIPT_DIR)
PCB_FILE   = os.path.join(BASE_DIR, "project", "sensor_board.kicad_pcb")
TMM = pcbnew.ToMM

board = pcbnew.LoadBoard(PCB_FILE)

print("=== SDA/SCL tracks on F.Cu ===")
for nn in ("I2C_SDA", "I2C_SCL"):
    print(f"\n--- {nn} ---")
    segments = []
    for item in board.GetTracks():
        if isinstance(item, pcbnew.PCB_TRACK) and not isinstance(item, pcbnew.PCB_VIA):
            net = item.GetNet()
            if net and net.GetNetname() == nn and item.GetLayer() == pcbnew.F_Cu:
                s, e = item.GetStart(), item.GetEnd()
                segments.append((TMM(s.x), TMM(s.y), TMM(e.x), TMM(e.y)))
    segments.sort(key=lambda s: (s[1], s[0]))
    for sx, sy, ex, ey in segments:
        print(f"  ({sx:.2f},{sy:.2f}) -> ({ex:.2f},{ey:.2f})")
    print(f"  Total: {len(segments)} segments")

print("\n=== SDA/SCL tracks on B.Cu ===")
for nn in ("I2C_SDA", "I2C_SCL"):
    count = 0
    for item in board.GetTracks():
        if isinstance(item, pcbnew.PCB_TRACK) and not isinstance(item, pcbnew.PCB_VIA):
            net = item.GetNet()
            if net and net.GetNetname() == nn and item.GetLayer() == pcbnew.B_Cu:
                s, e = item.GetStart(), item.GetEnd()
                print(f"  {nn} B.Cu: ({TMM(s.x):.2f},{TMM(s.y):.2f}) -> ({TMM(e.x):.2f},{TMM(e.y):.2f})")
                count += 1
    if count == 0:
        print(f"  {nn}: no B.Cu tracks")

print("\n=== Vias on SDA/SCL nets ===")
for item in board.GetTracks():
    if isinstance(item, pcbnew.PCB_VIA):
        net = item.GetNet()
        nn = net.GetNetname() if net else ""
        if nn in ("I2C_SDA", "I2C_SCL"):
            p = item.GetPosition()
            print(f"  {nn} via at ({TMM(p.x):.2f},{TMM(p.y):.2f})")

print("\n=== All F.Cu tracks crossing y=29.5 (x=3 to 9) ===")
for item in board.GetTracks():
    if isinstance(item, pcbnew.PCB_TRACK) and not isinstance(item, pcbnew.PCB_VIA):
        if item.GetLayer() == pcbnew.F_Cu:
            s, e = item.GetStart(), item.GetEnd()
            sx, sy, ex, ey = TMM(s.x), TMM(s.y), TMM(e.x), TMM(e.y)
            min_y, max_y = min(sy, ey), max(sy, ey)
            min_x, max_x = min(sx, ex), max(sx, ex)
            if min_y <= 29.5 <= max_y and min_x <= 9.0 and max_x >= 3.0:
                net = item.GetNet()
                nn = net.GetNetname() if net else ""
                print(f"  {nn}: ({sx:.2f},{sy:.2f}) -> ({ex:.2f},{ey:.2f})")

print("\n=== All F.Cu tracks crossing x=8.15 (y=22 to 35) ===")
for item in board.GetTracks():
    if isinstance(item, pcbnew.PCB_TRACK) and not isinstance(item, pcbnew.PCB_VIA):
        if item.GetLayer() == pcbnew.F_Cu:
            s, e = item.GetStart(), item.GetEnd()
            sx, sy, ex, ey = TMM(s.x), TMM(s.y), TMM(e.x), TMM(e.y)
            min_x, max_x = min(sx, ex), max(sx, ex)
            min_y, max_y = min(sy, ey), max(sy, ey)
            if min_x <= 8.15 <= max_x and max_y >= 22.0 and min_y <= 35.0:
                net = item.GetNet()
                nn = net.GetNetname() if net else ""
                print(f"  {nn}: ({sx:.2f},{sy:.2f}) -> ({ex:.2f},{ey:.2f})")

print("\n=== All B.Cu signal tracks (non-GND) ===")
for item in board.GetTracks():
    if isinstance(item, pcbnew.PCB_TRACK) and not isinstance(item, pcbnew.PCB_VIA):
        if item.GetLayer() == pcbnew.B_Cu:
            net = item.GetNet()
            nn = net.GetNetname() if net else ""
            if nn and nn != "GND":
                s, e = item.GetStart(), item.GetEnd()
                print(f"  {nn}: ({TMM(s.x):.2f},{TMM(s.y):.2f}) -> ({TMM(e.x):.2f},{TMM(e.y):.2f})")
