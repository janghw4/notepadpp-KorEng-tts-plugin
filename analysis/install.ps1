[CmdletBinding()]
param([string]$NotepadDirectory = (Join-Path $env:ProgramFiles 'Notepad++'))
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $projectRoot 'dist\SelectionTTS\SelectionTTS.dll'
$notepadExe = Join-Path $NotepadDirectory 'notepad++.exe'
if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Plugin DLL not found: $source" }
if (!(Test-Path -LiteralPath $notepadExe -PathType Leaf)) { throw "Notepad++ not found: $notepadExe" }
function Get-PeMachine([string]$Path) {
    $file = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($file)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw 'Invalid executable.' }
        $file.Position = 0x3C
        $offset = $reader.ReadInt32()
        $file.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw 'Invalid PE signature.' }
        return $reader.ReadUInt16()
    } finally { $reader.Dispose(); $file.Dispose() }
}
if ((Get-PeMachine $notepadExe) -ne 0x8664 -or (Get-PeMachine $source) -ne 0x8664) {
    throw 'This package requires x64 Notepad++. ARM64 and 32-bit editions are not supported by this binary.'
}
$destinationDirectory = Join-Path $NotepadDirectory 'plugins\SelectionTTS'
$destination = Join-Path $destinationDirectory 'SelectionTTS.dll'
$expectedHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$replaceableHashes = @('7917050F0FF6A6420C87EA51837BCA3790E9E88CC6B723F02004B3D95CB721C3', '6E762A2794AC8BA9121A1D6212F9C6826342919DA6AD79A69B7BA59453EDDD49', '25628E543456FABA9BD7261CFE308B348B26C0E69B70D3B06A4CE5485B20100C')
if (Test-Path -LiteralPath $destination) {
    $installedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    if ($installedHash -eq $expectedHash) {
        Write-Output "The same plugin is already installed: $destination"
    } else {
        if ($installedHash -notin $replaceableHashes) { throw "An unknown SelectionTTS.dll exists at $destination. It has not been replaced." }
        # Refuse to overwrite a loaded DLL. The installer never closes the user's editor.
        $lock = $null
        try { $lock = [IO.File]::Open($destination, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
        catch { throw 'Save your work and close Notepad++, then run this installer as administrator to update.' }
        finally { if ($lock) { $lock.Dispose() } }
        $backupDirectory = Join-Path $env:LOCALAPPDATA ('SelectionTTS\backups\' + $installedHash.ToLowerInvariant())
        New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
        $backup = Join-Path $backupDirectory 'SelectionTTS.dll'
        if (!(Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $destination -Destination $backup }
        if ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $installedHash) { throw 'Backup verification failed.' }
        Copy-Item -LiteralPath $source -Destination $destination -Force
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Updated DLL verification failed.' }
        Write-Output "Updated and verified: $destination"
    }
} else {
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Installed DLL verification failed.' }
    Write-Output "Installed and verified: $destination"
}
Write-Output 'Save your work and restart Notepad++ when ready. Then select text and press Ctrl+Alt+T.'
Write-Output 'Your current editor session and the existing Speech plugin were not changed.'
