[CmdletBinding()]
param(
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build-release\bin'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist\release'),
    [string]$Version = '0.1.0'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).ProviderPath
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Expected a semantic release version.' }
$name = "PlagueTaleInnocenceVR-$Version"
$target = Join-Path $OutputDirectory $name
if ((Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath "$target.zip")) { throw "Output already exists: $target" }
New-Item -ItemType Directory -Path $target -Force | Out-Null
$hashes = [ordered]@{}
foreach ($binary in @('PlagueTaleVR.dll','PlagueVRHost.exe','PlagueProbeLoader.exe')) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory $binary) -Destination $target
    $hashes[$binary] = (Get-FileHash -LiteralPath (Join-Path $target $binary) -Algorithm SHA256).Hash
}
$sdk = Join-Path $root 'vendor\dlss\bin\nvngx_dlss.dll'
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.json') -Raw | ConvertFrom-Json
$expectedSdk = ($lock.dlss.files | Where-Object path -eq 'bin/nvngx_dlss.dll').sha256
if ((Get-FileHash -LiteralPath $sdk -Algorithm SHA256).Hash -ne $expectedSdk) { throw 'NVIDIA runtime checksum mismatch.' }
Copy-Item -LiteralPath $sdk -Destination $target
$hashes['nvngx_dlss.dll'] = $expectedSdk
foreach ($file in @('LaunchVR.ps1','LaunchVR.cmd','StopVR.ps1','StopVR.cmd','RestoreResolution.ps1','RestoreResolution.cmd','PlagueTaleVR.ini')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $target
}
foreach ($file in @('README.md','CONTROLS.md','LICENSE','THIRD_PARTY.md')) { Copy-Item -LiteralPath (Join-Path $root $file) -Destination $target }
Copy-Item -LiteralPath (Join-Path $root 'tools\SHADER-HUNTER.md') -Destination $target
$licenses = Join-Path $target 'licenses'
New-Item -ItemType Directory -Path $licenses | Out-Null
foreach ($entry in @(
    @('vendor\minhook\LICENSE.txt','MinHook-LICENSE.txt'),
    @('vendor\stereo-bridge\LICENSE','NativeBridge-LICENSE.txt'),
    @('vendor\OpenXR-LICENSE','OpenXR-LICENSE.txt'),
    @('vendor\dxbc-hash\LICENSE.TXT','DXBC-HASH-LICENSE.txt'),
    @('vendor\dlss\LICENSE.txt','NVIDIA-RTX-SDK-LICENSE.txt')
)) { Copy-Item -LiteralPath (Join-Path $root $entry[0]) -Destination (Join-Path $licenses $entry[1]) }
$jsonLicense = Join-Path $root 'vendor\openxr-sdk-source\src\external\jsoncpp\LICENSE'
Copy-Item -LiteralPath $jsonLicense -Destination (Join-Path $licenses 'JsonCpp-LICENSE.txt')
$revision = $null
if (Test-Path -LiteralPath (Join-Path $root '.git')) {
    $gitRoot = $root.Replace('\','/')
    $revision = (& git -c "safe.directory=$gitRoot" -C $root rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -ne 0) { $revision = $null }
}
[ordered]@{version=$Version; transportABI=9; sourceCommit=$revision; sha256=$hashes} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $target 'payload.json') -Encoding UTF8
Compress-Archive -LiteralPath $target -DestinationPath "$target.zip"
$checksum = (Get-FileHash -LiteralPath "$target.zip" -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$target.zip.sha256" -Value "$checksum  $name.zip" -Encoding ASCII
Write-Output "Runtime: $target"
Write-Output "Archive: $target.zip"
Write-Output "SHA256: $checksum"
