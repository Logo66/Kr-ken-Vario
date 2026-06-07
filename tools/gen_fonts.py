"""Generate Arial Bold fonts for epdiy in 3 sizes."""
import subprocess, sys, os

FONTCONV = r"C:\Users\Ivo\aura_kruecke\.pio\libdeps\t5_epaper_s3_pro\epdiy\scripts\fontconvert.py"
FONT = r"C:\Windows\Fonts\arialbd.ttf"
OUTDIR = r"C:\Users\Ivo\aura_kruecke\include"
CHARS = r""" !"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_abcdefghijklmnopqrstuvwxyz{|}~"""

SIZES = [
    ("ArialBold40", 40),
    ("ArialBold28", 28),
    ("ArialBold16", 16),
]

for name, size in SIZES:
    outfile = os.path.join(OUTDIR, f"{name.lower()}.h")
    cmd = [sys.executable, FONTCONV, name, str(size), FONT, "--compress", "--string", CHARS]
    print(f"Generating {name} @ {size}pt -> {outfile}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  WARNINGS (non-fatal): {result.stderr[:200]}")
    with open(outfile, "w", encoding="utf-8") as f:
        f.write(result.stdout)
    fsize = os.path.getsize(outfile)
    print(f"  OK: {fsize} bytes")

print("Done!")
