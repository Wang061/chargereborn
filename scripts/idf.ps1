# scripts/idf.ps1 - S3-Forge single ESP-IDF 5.5.4 activation entry point.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 <idf.py args...>
#   e.g. powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 build
#        powershell -ExecutionPolicy Bypass -File scripts/idf.ps1 set-target esp32s3
# Notes:
#   1) MUST dot-source (. ) Initialize-Idf.ps1: it defines idf.py as a *function* (line 48);
#      only dot-sourcing keeps that function + env vars in this script's scope (& would lose them).
#   2) Do NOT apply *> redirection to the dot-source statement (it breaks execution).
#      Use `$null =` to swallow the noisy success-stream output instead.
#   3) IDF activation progress ("Activating ESP-IDF 5.5" etc.) goes to STDERR, so STDOUT stays
#      clean with only idf.py output - easy for idf-bridge / the agent to parse.
#   4) ASCII-only on purpose: Windows PowerShell 5.1 mis-decodes UTF-8-without-BOM non-ASCII
#      comments as GBK and throws a ParserError. Keep all .ps1 files ASCII.
param([Parameter(ValueFromRemainingArguments = $true)]$IdfArgs)

$ProjDir = (Get-Location).Path
$IdfId   = "esp-idf-664c96ea6a8aaf556c400ba925468017"   # ESP-IDF v5.5.4 @ D:\WJ\Espressif

# Activate: dot-source; $null= swallows verbose success stream; scope preserved -> idf.py usable.
$null = . "D:\WJ\Espressif\Initialize-Idf.ps1" -IdfId $IdfId
Set-Location $ProjDir

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    [Console]::Error.WriteLine("[idf.ps1] IDF activation failed: idf.py not defined. Check IdfId / D:\WJ\Espressif install.")
    exit 90
}

idf.py @IdfArgs
exit $LASTEXITCODE
