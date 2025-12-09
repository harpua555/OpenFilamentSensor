<# 
    tools-launcher.ps1
    TUI-style menu for running Centauri motion-detector tools.

    Base dir: script location (PSScriptRoot)
#>

# =========================
# 0) CONFIG
# =========================

# Base directory where the tools live (relative to script location)
if ($PSScriptRoot) {
    $BaseDir = $PSScriptRoot
} else {
    # Fallback if run in a weird context: use current location
    $BaseDir = (Get-Location).Path
}

# Scripts (relative to BaseDir)
# --- Build & Flash Tools ---
$BuildScript        = Join-Path $BaseDir "build_and_flash.py"
$BuildLocalScript   = Join-Path $BaseDir "build_local.py"


# --- Development & Analysis Tools ---
$ElegooStatusScript = Join-Path $BaseDir "elegoo_status_cli.py"
$ExtractLogDataScript = Join-Path $BaseDir "extract_log_data.py"

# --- Log Management Tools ---
$CaptureLogsScript  = Join-Path $BaseDir "capture_logs.ps1"
$StreamLogsScript   = Join-Path $BaseDir "stream_logs.ps1"

# --- Test Tools ---
$TestBuildScript    = Join-Path $BaseDir "..\test\build_tests.sh"
$TestBuildScriptWin = Join-Path $BaseDir "..\test\build_tests.bat"

# Python executable (change if you want a specific venv)
$PythonExe = "python"

# Empty presets placeholder (fill with your own later)
$Presets = @(
    # [PSCustomObject]@{
    #     Name        = "My favorite build"
    #     Description = "esp32s3, local, ignore secrets, nofs"
    #     Command     = "build"   # e.g. 'build' | 'capture' | 'stream'
    #     Settings    = @{ Env = "esp32s3"; Local = $true; IgnoreSecrets = $true; BuildMode = "nofs" }
    # }
)

# =========================
# 1) STATE (CURRENT SETTINGS)
# =========================

# --- build_and_flash.py defaults ---
$Global:BuildEnv            = "esp32s3"
$Global:BuildLocal          = $false
$Global:BuildIgnoreSecrets  = $false
# "full" means no --build-mode flag (full build)
$Global:BuildMode           = "full"

# Allowed env options (edit these to match your platformio.ini)
$BuildEnvOptions = @(
    "esp32s3",
    "esp32c3",
    "esp32",
    "seeed_esp32s3",
    "seeed_esp32c3",
    "<enter manually>"
)

# Allowed build-mode options
$BuildModeMap = @{
    "1" = @{ Label = "full (firmware + filesystem)"; Mode = "full"  }
    "2" = @{ Label = "nofs (firmware only)";         Mode = "nofs"  }
    "3" = @{ Label = "nobin (filesystem only)";      Mode = "nobin" }
}

# --- capture_logs.ps1 defaults ---
$Global:CaptureLogsIP = "192.168.0.153"

# --- elegoo_status_cli.py defaults ---
$Global:ElegooIP = "192.168.0.153"
$Global:ElegooTimeout = 5


# --- extract_log_data.py defaults ---
$Global:LogFile = "log.txt"
$Global:OutputFile = "extracted_data.csv"

# =========================
# 2) HELPERS
# =========================

function Wait-ReturnToMenu {
    Read-Host "Press Enter to return to menu"
}

function Test-ScriptPath {
    param(
        [string]$Path
    )
    if (-not (Test-Path $Path)) {
        Write-Warning "Script not found: $Path"
        Wait-ReturnToMenu
        return $false
    }
    return $true
}

# =========================
# 3) BUILD & FLASH SUB-MENU
# =========================

