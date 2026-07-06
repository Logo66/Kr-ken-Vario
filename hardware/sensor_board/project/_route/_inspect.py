import pcbnew
b = pcbnew.LoadBoard(r"C:\Users\Ivo\aura_kruecke\hardware\sensor_board\project\_route\sb_final_routed.kicad_pcb")
TMM = pcbnew.ToMM
from collections import Counter
cnt = Counter()
for t in b.GetTracks():
    if t.GetClass() == "PCB_TRACK":
        cnt[(t.GetNetname(), b.GetLayerName(t.GetLayer()))] += 1
for k, v in sorted(cnt.items()):
    if k[0] in ("I2C_SCL", "I2C_SDA", "+3V3", "GND"):
        print("track", k, v)
for net in ("I2C_SCL", "I2C_SDA"):
    xs = []; ys = []
    for t in b.GetTracks():
        if t.GetClass() == "PCB_TRACK" and t.GetNetname() == net:
            for p in (t.GetStart(), t.GetEnd()):
                xs.append(TMM(p.x)); ys.append(TMM(p.y))
    if xs:
        print(net, "track span x", round(min(xs), 1), "-", round(max(xs), 1), "y", round(min(ys), 1), "-", round(max(ys), 1))
for fp in b.GetFootprints():
    for pad in fp.Pads():
        ref = fp.GetReference() + "." + pad.GetNumber()
        if ref in ("U1.2", "U1.4", "U5.1", "U5.2", "U3.13", "U4.26", "U4.28"):
            pos = pad.GetPosition()
            print("PAD", ref, pad.GetNetname(), round(TMM(pos.x), 2), round(TMM(pos.y), 2),
                  "onF", pad.IsOnLayer(pcbnew.F_Cu), "onB", pad.IsOnLayer(pcbnew.B_Cu))
