import pcbnew, time
t0 = time.time()
b = pcbnew.LoadBoard(r"C:\Users\Ivo\aura_kruecke\hardware\sensor_board\project\_route\sb_final_routed.kicad_pcb")
print("LoadBoard %.2fs" % (time.time() - t0))
nf = len(list(b.GetFootprints())); nt = len(list(b.GetTracks()))
print("footprints", nf, "tracks/vias", nt, "%.2fs" % (time.time() - t0))