function Show-BuildMenu {
    while ($true) {
        Clear-Host
        Write-Host "==============================="
        Write-Host "  Build & Flash Firmware"
        Write-Host "==============================="
        Write-Host ""
        Write-Host (" Script: {0}" -f $BuildScript)
        Write-Host ""
        Write-Host (" [1] Env              : {0}" -f $Global:BuildEnv)
        Write-Host (" [2] --local          : {0}" -f ($(if ($Global:BuildLocal) {"ON"} else {"OFF"})))
        Write-Host (" [3] --ignore-secrets : {0}" -f ($(if ($Global:BuildIgnoreSecrets) {"ON"} else {"OFF"})))
        Write-Host (" [4] Build mode       : {0}" -f $(switch ($Global:BuildMode) {
            "full"  { "full (firmware + filesystem)" }
            "nofs"  { "nofs (firmware only)" }
            "nobin" { "nobin (filesystem only)" }
            default { $Global:BuildMode }
        }))
        Write-Host ""
        Write-Host " [R] Run with above settings"
        Write-Host " [B] Back to main menu"
        Write-Host ""

        $choice = Read-Host "Select an option"

        switch -Regex ($choice.Trim()) {
            "^[Bb]$" { return }

            "^[Rr]$" {
                Invoke-BuildCommand
            }

            "^1$" {
                Change-BuildEnv
            }

            "^2$" {
                $Global:BuildLocal = -not $Global:BuildLocal
            }

            "^3$" {
                $Global:BuildIgnoreSecrets = -not $Global:BuildIgnoreSecrets
            }

            "^4$" {
                Change-BuildMode
            }

            default {
                Write-Host "Invalid option." -ForegroundColor Red
                Start-Sleep -Seconds 1.2
            }
        }
    }
}

function Change-BuildEnv {
    while ($true) {
        Clear-Host
        Write-Host "Select PlatformIO env:"
        Write-Host ""
        for ($i = 0; $i -lt $BuildEnvOptions.Count; $i++) {
            $idx = $i + 1
            Write-Host (" [{0}] {1}" -f $idx, $BuildEnvOptions[$i])
        }
        Write-Host " [B] Back (no change)"
        Write-Host ""
        Write-Host ("Current env: {0}" -f $Global:BuildEnv)
        $choice = Read-Host "Choose env"

        if ($choice -match "^[Bb]$") { return }

        if ($choice -match "^\d+$") {
            $idx = [int]$choice - 1
            if ($idx -ge 0 -and $idx -lt $BuildEnvOptions.Count) {
                $selected = $BuildEnvOptions[$idx]
                if ($selected -eq "<enter manually>") {
                    $manual = Read-Host "Enter env name manually"
                    if ($manual.Trim()) {
                        $Global:BuildEnv = $manual.Trim()
                    }
                } else {
                    $Global:BuildEnv = $selected
                }
                return
            }
        }

        Write-Host "Invalid option." -ForegroundColor Red
        Start-Sleep -Seconds 1.2
    }
}

function Change-BuildMode {
    while ($true) {
        Clear-Host
        Write-Host "Select build mode:"
        Write-Host ""

        $BuildModeMap.GetEnumerator() | Sort-Object Key | ForEach-Object {
            Write-Host (" [{0}] {1}" -f $_.Key, $_.Value.Label)
        }

        Write-Host " [B] Back (no change)"
        Write-Host ""
        Write-Host ("Current mode: {0}" -f $(switch ($Global:BuildMode) {
            "full"  { "full (firmware + filesystem)" }
            "nofs"  { "nofs (firmware only)" }
            "nobin" { "nobin (filesystem only)" }
            default { $Global:BuildMode }
        }))

        $choice = Read-Host "Choose mode"

        if ($choice -match "^[Bb]$") { return }

        if ($BuildModeMap.ContainsKey($choice)) {
            $Global:BuildMode = $BuildModeMap[$choice].Mode
            return
        }

        Write-Host "Invalid option." -ForegroundColor Red
        Start-Sleep -Seconds 1.2
    }
}

function Invoke-BuildCommand {
    if (-not (Test-ScriptPath -Path $BuildScript)) { return }

    # Build up argument list
    $args = @()

    if ($Global:BuildEnv -and $Global:BuildEnv.Trim()) {
        $args += "--env"
        $args += $Global:BuildEnv.Trim()
    }

    if ($Global:BuildLocal) {
        $args += "--local"
    }

    if ($Global:BuildIgnoreSecrets) {
        $args += "--ignore-secrets"
    }

    if ($Global:BuildMode -and $Global:BuildMode -ne "full") {
        $args += "--build-mode"
        $args += $Global:BuildMode
    }

    Clear-Host
    Write-Host "Running build_and_flash.py with:" -ForegroundColor Cyan
    Write-Host "  Env              : $Global:BuildEnv"
    Write-Host "  --local          : $Global:BuildLocal"
    Write-Host "  --ignore-secrets : $Global:BuildIgnoreSecrets"
    Write-Host "  Build mode       : $Global:BuildMode"
    Write-Host ""
    Write-Host ("Command: {0} {1} {2}" -f $PythonExe, $BuildScript, ($args -join " "))
    Write-Host "===================================================="
    Write-Host ""

    & $PythonExe $BuildScript @args

    Write-Host ""
    Wait-ReturnToMenu
}

