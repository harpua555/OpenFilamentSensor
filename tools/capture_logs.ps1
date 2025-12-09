# Continuous log capture from ESP32 device
# Expected location: reporoot/tools/capture-logs.ps1
# Logs will be written to:  reporoot/logs

param(
    [string]$DeviceIP = "192.168.0.153"
)

# Determine repo root from script location (one level up from tools/)
if ($PSScriptRoot) {
    $repoRoot = Split-Path -Parent $PSScriptRoot
} else {
    # Fallback if run in a strange context: assume current dir is tools/
    $repoRoot = Split-Path -Parent (Get-Location).Path
}

# reporoot/logs folder
$logDir = Join-Path $repoRoot 'logs'

if (-not (Test-Path $logDir)) {
    New-Item -ItemType Directory -Path $logDir | Out-Null
}

# Timestamped log file, e.g. esp32-crash-logs-20251121-123045.txt
$timestamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
$outputFile = Join-Path $logDir "esp32-crash-logs-$timestamp.txt"

$endpoint = "http://${DeviceIP}/api/logs_live"

Write-Host "Starting log capture from $endpoint" -ForegroundColor Green
Write-Host "Saving to: $outputFile" -ForegroundColor Green
Write-Host "Press Ctrl+C to stop" -ForegroundColor Yellow
Write-Host ""

# Clear the file if it exists / add header
"=== Log capture started at $(Get-Date) ===" | Out-File -FilePath $outputFile -Encoding UTF8

$iteration = 0
$lastSeenLines = @()

while ($true) {
    try {
        $iteration++
        $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"

        # Fetch logs from device
        $response = Invoke-WebRequest -Uri $endpoint -TimeoutSec 5 -UseBasicParsing
        $currentLines = $response.Content -split "`n" | Where-Object { $_.Trim() -ne "" }

        # Find new lines (lines we haven't seen before)
        $newLines = @()
        if ($lastSeenLines.Count -eq 0) {
            # First iteration - capture everything
            $newLines = $currentLines
        } else {
            # Use the last actual log line as anchor
            $lastLine = $lastSeenLines[-1]
            $foundIndex = -1
            for ($i = $currentLines.Count - 1; $i -ge 0; $i--) {
                if ($currentLines[$i] -eq $lastLine) {
                    $foundIndex = $i
                    break
                }
            }

            if ($foundIndex -ge 0 -and $foundIndex -lt $currentLines.Count - 1) {
                # Found the last line, capture everything after it
                $newLines = $currentLines[($foundIndex + 1)..($currentLines.Count - 1)]
            } elseif ($foundIndex -eq -1) {
                # Didn't find last line - log buffer may have wrapped, capture all
                $newLines = $currentLines
                Write-Host "[$timestamp] WARNING: Log buffer wrapped, capturing all entries" -ForegroundColor Yellow
            }
            # If foundIndex is the last line, there are no new lines
        }

        # Write new lines to file
        if ($newLines.Count -gt 0) {
            $newLines | Out-File -FilePath $outputFile -Append -Encoding UTF8
            Write-Host "[$timestamp] Captured $($newLines.Count) new log entries (iteration $iteration)" -ForegroundColor Cyan
        } else {
            Write-Host "[$timestamp] No new log entries (iteration $iteration)" -ForegroundColor DarkGray
        }

        # Update last seen lines
        $lastSeenLines = $currentLines

    } catch {
        $errorMsg = "[$timestamp] ERROR: $($_.Exception.Message)"
        Write-Host $errorMsg -ForegroundColor Red
        $errorMsg | Out-File -FilePath $outputFile -Append -Encoding UTF8
    }

    Start-Sleep -Seconds 1
}
