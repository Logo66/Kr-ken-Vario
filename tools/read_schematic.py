import fitz, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
doc = fitz.open(r"C:\Users\Ivo\aura_kruecke\docs\T5_EPaper_S3_Pro_Schematic.pdf")
for i, page in enumerate(doc):
    print(f"\n=== PAGE {i+1} ===")
    for line in page.get_text().split('\n'):
        line = line.strip()
        if line:
            print(line)
