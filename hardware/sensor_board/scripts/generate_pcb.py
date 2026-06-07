"""
Generate sensor_board.kicad_pcb for AURA-KRUECKE Sensor Board V1.

Footprints + Netze + Board-Outline + Zonen.
KEINE Tracks/Vias — Platzierung von Hand, Routing via FreeRouting.

Run with KiCad's Python:
  "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" generate_pcb.py
"""

import pcbnew
import os

# --------------- paths ---------------
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_DIR   = os.path.dirname(SCRIPT_DIR)
PROJECT_DIR = os.path.join(BASE_DIR, "project")
LIB_DIR     = os.path.join(BASE_DIR, "lib")
CUSTOM_FP   = os.path.join(LIB_DIR, "aura_sensors.pretty")
KICAD_FP    = r"C:\Program Files\KiCad\10.0\share\kicad\footprints"
OUTPUT_PCB  = os.path.join(PROJECT_DIR, "sensor_board.kicad_pcb")

# --------------- helpers ---------------
MM  = pcbnew.FromMM
TMM = pcbnew.ToMM

def vec(x_mm, y_mm):
    return pcbnew.VECTOR2I(MM(x_mm), MM(y_mm))

def fp_lib(lib_name):
    return os.path.join(KICAD_FP, f"{lib_name}.pretty")


# --------------- BMP581 footprint creation ---------------
def create_bmp581_footprint():
    """Create BMP581 LGA-10 2x2mm footprint with 0.4mm pitch."""
    fp = pcbnew.FOOTPRINT(None)
    fp.SetFPID(pcbnew.LIB_ID("aura_sensors", "BMP581_LGA-10_2x2mm"))
    fp.SetLibDescription("Bosch BMP581 LGA-10, 2x2x0.75mm, 0.4mm pitch")
    fp.SetKeywords("BMP581 LGA barometer pressure")
    fp.SetAttributes(pcbnew.FP_SMD)

    pad_defs = [
        ("1",   0.40, -0.85,  0.25, 0.40),
        ("2",   0.00, -0.85,  0.25, 0.40),
        ("3",  -0.40, -0.85,  0.25, 0.40),
        ("4",  -0.85, -0.20,  0.40, 0.25),
        ("5",  -0.85,  0.20,  0.40, 0.25),
        ("6",  -0.40,  0.85,  0.25, 0.40),
        ("7",   0.00,  0.85,  0.25, 0.40),
        ("8",   0.40,  0.85,  0.25, 0.40),
        ("9",   0.85,  0.20,  0.40, 0.25),
        ("10",  0.85, -0.20,  0.40, 0.25),
    ]

    for pin, px, py, pw, ph in pad_defs:
        pad = pcbnew.PAD(fp)
        pad.SetNumber(pin)
        pad.SetShape(pcbnew.PAD_SHAPE_ROUNDRECT)
        pad.SetRoundRectRadiusRatio(0.25)
        pad.SetAttribute(pcbnew.PAD_ATTRIB_SMD)
        pad.SetLayerSet(pcbnew.PAD(fp).SMDMask())
        pad.SetPosition(vec(px, py))
        pad.SetSize(pcbnew.VECTOR2I(MM(pw), MM(ph)))
        fp.Add(pad)

    cy = pcbnew.PCB_SHAPE(fp)
    cy.SetShape(pcbnew.SHAPE_T_RECT)
    cy.SetStart(vec(-1.3, -1.3))
    cy.SetEnd(vec(1.3, 1.3))
    cy.SetLayer(pcbnew.F_CrtYd)
    cy.SetWidth(MM(0.05))
    fp.Add(cy)

    fab = pcbnew.PCB_SHAPE(fp)
    fab.SetShape(pcbnew.SHAPE_T_RECT)
    fab.SetStart(vec(-1.0, -1.0))
    fab.SetEnd(vec(1.0, 1.0))
    fab.SetLayer(pcbnew.F_Fab)
    fab.SetWidth(MM(0.1))
    fp.Add(fab)

    marker = pcbnew.PCB_SHAPE(fp)
    marker.SetShape(pcbnew.SHAPE_T_CIRCLE)
    marker.SetStart(vec(0.6, -1.4))
    marker.SetEnd(vec(0.7, -1.4))
    marker.SetLayer(pcbnew.F_SilkS)
    marker.SetWidth(MM(0.12))
    fp.Add(marker)

    ref_text = fp.Reference()
    ref_text.SetPosition(vec(0, -2.0))
    ref_text.SetTextSize(pcbnew.VECTOR2I(MM(0.8), MM(0.8)))
    ref_text.SetTextThickness(MM(0.12))

    val_text = fp.Value()
    val_text.SetPosition(vec(0, 2.0))
    val_text.SetTextSize(pcbnew.VECTOR2I(MM(0.8), MM(0.8)))
    val_text.SetTextThickness(MM(0.12))

    os.makedirs(CUSTOM_FP, exist_ok=True)
    pcbnew.FootprintSave(CUSTOM_FP, fp)
    print(f"BMP581 footprint saved to {CUSTOM_FP}")
    return fp


