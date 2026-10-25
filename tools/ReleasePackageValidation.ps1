[CmdletBinding()]
param([Parameter(Mandatory)][string]$Runtime, [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$checks = 0
function Assert([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { throw $Message }
}
$Runtime = (Resolve-Path -LiteralPath $Runtime).ProviderPath
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$fixture = (Resolve-Path -LiteralPath $OutputDirectory).ProviderPath
foreach ($file in Get-ChildItem -LiteralPath $Runtime -Filter '*.ps1' -File) {
    $tokens = $null; $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$errors) | Out-Null
    Assert ($errors.Count -eq 0) "Script parse: $($file.Name)"
}
$ini = Get-Content -LiteralPath (Join-Path $Runtime 'PlagueTaleVR.ini') -Raw
Assert ($ini -match '(?m)^DLSS=1\s*$') 'DLSS enabled by default'
Assert ($ini -match '(?m)^DLSSMode=3\s*$') 'Balanced is default'
Assert ($ini -match '(?m)^FirstPerson=0\s*$') 'Third person is default'
Assert ($ini -match '(?m)^HideFirstPersonHead=1\s*$') 'Head hiding retained when first person is selected'
$manifest = Get-Content -LiteralPath (Join-Path $Runtime 'payload.json') -Raw | ConvertFrom-Json
Assert ($manifest.transportABI -eq 9) 'Matching transport ABI'
foreach ($property in $manifest.sha256.PSObject.Properties) {
    Assert ((Get-FileHash -LiteralPath (Join-Path $Runtime $property.Name) -Algorithm SHA256).Hash -eq $property.Value) "Payload checksum: $($property.Name)"
}
$game = Join-Path $fixture 'game'
New-Item -ItemType Directory -Path $game -Force | Out-Null
# Synthetic executable data is never run. ValidateOnly must accept a different
# file fingerprint and must return before game launch or user settings access.
[System.IO.File]::WriteAllBytes((Join-Path $game 'APlagueTaleInnocence_x64.exe'), [byte[]](77,90,1,2,3,4,5,6))
& (Join-Path $Runtime 'LaunchVR.ps1') -GameDirectory $game -ValidateOnly | Out-Null
Assert $true 'Non-reference executable hash is allowed through ValidateOnly'
$scripts = Join-Path $fixture 'scripts'
New-Item -ItemType Directory -Path $scripts -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $Runtime 'StopVR.ps1') -Destination $scripts
$fixturePid = 2134567890
$mapping = [System.IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew("Local\PlagueTaleVR-v9-$fixturePid",448)
$view = $mapping.CreateViewAccessor()
try {
    $view.Write(0,[uint32]0x50545356); $view.Write(4,[uint32]9); $view.Write(8,[uint32]$fixturePid)
    function Get-Process { param($Name,$ErrorAction) [pscustomobject]@{Id=$fixturePid} }
    & (Join-Path $scripts 'StopVR.ps1') | Out-Null
    Assert ($view.ReadUInt32(60) -eq 1) 'Stop helper writes the matching ABI9 stop flag'
} finally { $view.Dispose(); $mapping.Dispose() }
function Get-Process { param($Name,$ErrorAction) @() }
Copy-Item -LiteralPath (Join-Path $Runtime 'RestoreResolution.ps1') -Destination $scripts
$backup = "{Video`nResolution 1920 1080`nPrimaryScaling 0.800000`nSecondaryScaling 0.900000`nGamma 1.000000`n}`n"
$current = "{Video`nResolution 4000 4768`nPrimaryScaling 1.000000`nSecondaryScaling 1.000000`nGamma 1.234567`n}`n"
Set-Content -LiteralPath (Join-Path $scripts 'ENGINESETTINGS.before-vr') -Value $backup
$settings = Join-Path $scripts 'ENGINESETTINGS'
Set-Content -LiteralPath $settings -Value $current
& (Join-Path $scripts 'RestoreResolution.ps1') -SettingsPath $settings | Out-Null
$restored = Get-Content -LiteralPath $settings -Raw
Assert ($restored -match 'Resolution 1920 1080' -and $restored -match 'PrimaryScaling 0.800000' -and $restored -match 'SecondaryScaling 0.900000') 'Saved video dimensions/scales restored'
Assert ($restored -match 'Gamma 1.234567') 'Unrelated settings preserved'
Write-Output "PASS: $checks release package/default/checksum/compatibility/stop/restore checks. No real game process or user settings were modified."
