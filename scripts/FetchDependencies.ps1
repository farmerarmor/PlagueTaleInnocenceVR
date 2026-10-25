[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).ProviderPath
$lock = Get-Content -LiteralPath (Join-Path $root 'dependencies.json') -Raw | ConvertFrom-Json
$cache = Join-Path $root 'vendor\.downloads'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
function Get-VerifiedFile([string]$Url, [string]$Path, [string]$Hash) {
    if ((Test-Path -LiteralPath $Path) -and (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Hash) { return }
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    $temporary = "$Path.download"
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $temporary
    if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $Hash) { throw "Dependency checksum mismatch: $Path" }
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}
# SDK components are fetched from NVIDIA rather than copied into this source
# repository. Their separate license accompanies both development and runtime.
foreach ($file in $lock.dlss.files) {
    Get-VerifiedFile $file.url (Join-Path $root "vendor\dlss\$($file.path)") $file.sha256
}
$archive = Join-Path $cache 'openxr-sdk.zip'
Get-VerifiedFile $lock.openxr.url $archive $lock.openxr.sha256
$sdk = [System.IO.Path]::GetFullPath((Join-Path $root 'vendor\openxr-sdk-source'))
$marker = Join-Path $sdk '.dependency-commit'
if (-not (Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw).Trim() -ne $lock.openxr.commit) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    New-Item -ItemType Directory -Path $sdk -Force | Out-Null
    $zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
    try {
        foreach ($entry in $zip.Entries) {
            if (-not $entry.Name) { continue }
            if (-not $entry.FullName.StartsWith($lock.openxr.archivePrefix, [System.StringComparison]::Ordinal)) { throw 'Unexpected dependency archive root.' }
            $relative = $entry.FullName.Substring($lock.openxr.archivePrefix.Length)
            $target = [System.IO.Path]::GetFullPath((Join-Path $sdk $relative))
            if (-not $target.StartsWith($sdk + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe dependency archive entry.' }
            New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    } finally { $zip.Dispose() }
    Set-Content -LiteralPath $marker -Value $lock.openxr.commit
}
Write-Output 'Verified pinned OpenXR and NVIDIA DLSS build dependencies.'