# --------------- Net-to-pad mapping (from schematic) ---------------
PAD_NETS = {
    "J1": {"1": "GND", "2": "+3V3", "3": "I2C_SDA", "4": "I2C_SCL", "MP": "GND"},
    "R1": {"1": "+3V3", "2": "I2C_SDA"},
    "R2": {"1": "+3V3", "2": "I2C_SCL"},
    "C6": {"1": "+3V3", "2": "GND"},
    "U1": {"1": "+3V3", "2": "I2C_SCL", "3": "GND", "4": "I2C_SDA",
           "5": "+3V3", "6": "+3V3", "7": None, "8": "GND", "9": "GND", "10": "+3V3"},
    "C1": {"1": "+3V3", "2": "GND"},
    "U2": {"1": "+3V3", "2": "I2C_SCL", "3": "GND", "4": "I2C_SDA",
           "5": "GND", "6": "+3V3", "7": None, "8": "GND", "9": "GND", "10": "+3V3"},
    "C2": {"1": "+3V3", "2": "GND"},
    "U3": {"1": "GND", "2": None, "3": None, "4": "INT_LSM",
           "5": "+3V3", "6": "GND", "7": "GND", "8": "+3V3",
           "9": None, "10": None, "11": None, "12": "+3V3",
           "13": "I2C_SCL", "14": "I2C_SDA"},
    "C3": {"1": "+3V3", "2": "GND"},
    "U4": {"1": "GND", "2": "GND", "3": "+3V3", "4": "BOOTN_N",
           "5": "GND", "6": "GND", "7": "GND", "8": "GND",
           "9": "CAP_BNO", "10": "GND", "11": "RST_BNO", "12": "GND",
           "13": "GND", "14": "INT_BNO", "15": None, "16": None,
           "17": "GND", "18": "+3V3", "19": "I2C_SCL", "20": "I2C_SDA",
           "21": "GND", "22": "GND", "23": "GND", "24": "GND",
           "25": "GND", "26": "GND", "27": None, "28": "+3V3"},
    "C4": {"1": "+3V3", "2": "GND"},
    "C7": {"1": "CAP_BNO", "2": "GND"},
    "R3": {"1": "+3V3", "2": "BOOTN_N"},
    "R4": {"1": "+3V3", "2": "RST_BNO"},
    "U5": {"1": "I2C_SDA", "2": "I2C_SCL", "3": "+3V3", "4": "GND"},
    "C5": {"1": "+3V3", "2": "GND"},
}


# --------------- Component placement (initial, user will re-place) ---------------
PLACEMENT = [
    ("J1",  fp_lib("Connector_JST"),
            "JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal",
            8.0, 2.0, 180.0, "Qwiic"),

    ("R1",  fp_lib("Resistor_SMD"), "R_0402_1005Metric",
            12.0, 6.5, 90.0, "4.7k"),
    ("R2",  fp_lib("Resistor_SMD"), "R_0402_1005Metric",
            14.5, 6.5, 90.0, "4.7k"),
    ("C6",  fp_lib("Capacitor_SMD"), "C_0805_2012Metric",
            18.0, 6.5, 90.0, "10u"),

    ("U1",  CUSTOM_FP, "BMP581_LGA-10_2x2mm",
            10.0, 11.5, 0.0, "BMP581"),
    ("C1",  fp_lib("Capacitor_SMD"), "C_0402_1005Metric",
            14.0, 11.5, 90.0, "100n"),

    ("U2",  CUSTOM_FP, "BMP581_LGA-10_2x2mm",
            10.0, 16.5, 0.0, "BMP581"),
    ("C2",  fp_lib("Capacitor_SMD"), "C_0402_1005Metric",
            14.0, 16.5, 90.0, "100n"),

    ("U3",  fp_lib("Package_LGA"), "Bosch_LGA-14_3x2.5mm_P0.5mm",
            10.0, 21.5, 0.0, "LSM6DSO32"),
    ("C3",  fp_lib("Capacitor_SMD"), "C_0402_1005Metric",
            14.0, 21.5, 90.0, "100n"),

    ("U4",  fp_lib("Package_LGA"), "LGA-28_5.2x3.8mm_P0.5mm",
            17.5, 28.0, 0.0, "BNO085"),
    ("C4",  fp_lib("Capacitor_SMD"), "C_0402_1005Metric",
            24.0, 26.0, 90.0, "100n"),
    ("C7",  fp_lib("Capacitor_SMD"), "C_0402_1005Metric",
            24.0, 30.0, 90.0, "100n"),
    ("R3",  fp_lib("Resistor_SMD"), "R_0402_1005Metric",
            26.5, 26.0, 90.0, "10k"),
    ("R4",  fp_lib("Resistor_SMD"), "R_0402_1005Metric",
            29.0, 26.0, 90.0, "10k"),

    ("U5",  fp_lib("Sensor_Humidity"),
            "Sensirion_DFN-4_1.5x1.5mm_P0.8mm_SHT4x_NoCentralPad",
            6.0, 32.0, 0.0, "SHT40"),
    ("C5",  fp_lib("Capacitor_SMD"), "C_0402_1005Metric",
            10.0, 32.0, 90.0, "100n"),

]


