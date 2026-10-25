$ErrorActionPreference = 'Stop'
Set-Content -LiteralPath (Join-Path $PSScriptRoot 'control.txt') -Value '0'
foreach ($game in @(Get-Process -Name 'APlagueTaleInnocence_x64' -ErrorAction SilentlyContinue)) {
    $mapping = $null
    $view = $null
    try {
        $mapping = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting("Local\PlagueTaleVR-v9-$($game.Id)")
        $view = $mapping.CreateViewAccessor(0, 448, [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)
        if ($view.ReadUInt32(0) -ne 0x50545356 -or $view.ReadUInt32(4) -ne 9 -or $view.ReadUInt32(8) -ne $game.Id) {
            throw 'The native VR transport does not match the expected ABI and game process.'
        }
        # Transport::Header v9: swapEyes is at 56; reserved2 stop flag is at 60.
        # This is the same flag used by F10 and the normal companion shutdown.
        $view.Write(60, [uint32]1)
        $view.Flush()
    } catch [System.IO.FileNotFoundException] {
        # A flat game has no native transport to stop.
    } finally {
        if ($view) { $view.Dispose() }
        if ($mapping) { $mapping.Dispose() }
    }
}
Write-Output 'VR stopped. Native camera and effect overrides restore during the next game updates. Exit the game before launching VR again.'
