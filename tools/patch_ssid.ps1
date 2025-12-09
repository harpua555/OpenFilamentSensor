# patch_ssid.ps1

$inFile  = "littlefs.bin"          # input .bin
$outFile = "littlefs_patched.bin"  # output .bin

# New values (can be shorter; will be padded with spaces)
$newSsid     = "mywifi"
$newPasswd   = "mypass"
$newElegooIp = "192.1.2.3"
$newApMode   = "false"  # text as it appears in JSON: true/false/0/1/etc.

# 1:1 bytes <-> chars encoding
$enc       = [System.Text.Encoding]::GetEncoding(28591)
$origBytes = [IO.File]::ReadAllBytes($inFile)
$text      = $enc.GetString($origBytes)

# Regex to find the block
$pattern = '(?s)"ssid"\s*:\s*"(?<ssid>[^"]*)"\s*,\s*' +
           '"passwd"\s*:\s*"(?<passwd>[^"]*)"\s*,\s*' +
           '"elegooip"\s*:\s*"(?<elegooip>[^"]*)"\s*,\s*' +
           '"ap_mode"\s*:\s*(?<ap_mode>[^,}\r\n]+)'

$regex = [regex]$pattern
$m = $regex.Match($text)

if (-not $m.Success) {
    throw "Pattern not found in $inFile"
}

# Capture old values
$oldSsid     = $m.Groups['ssid'].Value
$oldPasswd   = $m.Groups['passwd'].Value
$oldElegooIp = $m.Groups['elegooip'].Value
$oldApMode   = $m.Groups['ap_mode'].Value

Write-Host "=== BEFORE ==="
Write-Host "ssid     : [$oldSsid]"
Write-Host "passwd   : [$oldPasswd]"
Write-Host "elegooip : [$oldElegooIp]"
Write-Host "ap_mode  : [$oldApMode]"
Write-Host ""

# Helper: pad new value to exactly captured length
function Fit-ToLength {
    param(
        [string]$value,
        [int]$length,
        [string]$name
    )
    if ($value.Length -gt $length) {
        throw "New value for '$name' is longer ($($value.Length)) than allowed length $length."
    }
    return $value.PadRight($length, ' ')
}

$chars = $text.ToCharArray()

# Patch each field in-place
$ssidIndex  = $m.Groups['ssid'].Index
$ssidLen    = $m.Groups['ssid'].Length
$newSsidFit = Fit-ToLength $newSsid $ssidLen 'ssid'
$newSsidFit.CopyTo(0, $chars, $ssidIndex, $ssidLen)

$pwIndex  = $m.Groups['passwd'].Index
$pwLen    = $m.Groups['passwd'].Length
$newPwFit = Fit-ToLength $newPasswd $pwLen 'passwd'
$newPwFit.CopyTo(0, $chars, $pwIndex, $pwLen)

$ipIndex  = $m.Groups['elegooip'].Index
$ipLen    = $m.Groups['elegooip'].Length
$newIpFit = Fit-ToLength $newElegooIp $ipLen 'elegooip'
$newIpFit.CopyTo(0, $chars, $ipIndex, $ipLen)

$apIndex  = $m.Groups['ap_mode'].Index
$apLen    = $m.Groups['ap_mode'].Length
$newApFit = Fit-ToLength $newApMode $apLen 'ap_mode'
$newApFit.CopyTo(0, $chars, $apIndex, $apLen)

$newText  = -join $chars
$newBytes = $enc.GetBytes($newText)

# Sanity: length must match
if ($origBytes.Length -ne $newBytes.Length) {
    throw "Byte length changed! orig=$($origBytes.Length) new=$($newBytes.Length)"
}

# Define allowed change ranges (byte indexes == char indexes with this encoding)
$allowedRanges = @(
    @{ Name = 'ssid';     Start = $ssidIndex; End = $ssidIndex + $ssidLen - 1 },
    @{ Name = 'passwd';   Start = $pwIndex;   End = $pwIndex + $pwLen - 1 },
    @{ Name = 'elegooip'; Start = $ipIndex;   End = $ipIndex + $ipLen - 1 },
    @{ Name = 'ap_mode';  Start = $apIndex;   End = $apIndex + $apLen - 1 }
)

function Is-InRange {
    param(
        [int]$index,
        $ranges
    )
    foreach ($r in $ranges) {
        if ($index -ge $r.Start -and $index -le $r.End) {
            return $true
        }
    }
    return $false
}

# Compute SHA256 hashes (full file)
$sha256   = [System.Security.Cryptography.SHA256]::Create()
$origHash = [BitConverter]::ToString($sha256.ComputeHash($origBytes)) -replace '-', ''
$newHash  = [BitConverter]::ToString($sha256.ComputeHash($newBytes))  -replace '-', ''

# Build masked copies by zeroing OUT the edited ranges
$maskedOrig = [byte[]]::new($origBytes.Length)
$maskedNew  = [byte[]]::new($newBytes.Length)
[Array]::Copy($origBytes, $maskedOrig, $origBytes.Length)
[Array]::Copy($newBytes,  $maskedNew,  $newBytes.Length)

foreach ($r in $allowedRanges) {
    for ($i = $r.Start; $i -le $r.End; $i++) {
        $maskedOrig[$i] = 0
        $maskedNew[$i]  = 0
    }
}

$origMaskedHash = [BitConverter]::ToString($sha256.ComputeHash($maskedOrig)) -replace '-', ''
$newMaskedHash  = [BitConverter]::ToString($sha256.ComputeHash($maskedNew))  -replace '-', ''

if ($origMaskedHash -ne $newMaskedHash) {
    throw "Masked SHA mismatch! Something outside the edit ranges changed."
}

# Write patched file only after validation
[IO.File]::WriteAllBytes($outFile, $newBytes)

# Re-extract new values for display (from newText)
$newMatch = $regex.Match($newText)
$newSsidVal     = $newMatch.Groups['ssid'].Value
$newPasswdVal   = $newMatch.Groups['passwd'].Value
$newElegooIpVal = $newMatch.Groups['elegooip'].Value
$newApModeVal   = $newMatch.Groups['ap_mode'].Value

Write-Host "=== AFTER ==="
Write-Host "ssid     : [$newSsidVal]"
Write-Host "passwd   : [$newPasswdVal]"
Write-Host "elegooip : [$newElegooIpVal]"
Write-Host "ap_mode  : [$newApModeVal]"
Write-Host ""

Write-Host "Original SHA256: $origHash"
Write-Host "Patched  SHA256: $newHash"
Write-Host ""
Write-Host "Original MASKED SHA256 (excl. edited ranges): $origMaskedHash"
Write-Host "Patched  MASKED SHA256 (excl. edited ranges): $newMaskedHash"
Write-Host ""
Write-Host "Success: only ssid/passwd/elegooip/ap_mode ranges were modified, masked SHA is identical."
Write-Host "Patched file written to '$outFile'."
