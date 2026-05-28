param(
    [Parameter(Mandatory=$true)]
    [string]$SourcePath
)

$ErrorActionPreference = "Stop"

if ($env:WLANMONITOR_SKIP_VERSION_PROMPT -eq "1") {
    Write-Host "WLANMONITOR_SKIP_VERSION_PROMPT=1. Skipping version prompt."
    exit 0
}

if (-not (Test-Path -LiteralPath $SourcePath)) {
    Write-Error "Source file not found: $SourcePath"
    exit 1
}

$latin1 = [System.Text.Encoding]::GetEncoding(28591)
$bytes = [System.IO.File]::ReadAllBytes($SourcePath)
$text = $latin1.GetString($bytes)
$pattern = '#define\s+MANUAL_COMPILE_VERSION\s+"([^"]+)"'
$match = [regex]::Match($text, $pattern)
if (-not $match.Success) {
    Write-Error "MANUAL_COMPILE_VERSION was not found in $SourcePath"
    exit 1
}

$currentVersion = $match.Groups[1].Value

Add-Type -AssemblyName Microsoft.VisualBasic
Add-Type -AssemblyName System.Windows.Forms

$newVersion = [Microsoft.VisualBasic.Interaction]::InputBox(
    "Current version: $currentVersion`r`n`r`nEnter the version to build:",
    "WlanMonitorSvc Build Version",
    $currentVersion
)

if ([string]::IsNullOrWhiteSpace($newVersion)) {
    Write-Host "Version prompt cancelled or empty. Keeping current version: $currentVersion"
    exit 0
}

$newVersion = $newVersion.Trim()
if ($newVersion -notmatch '^\d+(\.\d+)*[A-Za-z0-9_-]*$') {
    [System.Windows.Forms.MessageBox]::Show(
        "Invalid version: $newVersion`r`nUse a version like 1.8.25 or 1.8.25a.",
        "Invalid Build Version",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error
    ) | Out-Null
    exit 1
}

if ($newVersion -eq $currentVersion) {
    Write-Host "Version unchanged: $currentVersion"
    exit 0
}

$replacement = "#define MANUAL_COMPILE_VERSION `"$newVersion`""
$updated = [regex]::Replace($text, $pattern, $replacement, 1)
[System.IO.File]::WriteAllBytes($SourcePath, $latin1.GetBytes($updated))

Write-Host "Updated MANUAL_COMPILE_VERSION: $currentVersion -> $newVersion"