function Invoke-BuildLocalCommand {
    if (-not (Test-ScriptPath -Path $BuildLocalScript)) { return }

    Clear-Host
    Write-Host "Running build_local.py..." -ForegroundColor Cyan
    Write-Host ""
    Write-Host ("Command: {0} {1}" -f $PythonExe, $BuildLocalScript)
    Write-Host "===================================================="
    Write-Host ""

    & $PythonExe $BuildLocalScript

    Write-Host ""
    Wait-ReturnToMenu
}

# =========================
# 4) CAPTURE LOGS SUB-MENU
# =========================

function Show-CaptureMenu {
    while ($true) {
        Clear-Host
        Write-Host "==============================="
        Write-Host "  Capture Logs"
        Write-Host "==============================="
        Write-Host ""
        Write-Host (" Script : {0}" -f $CaptureLogsScript)
        Write-Host (" IP     : {0}" -f $Global:CaptureLogsIP)
        Write-Host ""
        Write-Host " [1] Change IP"
        Write-Host " [R] Run capture_logs.ps1"
        Write-Host " [B] Back to main menu"
        Write-Host ""

        $choice = Read-Host "Select an option"

        switch -Regex ($choice.Trim()) {
            "^[Bb]$" { return }

            "^[Rr]$" {
                Invoke-CaptureCommand
            }

            "^1$" {
                $ip = Read-Host "Enter IP (blank to keep current)"
                if ($ip.Trim()) {
                    $Global:CaptureLogsIP = $ip.Trim()
                }
            }

            default {
                Write-Host "Invalid option." -ForegroundColor Red
                Start-Sleep -Seconds 1.2
            }
        }
    }
}

function Invoke-CaptureCommand {
    if (-not (Test-ScriptPath -Path $CaptureLogsScript)) { return }

    Clear-Host
    Write-Host "Running capture_logs.ps1 with:" -ForegroundColor Cyan
    Write-Host "  IP: $Global:CaptureLogsIP"
    Write-Host ""
    Write-Host ("Command: powershell.exe -File {0} -IP {1}" -f $CaptureLogsScript, $Global:CaptureLogsIP)
    Write-Host "===================================================="
    Write-Host ""

    & powershell.exe -NoLogo -ExecutionPolicy Bypass -File $CaptureLogsScript -IP $Global:CaptureLogsIP

    Write-Host ""
    Wait-ReturnToMenu
}

# =========================
# 5) STREAM LOGS SUB-MENU
# =========================

function Show-StreamMenu {
    while ($true) {
        Clear-Host
        Write-Host "==============================="
        Write-Host "  Stream Logs"
        Write-Host "==============================="
        Write-Host ""
        Write-Host (" Script : {0}" -f $StreamLogsScript)
        Write-Host ""
        Write-Host " [R] Run stream_logs.ps1"
        Write-Host " [B] Back to main menu"
        Write-Host ""

        $choice = Read-Host "Select an option"

        switch -Regex ($choice.Trim()) {
            "^[Bb]$" { return }

            "^[Rr]$" {
                Invoke-StreamCommand
            }

            default {
                Write-Host "Invalid option." -ForegroundColor Red
                Start-Sleep -Seconds 1.2
            }
        }
    }
}

function Invoke-StreamCommand {
    if (-not (Test-ScriptPath -Path $StreamLogsScript)) { return }

    Clear-Host
    Write-Host "Running stream_logs.ps1..." -ForegroundColor Cyan
    Write-Host ""
    Write-Host ("Command: powershell.exe -File {0}" -f $StreamLogsScript)
    Write-Host "===================================================="
    Write-Host ""

    & powershell.exe -NoLogo -ExecutionPolicy Bypass -File $StreamLogsScript

    Write-Host ""
    Wait-ReturnToMenu
}

# =========================
# 6) ENVIRONMENT & SETUP SUB-MENU
# =========================


