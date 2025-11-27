# Find the most recent esp32 crash log in reporoot/logs
# This script is expected to live in: reporoot/tools/stream_logs.ps1

# Determine repo root from script location
if ($PSScriptRoot) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
} else {
    # Fallback if run in a strange context: use current directory as tools/, so one level up is repo root
    $repoRoot = Split-Path -Parent (Get-Location).Path
}

$logDir = Join-Path $repoRoot "logs"

if (-not (Test-Path $logDir)) {
    Write-Host "Log directory not found: $logDir"
    exit 1
}

$latest = Get-ChildItem $logDir -Filter 'esp32-crash-logs-*.txt' -File |
          Sort-Object LastWriteTime -Descending |
          Select-Object -First 1

if (-not $latest) {
    Write-Host "No log files found in $logDir matching esp32-crash-logs-*.txt"
    exit 1
}

Write-Host "Tailing: $($latest.FullName)"
Get-Content -Path $latest.FullName -Wait