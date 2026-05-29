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
$channelPattern = '#define\s+MANUAL_BUILD_CHANNEL\s+"([^"]+)"'
$channelMatch = [regex]::Match($text, $channelPattern)
if (-not $channelMatch.Success) {
    Write-Error "MANUAL_BUILD_CHANNEL was not found in $SourcePath"
    exit 1
}
$currentChannel = $channelMatch.Groups[1].Value

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$form = New-Object System.Windows.Forms.Form
$form.Text = "WlanMonitorSvc Build Settings"
$form.StartPosition = "CenterScreen"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false
$form.MinimizeBox = $false
$form.ClientSize = New-Object System.Drawing.Size(380, 185)
$form.TopMost = $true

$versionLabel = New-Object System.Windows.Forms.Label
$versionLabel.Text = "Build version:"
$versionLabel.Location = New-Object System.Drawing.Point(16, 18)
$versionLabel.Size = New-Object System.Drawing.Size(120, 22)
$form.Controls.Add($versionLabel)

$versionBox = New-Object System.Windows.Forms.TextBox
$versionBox.Text = $currentVersion
$versionBox.Location = New-Object System.Drawing.Point(140, 16)
$versionBox.Size = New-Object System.Drawing.Size(220, 24)
$form.Controls.Add($versionBox)

$channelLabel = New-Object System.Windows.Forms.Label
$channelLabel.Text = "Build channel:"
$channelLabel.Location = New-Object System.Drawing.Point(16, 58)
$channelLabel.Size = New-Object System.Drawing.Size(120, 22)
$form.Controls.Add($channelLabel)

$channelBox = New-Object System.Windows.Forms.ComboBox
$channelBox.DropDownStyle = [System.Windows.Forms.ComboBoxStyle]::DropDownList
[void]$channelBox.Items.Add("stable")
[void]$channelBox.Items.Add("test")
$channelBox.SelectedItem = $(if ($currentChannel -eq "test") { "test" } else { "stable" })
$channelBox.Location = New-Object System.Drawing.Point(140, 56)
$channelBox.Size = New-Object System.Drawing.Size(220, 24)
$form.Controls.Add($channelBox)

$hintLabel = New-Object System.Windows.Forms.Label
$hintLabel.Text = "Cancel or close this window to cancel the build."
$hintLabel.Location = New-Object System.Drawing.Point(16, 95)
$hintLabel.Size = New-Object System.Drawing.Size(340, 22)
$hintLabel.ForeColor = [System.Drawing.Color]::DimGray
$form.Controls.Add($hintLabel)

$okButton = New-Object System.Windows.Forms.Button
$okButton.Text = "Build"
$okButton.Location = New-Object System.Drawing.Point(190, 135)
$okButton.Size = New-Object System.Drawing.Size(80, 30)
$okButton.DialogResult = [System.Windows.Forms.DialogResult]::OK
$form.AcceptButton = $okButton
$form.Controls.Add($okButton)

$cancelButton = New-Object System.Windows.Forms.Button
$cancelButton.Text = "Cancel"
$cancelButton.Location = New-Object System.Drawing.Point(280, 135)
$cancelButton.Size = New-Object System.Drawing.Size(80, 30)
$cancelButton.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
$form.CancelButton = $cancelButton
$form.Controls.Add($cancelButton)

$dialogResult = $form.ShowDialog()

if ($dialogResult -ne [System.Windows.Forms.DialogResult]::OK) {
    Write-Host "Build settings prompt cancelled. Aborting build."
    exit 1
}

$newVersion = $versionBox.Text

if ([string]::IsNullOrWhiteSpace($newVersion)) {
    [System.Windows.Forms.MessageBox]::Show(
        "Version cannot be empty.",
        "Invalid Build Version",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error
    ) | Out-Null
    exit 1
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
}

$newChannel = [string]$channelBox.SelectedItem
if ($newChannel -notin @("stable", "test")) {
    Write-Error "Invalid build channel: $newChannel"
    exit 1
}

$replacement = "#define MANUAL_COMPILE_VERSION `"$newVersion`""
$updated = [regex]::Replace($text, $pattern, $replacement, 1)
$channelReplacement = "#define MANUAL_BUILD_CHANNEL `"$newChannel`""
$updated = [regex]::Replace($updated, $channelPattern, $channelReplacement, 1)
[System.IO.File]::WriteAllBytes($SourcePath, $latin1.GetBytes($updated))

Write-Host "Updated MANUAL_COMPILE_VERSION: $currentVersion -> $newVersion"
Write-Host "Updated MANUAL_BUILD_CHANNEL: $currentChannel -> $newChannel"