# =========================
# 7) DEVELOPMENT TOOLS SUB-MENU
# =========================

function Show-DevelopmentMenu {
    while ($true) {
        Clear-Host
        Write-Host "==============================="
        Write-Host "  Development & Analysis Tools"
        Write-Host "==============================="
        Write-Host ""
        Write-Host " [1] Elegoo Status CLI (elegoo_status_cli.py)"
        Write-Host " [2] Extract Log Data (extract_log_data.py)"
        Write-Host " [B] Back to main menu"
        Write-Host ""

        $choice = Read-Host "Select an option"

        switch -Regex ($choice.Trim()) {
            "^[Bb]$" { return }

            "^1$" { Show-ElegooStatusMenu }

            "^2$" { Show-ExtractLogMenu }

            default {
                Write-Host "Invalid option." -ForegroundColor Red
                Start-Sleep -Seconds 1.2
            }
        }
    }
}


function Show-ElegooStatusMenu {
    while ($true) {
        Clear-Host
        Write-Host "==============================="
        Write-Host "  Elegoo Status CLI"
        Write-Host "==============================="
        Write-Host ""
        Write-Host (" [1] IP Address    : {0}" -f $Global:ElegooIP)
        Write-Host (" [2] Timeout (s)   : {0}" -f $Global:ElegooTimeout)
        Write-Host ""
        Write-Host " [R] Run with above settings"
        Write-Host " [B] Back to development menu"
        Write-Host ""

        $choice = Read-Host "Select an option"

        switch -Regex ($choice.Trim()) {
            "^[Bb]$" { return }

            "^[Rr]$" {
                Invoke-ElegooStatusCommand
            }

            "^1$" {
                $ip = Read-Host "Enter IP address (blank to keep current)"
                if ($ip.Trim()) {
                    $Global:ElegooIP = $ip.Trim()
                }
            }

            "^2$" {
                $timeout = Read-Host "Enter timeout in seconds (blank to keep current)"
                if ($timeout.Trim() -and $timeout.Trim() -match '^\d+$') {
                    $Global:ElegooTimeout = [int]$timeout.Trim()
                }
            }

            default {
                Write-Host "Invalid option." -ForegroundColor Red
                Start-Sleep -Seconds 1.2
            }
        }
    }
}

function Invoke-ElegooStatusCommand {
    if (-not (Test-ScriptPath -Path $ElegooStatusScript)) { return }

    $args = @($Global:ElegooIP)
    $args += "--timeout"
    $args += $Global:ElegooTimeout.ToString()

    Clear-Host
    Write-Host "Running elegoo_status_cli.py with:" -ForegroundColor Cyan
    Write-Host "  IP Address : $Global:ElegooIP"
    Write-Host "  Timeout (s): $Global:ElegooTimeout"
    Write-Host ""
    Write-Host ("Command: {0} {1} {2}" -f $PythonExe, $ElegooStatusScript, ($args -join " "))
    Write-Host "===================================================="
    Write-Host ""

    & $PythonExe $ElegooStatusScript @args

    Write-Host ""
    Wait-ReturnToMenu
}

function Show-ExtractLogMenu {
    while ($true) {
        Clear-Host
        Write-Host "==============================="
        Write-Host "  Extract Log Data"
        Write-Host "==============================="
        Write-Host ""
        Write-Host (" [1] Log File     : {0}" -f $Global:LogFile)
        Write-Host (" [2] Output File  : {0}" -f $Global:OutputFile)
        Write-Host ""
        Write-Host " [R] Run with above settings"
        Write-Host " [B] Back to development menu"
        Write-Host ""

        $choice = Read-Host "Select an option"

        switch -Regex ($choice.Trim()) {
            "^[Bb]$" { return }

            "^[Rr]$" {
                Invoke-ExtractLogCommand
            }

            "^1$" {
                $file = Read-Host "Enter log file path (blank to keep current)"
                if ($file.Trim()) {
                    $Global:LogFile = $file.Trim()
                }
            }

            "^2$" {
                $file = Read-Host "Enter output file path (blank to keep current)"
                if ($file.Trim()) {
                    $Global:OutputFile = $file.Trim()
                }
            }

            default {
                Write-Host "Invalid option." -ForegroundColor Red
                Start-Sleep -Seconds 1.2
            }
        }
    }
}

