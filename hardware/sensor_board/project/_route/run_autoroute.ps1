# run_autoroute.ps1  -  FreeRouting add-on pipeline for the AURA sensor board
#
# Solves the things plain FreeRouting can't on this board:
#   * GUI-thread hang on SES export   -> headless via JAVA_TOOL_OPTIONS
#   * fine-pitch LGA plane fanout      -> fanout.py adds locked GND/+3V3 vias+stubs
#   * tight clearances for 0.4mm pitch -> finalize.py relaxes to 0.10mm (JLC-OK)
#
# Steps: fanout -> Specctra DSN -> FreeRouting (headless) -> import SES + fill -> DRC
#
# Usage:  powershell -File run_autoroute.ps1 [prepBoard.kicad_pcb]
#   prepBoard defaults to sb_solid2_prep.kicad_pcb (placed, planes, 0 tracks).

param([string]$Prep)

$wd  = "C:\Users\Ivo\aura_kruecke\hardware\sensor_board\project\_route"
$py  = "C:\Program Files\KiCad\10.0\bin\python.exe"
$cli = "C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
$jar = "C:\Users\Ivo\aura_kruecke\hardware\sensor_board\archive\_pre_v1_20260524\freerouting.jar"
if (-not $Prep) { $Prep = "$wd\sb_solid2_prep.kicad_pcb" }

$fanoutPrep = "$wd\sb_fanout_prep.kicad_pcb"
$dsn = "$wd\sb_fanout.dsn"
$ses = "$wd\sb_route.ses"
$out = "$wd\sb_final_routed.kicad_pcb"
$drc = "$wd\drc_final.rpt"

Write-Host "[1/5] fanout vias on fine-pitch LGAs..."
& $py "$wd\fanout.py" $Prep $fanoutPrep 2>$null | Out-Null
Get-Content "$wd\fanout.log" | Select-Object -First 2

Write-Host "[2/5] export Specctra DSN..."
& $py "$wd\export_dsn.py" $fanoutPrep $dsn 2>$null | Out-Null

Write-Host "[3/5] FreeRouting (headless)..."
if (Test-Path $ses) { Move-Item $ses "$ses.bak" -Force }
$env:JAVA_TOOL_OPTIONS = "-Djava.awt.headless=true"   # avoids the v2.0.1 GUI-thread SES-export hang
& java -jar $jar -de $dsn -do $ses -mp 30 | Out-Null
Remove-Item Env:\JAVA_TOOL_OPTIONS -ErrorAction SilentlyContinue

Write-Host "[4/5] import SES + fill zones..."
& $py "$wd\finalize.py" $fanoutPrep $ses $out 0.1 2>$null | Out-Null
Get-Content "$wd\finalize.log" | Select-Object -Last 2

Write-Host "[5/5] DRC..."
& $cli pcb drc --severity-error --exit-code-violations -o $drc $out 2>&1 | Out-String | Write-Host
$c = Get-Content $drc -Raw
Write-Host "Violation types:"
[regex]::Matches($c,'\[(\w+)\]:') | ForEach-Object { $_.Groups[1].Value } | Group-Object | Sort-Object Count -Descending | ForEach-Object { "  {0,3}  {1}" -f $_.Count, $_.Name }
Write-Host "Done -> $out"