# --------------- Main ---------------
def generate():
    print("Creating BMP581 footprint...")
    create_bmp581_footprint()

    print("Creating PCB board...")
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(4)

    ds = board.GetDesignSettings()
    ds.SetBoardThickness(MM(1.6))
    ds.m_TrackMinWidth = MM(0.1)
    ds.m_ViasMinSize = MM(0.5)
    ds.m_ViasMinDrill = MM(0.3)
    ds.m_CopperEdgeClearance = MM(0.0)
    try:
        ds.m_MinClearance = MM(0.09)
    except Exception:
        pass

    # ----- Create nets -----
    net_names = ["+3V3", "GND", "I2C_SCL", "I2C_SDA",
                 "BOOTN_N", "CAP_BNO", "INT_BNO", "INT_LSM", "RST_BNO"]
    nets = {}
    for name in net_names:
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        nets[name] = ni
    print(f"  Created {len(nets)} nets")

    # ----- Board outline 35x35mm -----
    BW, BH = 35.0, 35.0
    for (x1, y1), (x2, y2) in [((0,0),(BW,0)), ((BW,0),(BW,BH)),
                                 ((BW,BH),(0,BH)), ((0,BH),(0,0))]:
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(vec(x1, y1)); seg.SetEnd(vec(x2, y2))
        seg.SetLayer(pcbnew.Edge_Cuts); seg.SetWidth(MM(0.05))
        board.Add(seg)
    print(f"  Board outline: {BW}x{BH}mm")

    # ----- Place components -----
    footprints = {}
    for ref, lib_path, fp_name, cx, cy, angle, value in PLACEMENT:
        fp = pcbnew.FootprintLoad(lib_path, fp_name)
        fp.SetPosition(vec(cx, cy))
        fp.SetOrientationDegrees(angle)
        fp.SetReference(ref)
        fp.Value().SetText(value)
        board.Add(fp)
        footprints[ref] = fp

    # Assign nets to pads
    for ref, pad_map in PAD_NETS.items():
        fp = footprints.get(ref)
        if not fp:
            continue
        for pad in fp.Pads():
            pnum = pad.GetNumber()
            net_name = pad_map.get(pnum)
            if net_name and net_name in nets:
                pad.SetNet(nets[net_name])
    print(f"  Placed {len(footprints)} components with net assignments")

    # Print pad positions for reference
    for ref in sorted(footprints.keys(), key=lambda r: PLACEMENT[[p[0] for p in PLACEMENT].index(r)][3]):
        fp = footprints[ref]
        pads_str = ", ".join(
            f"{p.GetNumber()}:({TMM(p.GetPosition().x):.2f},{TMM(p.GetPosition().y):.2f})"
            for p in sorted(fp.Pads(), key=lambda p: int(p.GetNumber()) if p.GetNumber().isdigit() else 99)
        )
        print(f"    {ref}: {pads_str}")

    # ----- Copper zones (4-layer stackup) -----
    margin = 0.3
    zc = [(margin, margin), (BW-margin, margin),
          (BW-margin, BH-margin), (margin, BH-margin)]

    def add_zone(layer, net_name, prio, connection=pcbnew.ZONE_CONNECTION_THERMAL):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer); z.SetNet(nets[net_name])
        z.SetAssignedPriority(prio); z.SetMinThickness(MM(0.15))
        z.SetThermalReliefGap(MM(0.2))
        z.SetThermalReliefSpokeWidth(MM(0.25))
        z.SetPadConnection(connection)
        ol = z.Outline(); ol.NewOutline()
        for x, y in zc:
            ol.Append(MM(x), MM(y))
        board.Add(z)

    add_zone(pcbnew.F_Cu,   "GND",  1, pcbnew.ZONE_CONNECTION_THERMAL)
    add_zone(pcbnew.In1_Cu, "GND",  0, pcbnew.ZONE_CONNECTION_THERMAL)
    add_zone(pcbnew.In2_Cu, "+3V3", 0, pcbnew.ZONE_CONNECTION_THERMAL)
    add_zone(pcbnew.B_Cu,   "GND",  0, pcbnew.ZONE_CONNECTION_THERMAL)
    print("  Added copper zones (GND on F/In1/B.Cu, +3V3 on In2.Cu)")

    # ----- Save -----
    board.Save(OUTPUT_PCB)
    print(f"\nPCB saved to: {OUTPUT_PCB}")
    print(f"Board: {BW}x{BH}mm, 4 layers, {len(footprints)} components")
    print("No routing — place components, then use FreeRouting.")

    # Fill zones
    try:
        print("  Filling zones...")
        board2 = pcbnew.LoadBoard(OUTPUT_PCB)
        filler = pcbnew.ZONE_FILLER(board2)
        filler.Fill(board2.Zones())
        board2.Save(OUTPUT_PCB)
        print("  Zones filled successfully")
    except Exception as e:
        print(f"  Zone fill skipped ({e}) — will fill on open in KiCad")


if __name__ == "__main__":
    generate()