function Invoke-ExtractLogCommand {
    if (-not (Test-ScriptPath -Path $ExtractLogDataScript)) { return }

    $args = @($Global:LogFile)
    $args += "--output"
    $args += $Global:OutputFile

    Clear-Host
    Write-Host "Running extract_log_data.py with:" -ForegroundColor Cyan
    Write-Host "  Log File    : $Global:LogFile"
    Write-Host "  Output File : $Global:OutputFile"
    Write-Host ""
    Write-Host ("Command: {0} {1} {2}" -f $PythonExe, $ExtractLogDataScript, ($args -join " "))
    Write-Host "===================================================="
    Write-Host ""

    & $PythonExe $ExtractLogDataScript @args

    Write-Host ""
    Wait-ReturnToMenu
}


# =========================
# 8) TESTING SUB-MENU
# =========================

function Show-TestingMenu {
    while ($true) {
        Clear-Host
        Write-Host "==============================="
        Write-Host "  Testing Tools"
        Write-Host "==============================="
        Write-Host ""
        Write-Host " [1] Run Build Tests (Linux/WSL)"
        Write-Host " [2] Run Build Tests (Windows)"
        Write-Host " [B] Back to main menu"
        Write-Host ""

        $choice = Read-Host "Select an option"

        switch -Regex ($choice.Trim()) {
            "^[Bb]$" { return }

            "^1$" {
                Invoke-TestBuildCommand
            }

            "^2$" {
                Invoke-TestBuildCommandWin
            }

            default {
                Write-Host "Invalid option." -ForegroundColor Red
                Start-Sleep -Seconds 1.2
            }
        }
    }
}

function Invoke-TestBuildCommand {
    if (-not (Test-ScriptPath -Path $TestBuildScript)) { return }

    Clear-Host
    Write-Host "Running build tests (Linux/WSL)..." -ForegroundColor Cyan
    Write-Host ""
    Write-Host ("Command: bash {0}" -f $TestBuildScript)
    Write-Host "===================================================="
    Write-Host ""

    & bash $TestBuildScript

    Write-Host ""
    Wait-ReturnToMenu
}

function Invoke-TestBuildCommandWin {
    if (-not (Test-ScriptPath -Path $TestBuildScriptWin)) { return }

    Clear-Host
    Write-Host "Running build tests (Windows)..." -ForegroundColor Cyan
    Write-Host ""
    Write-Host ("Command: {0}" -f $TestBuildScriptWin)
    Write-Host "===================================================="
    Write-Host ""

    & cmd /c $TestBuildScriptWin

    Write-Host ""
    Wait-ReturnToMenu
}

# =========================
# 9) MAIN MENU
# =========================

function Show-MainMenu {
    while ($true) {
        Clear-Host
        Write-Host "======================================="
        Write-Host "  Centauri Motion Detector Tools Menu"
        Write-Host "======================================="
        Write-Host ""
        Write-Host (" Base dir: {0}" -f $BaseDir)
        Write-Host ""
        Write-Host " Build & Flash Tools:"
        Write-Host " [1] Build & Flash Firmware (build_and_flash.py)"
        Write-Host " [2] Local Build (build_local.py)"
        Write-Host ""
        Write-Host " Development & Analysis:"
        Write-Host " [3] Development & Analysis Tools"
        Write-Host ""
        Write-Host " Log Management:"
        Write-Host " [4] Capture Logs (capture_logs.ps1)"
        Write-Host " [5] Stream Logs (stream_logs.ps1)"
        Write-Host ""
        Write-Host " Testing:"
        Write-Host " [6] Testing Tools"
        Write-Host ""
        Write-Host " [Q] Quit"
        Write-Host ""

        $choice = Read-Host "Select an option"

        switch -Regex ($choice.Trim()) {
            "^[Qq]$" { return }

            "^1$" { Show-BuildMenu }

            "^2$" { Invoke-BuildLocalCommand }

            "^3$" { Show-DevelopmentMenu }

            "^4$" { Show-CaptureMenu }

            "^5$" { Show-StreamMenu }

            "^6$" { Show-TestingMenu }

            default {
                Write-Host "Invalid option." -ForegroundColor Red
                Start-Sleep -Seconds 1.2
            }
        }
    }
}

# =========================
# 10) ENTRY POINT
# =========================

Show-MainMenu
