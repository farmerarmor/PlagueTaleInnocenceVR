[CmdletBinding()]
param(
    [string]$GameDirectory,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
$modDirectory = $PSScriptRoot
$gameName = 'APlagueTaleInnocence_x64.exe'
$expectedGame = 'BC9D87B88A9780EFB5E2AE48F444C1147295B59B775DE96FE13DF94E7A5967B9'
$dllName = 'PlagueTaleVR.dll'
$cachePath = Join-Path $modDirectory 'game-path.txt'

if (-not $GameDirectory -and (Test-Path -LiteralPath $cachePath)) {
    $GameDirectory = (Get-Content -LiteralPath $cachePath -Raw).Trim()
}
if (-not $GameDirectory) {
    $ancestor = [System.IO.DirectoryInfo]::new($modDirectory)
    while ($ancestor) {
        foreach ($candidate in @($ancestor.FullName, (Join-Path $ancestor.FullName 'A Plague Tale Innocence'))) {
            if (Test-Path -LiteralPath (Join-Path $candidate $gameName) -PathType Leaf) {
                $GameDirectory = $candidate
                break
            }
        }
        if ($GameDirectory) { break }
        $ancestor = $ancestor.Parent
    }
}
if (-not $GameDirectory) { $GameDirectory = Read-Host 'Path to the A Plague Tale Innocence game folder' }
$GameDirectory = (Resolve-Path -LiteralPath $GameDirectory.Trim('"')).ProviderPath
$gameExe = Join-Path $GameDirectory $gameName
if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) { throw "Game executable not found in $GameDirectory" }
# Fingerprints identify the tested reference only; other builds may attempt startup.
$actualGame = (Get-FileHash -LiteralPath $gameExe -Algorithm SHA256).Hash
if ($actualGame -ne $expectedGame) {
    Write-Output 'Different game build detected; attempting native layout checks.'
}
$manifest = Get-Content -LiteralPath (Join-Path $modDirectory 'payload.json') -Raw | ConvertFrom-Json
foreach ($name in @($dllName, 'PlagueVRHost.exe', 'PlagueProbeLoader.exe')) {
    $expected = $manifest.sha256.$name
    if (-not $expected -or (Get-FileHash -LiteralPath (Join-Path $modDirectory $name) -Algorithm SHA256).Hash -ne $expected) {
        throw "The packaged $name does not match its checksum. Extract the complete package again."
    }
}
if ($ValidateOnly) {
    Write-Output 'PASS: game found and matching DLL, host, loader, and DLSS payloads. No executable-version whitelist is applied.'
    return
}
if (Get-Process -Name 'APlagueTaleInnocence_x64' -ErrorAction SilentlyContinue) {
    throw 'Exit the running game before using this launcher. A fresh process is required.'
}
if (Get-Process -Name 'PlagueVRHost' -ErrorAction SilentlyContinue) {
    throw 'An existing Plague VR host is still running. Exit its game and wait for it to close first.'
}
# Query before starting any game/VR process, then pass the exact runtime size
# to the game-owned resize path and select 100% game render scaling.
$displayText = (& (Join-Path $modDirectory 'PlagueVRHost.exe') --display-query | Out-String)
if ($LASTEXITCODE -ne 0 -or $displayText -notmatch 'OpenXR eye=(\d+)x(\d+) refresh=([0-9.]+) Hz') {
    throw 'Could not query the active headset. Connect PC VR and check the OpenXR runtime, then retry.'
}
$eyeWidth = [int]$Matches[1]
$eyeHeight = [int]$Matches[2]
if ($eyeWidth -lt 1 -or $eyeWidth -gt 16384 -or $eyeHeight -lt 1 -or $eyeHeight -gt 8192) {
    throw 'The headset requested dimensions outside the supported texture bounds.'
}
$settingsPath = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games\A Plague Tale Innocence\ENGINESETTINGS'
if (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf)) { throw 'Run the original game once to create ENGINESETTINGS, then exit and retry.' }
$settingsText = [System.IO.File]::ReadAllText($settingsPath)
$videoPattern = '(?ms)^\{Video\r?\n.*?^\}'
if ([regex]::Matches($settingsText, $videoPattern).Count -ne 1) { throw 'Unexpected game video settings format.' }
$settingsBackup = Join-Path $modDirectory 'ENGINESETTINGS.before-vr'
if (-not (Test-Path -LiteralPath $settingsBackup)) { Copy-Item -LiteralPath $settingsPath -Destination $settingsBackup }
$video = [regex]::Match($settingsText, $videoPattern).Value
$newVideo = $video
foreach ($entry in @(
    @('Resolution', "$eyeWidth $eyeHeight"),
    @('PrimaryScaling', '1.000000'),
    @('SecondaryScaling', '1.000000')
)) {
    $settingPattern = '(?m)^(\s*' + $entry[0] + '\s+)[^\r\n]+'
    if ([regex]::Matches($newVideo, $settingPattern).Count -ne 1) { throw "Unexpected $($entry[0]) setting format." }
    $newVideo = [regex]::Replace($newVideo, $settingPattern, '${1}' + $entry[1])
}
$settingsText = $settingsText.Replace($video, $newVideo)
[System.IO.File]::WriteAllText($settingsPath, $settingsText, [System.Text.UTF8Encoding]::new($false))
Set-Content -LiteralPath (Join-Path $modDirectory 'headset-resolution.txt') -Value "$eyeWidth $eyeHeight"
Write-Output "Headset resolution: $eyeWidth x $eyeHeight per eye, 100% game render scaling."
Set-Content -LiteralPath $cachePath -Value $GameDirectory
Set-Content -LiteralPath (Join-Path $modDirectory 'control.txt') -Value '0'
foreach ($setting in @('effects-control.txt', 'occlusion-control.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $modDirectory $setting))) {
        Set-Content -LiteralPath (Join-Path $modDirectory $setting) -Value '1'
    }
}
$probeLog = Join-Path $modDirectory 'PlagueStereoProbe.log'
Set-Content -LiteralPath $probeLog -Value ''
foreach ($flag in @('capture.flag', 'host-capture.flag')) {
    Remove-Item -LiteralPath (Join-Path $modDirectory $flag) -ErrorAction SilentlyContinue
}
$game = Start-Process -FilePath $gameExe -WorkingDirectory $GameDirectory -PassThru
try {
    Start-Sleep -Milliseconds 1300
    if ($game.HasExited) { throw 'The game exited during startup.' }
    & (Join-Path $modDirectory 'PlagueProbeLoader.exe') $game.Id (Join-Path $modDirectory $dllName)
    if ($LASTEXITCODE -ne 0) { throw 'The native stereo DLL could not be loaded.' }
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $initialized = $false
    do {
        $status = Get-Content -LiteralPath $probeLog -Raw
        if ($status -match 'Incompatible process image|signature mismatch|hook installation failed') {
            throw 'The stereo DLL rejected the executable or a required hook. See PlagueStereoProbe.log.'
        }
        if ($status -match 'Native layout checks passed; observe mode') { $initialized = $true; break }
        if ($game.HasExited) { throw 'The game exited before stereo initialization.' }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $initialized) { throw 'Stereo initialization timed out. See PlagueStereoProbe.log.' }
    Set-Content -LiteralPath (Join-Path $modDirectory 'control.txt') -Value '3'
    Start-Sleep -Milliseconds 700
    $companion = Start-Process -FilePath (Join-Path $modDirectory 'PlagueVRHost.exe') -ArgumentList $game.Id -WorkingDirectory $modDirectory -WindowStyle Hidden -RedirectStandardOutput (Join-Path $modDirectory 'host-stdout.log') -RedirectStandardError (Join-Path $modDirectory 'host-stderr.log') -PassThru
    Start-Sleep -Milliseconds 1500
    if ($companion.HasExited) { throw "The OpenXR host exited with code $($companion.ExitCode). See host-stdout.log and PlagueTaleVR-OpenXR.log." }
    Write-Output "VR launched. Game PID $($game.Id), host PID $($companion.Id). The companion waits for the first full-sized native pair. Load gameplay, then press F9 to recenter."
} catch {
    Set-Content -LiteralPath (Join-Path $modDirectory 'control.txt') -Value '0'
    throw
}
