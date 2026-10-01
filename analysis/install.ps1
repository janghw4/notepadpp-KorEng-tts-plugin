[CmdletBinding()]
param([string]$NotepadDirectory = (Join-Path $env:ProgramFiles 'Notepad++'))
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$NotepadDirectory = [IO.Path]::GetFullPath($NotepadDirectory)
$source = Join-Path $projectRoot 'dist\KorEngTTS\KorEngTTS.dll'
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
$destinationDirectory = Join-Path $NotepadDirectory 'plugins\KorEngTTS'
$destination = Join-Path $destinationDirectory 'KorEngTTS.dll'
$legacy = Join-Path $NotepadDirectory 'plugins\SelectionTTS\SelectionTTS.dll'
$expectedHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$replaceableHashes = @('7917050F0FF6A6420C87EA51837BCA3790E9E88CC6B723F02004B3D95CB721C3', '6E762A2794AC8BA9121A1D6212F9C6826342919DA6AD79A69B7BA59453EDDD49', '25628E543456FABA9BD7261CFE308B348B26C0E69B70D3B06A4CE5485B20100C', '5EA5FF35E7674136C906275BBCF58AADF5561DBABDCC92B78346E396419E9E1C')
function Assert-PluginClosed([string]$Path) {
    $lock = $null
    try { $lock = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
    catch { throw 'Save your work and close Notepad++, then run this installer as administrator to update.' }
    finally { if ($lock) { $lock.Dispose() } }
}
# Validate both installations before writing or archiving either binary.
$installedHash = $null
if (Test-Path -LiteralPath $destination) {
    $installedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    if ($installedHash -ne $expectedHash -and $installedHash -notin $replaceableHashes) { throw "An unknown KorEngTTS.dll exists at $destination. It has not been replaced." }
    if ($installedHash -ne $expectedHash) { Assert-PluginClosed $destination }
}
$legacyHash = $null
if (Test-Path -LiteralPath $legacy) {
    $legacyHash = (Get-FileHash -LiteralPath $legacy -Algorithm SHA256).Hash
    if ($legacyHash -notin $replaceableHashes) { throw "An unknown SelectionTTS.dll exists at $legacy. It has not been archived." }
    Assert-PluginClosed $legacy
}
if ($installedHash -eq $expectedHash) {
    Write-Output "The same plugin is already installed: $destination"
} else {
    if ($installedHash) {
        $backupDirectory = Join-Path $env:LOCALAPPDATA ('KorEngTTS\backups\' + $installedHash.ToLowerInvariant())
        New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
        $backup = Join-Path $backupDirectory 'KorEngTTS.dll'
        if (!(Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $destination -Destination $backup }
        if ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $installedHash) { throw 'Backup verification failed.' }
    }
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Installed DLL verification failed.' }
    Write-Output "Installed and verified: $destination"
}
if ($legacyHash) {
    $backupRoot = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'KorEngTTS\backups'))
    $archiveDirectory = Join-Path $backupRoot ($legacyHash.ToLowerInvariant() + '\' + [guid]::NewGuid().ToString('N'))
    $archive = [IO.Path]::GetFullPath((Join-Path $archiveDirectory 'SelectionTTS.dll'))
    $installPrefix = $NotepadDirectory.TrimEnd('\') + '\'
    if (!$legacy.StartsWith($installPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        !$archive.StartsWith(($backupRoot.TrimEnd('\') + '\'), [StringComparison]::OrdinalIgnoreCase)) { throw 'Archive path validation failed.' }
    New-Item -ItemType Directory -Path $archiveDirectory -Force | Out-Null
    Move-Item -LiteralPath $legacy -Destination $archive
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $legacyHash) { throw 'Legacy archive verification failed.' }
    Write-Output "Archived the previous Selection TTS binary: $archive"
}
Write-Output 'Save your work and restart Notepad++ when ready. Then select text and press Ctrl+Alt+T.'
Write-Output 'Your current editor session and the existing Speech plugin were not changed.'
