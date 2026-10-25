[CmdletBinding()]
param([string]$SettingsPath)
$ErrorActionPreference = 'Stop'
if (Get-Process -Name 'APlagueTaleInnocence_x64' -ErrorAction SilentlyContinue) { throw 'Close the game before restoring video settings.' }
$backup = Join-Path $PSScriptRoot 'ENGINESETTINGS.before-vr'
if (-not (Test-Path -LiteralPath $backup)) { throw 'No original settings backup exists in this package folder.' }
if (-not $SettingsPath) { $SettingsPath = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\A Plague Tale Innocence\ENGINESETTINGS' }
$original = [System.IO.File]::ReadAllText($backup)
$current = [System.IO.File]::ReadAllText($settingsPath)
foreach ($key in @('Resolution', 'PrimaryScaling', 'SecondaryScaling')) {
    $pattern = '(?m)^(\s*' + $key + '\s+)([^\r\n]+)'
    $matches = [regex]::Matches($original, $pattern)
    if ($matches.Count -ne 1 -or [regex]::Matches($current, $pattern).Count -ne 1) { throw "Unexpected $key format." }
    $current = [regex]::Replace($current, $pattern, '${1}' + $matches[0].Groups[2].Value)
}
[System.IO.File]::WriteAllText($settingsPath, $current, [System.Text.UTF8Encoding]::new($false))
Write-Output 'Original resolution and scaling restored; other game settings preserved.'
