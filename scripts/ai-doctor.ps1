# scripts/ai-doctor.ps1 - S3-Forge health check (ASCII-only; PS 5.1 safe).
# Run in a CLEAN PowerShell (the Claude Code PowerShell tool), NOT from git-bash,
# so the idf.py check is not polluted by MSYS env. Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/ai-doctor.ps1
$ErrorActionPreference = "Continue"
$proj = Split-Path -Parent $PSScriptRoot   # WORKplace root (script is in scripts/)
Set-Location $proj
$pass = 0; $warn = 0; $fail = 0
function Ok($m){ Write-Output ("[OK]   " + $m); $script:pass++ }
function Wn($m){ Write-Output ("[WARN] " + $m); $script:warn++ }
function Fl($m){ Write-Output ("[FAIL] " + $m); $script:fail++ }

Write-Output "==== S3-Forge ai-doctor ===="
Write-Output ("project: " + $proj)

# 1) idf.ps1 + IDF version
if (Test-Path "scripts\idf.ps1") {
    $v = ""
    try { $v = (& "scripts\idf.ps1" --version 2>$null | Select-Object -Last 1) } catch { $v = "" }
    if ($v -match "5\.5\.4") { Ok ("IDF activation + version: " + $v) }
    elseif ($v) { Wn ("IDF version not 5.5.4: " + $v) }
    else { Wn "idf.ps1 ran but no version (run in clean PowerShell, not git-bash)" }
} else { Fl "scripts/idf.ps1 missing" }

# 2) venv + deps
$vpy = ".venv-tools\Scripts\python.exe"
if (Test-Path $vpy) {
    $env:PYTHONIOENCODING = "utf-8"
    $imp = (& $vpy -c "import serial,cv2; import mcp.server.fastmcp; print('ok')" 2>$null)
    if ($imp -eq "ok") { Ok ".venv-tools deps (pyserial/cv2/mcp) import OK" }
    else { Fl ".venv-tools deps import FAILED" }
} else { Fl ".venv-tools/Scripts/python.exe missing (Phase 0)" }

# 3) settings.json valid + hooks
if (Test-Path ".claude\settings.json") {
    if (Test-Path $vpy) {
        $ok = (& $vpy -c "import json;d=json.load(open('.claude/settings.json',encoding='utf-8'));print('Y' if d.get('hooks') else 'N')" 2>$null)
        if ($ok -eq "Y") { Ok ".claude/settings.json valid with hooks" }
        elseif ($ok -eq "N") { Wn ".claude/settings.json valid but no hooks" }
        else { Fl ".claude/settings.json INVALID JSON" }
    } else { Wn ".claude/settings.json present (cannot validate without venv)" }
} else { Fl ".claude/settings.json missing" }

# 4) hook scripts
$hooks = @("guard.py","session_doctor.py","post_build_log.py","ralph_stop.py")
$missing = @(); foreach ($h in $hooks){ if (-not (Test-Path (".claude\hooks\" + $h))){ $missing += $h } }
if ($missing.Count -eq 0) { Ok ("hooks present: " + ($hooks -join ", ")) }
else { Fl ("hooks missing: " + ($missing -join ", ")) }

# 5) .mcp.json (Phase 2)
if (Test-Path ".mcp.json") { Ok ".mcp.json present" } else { Wn ".mcp.json missing (Phase 2: espressif-docs + idf-bridge)" }

# 6) target esp32s3
if (Test-Path "sdkconfig") {
    $t = (Select-String -Path "sdkconfig" -Pattern '^CONFIG_IDF_TARGET=' -SimpleMatch:$false | Select-Object -First 1)
    if ($t -and ($t.Line -match 'esp32s3')) { Ok "sdkconfig target = esp32s3" }
    elseif ($t) { Fl ("sdkconfig target NOT esp32s3: " + $t.Line) }
    else { Wn "sdkconfig has no CONFIG_IDF_TARGET" }
} else { Wn "no sdkconfig yet (run set-target esp32s3)" }

# 7) BOARD.md filled
if (Test-Path "docs\ai\BOARD.md") {
    $todo = (Select-String -Path "docs\ai\BOARD.md" -Pattern "TODO" | Measure-Object).Count
    if ($todo -eq 0) { Ok "BOARD.md filled" } else { Wn ("BOARD.md has " + $todo + " TODO(s) - fill before hardware") }
} else { Wn "docs/ai/BOARD.md missing" }

Write-Output "==== summary ===="
Write-Output ("PASS=" + $pass + "  WARN=" + $warn + "  FAIL=" + $fail)
if ($fail -gt 0) { exit 1 } else { exit 0 }
