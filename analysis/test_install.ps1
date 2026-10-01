[CmdletBinding()]
param([string]$LegacyDll)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$installer = Join-Path $PSScriptRoot 'install.ps1'
$source = Join-Path $projectRoot 'dist\KorEngTTS\KorEngTTS.dll'
$expectedHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('koreng-tts-installer-' + [guid]::NewGuid().ToString('N'))
$savedLocalAppData = $env:LOCALAPPDATA
$script:checks = 0
function Require([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
    $script:checks++
}
function New-TestEditor([string]$Case) {
    $directory = Join-Path $testRoot $Case
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    # Only the PE architecture header is needed. This file is never executed.
    $bytes = New-Object byte[] 512
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]0x5A4D),0,$bytes,0,2)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([int]0x80),0,$bytes,0x3C,4)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint32]0x4550),0,$bytes,0x80,4)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint16]0x8664),0,$bytes,0x84,2)
    [IO.File]::WriteAllBytes((Join-Path $directory 'notepad++.exe'),$bytes)
    return $directory
}
try {
    # This environment override is confined to the test process and restored below.
    $env:LOCALAPPDATA = Join-Path $testRoot 'appdata'
    $fresh = New-TestEditor 'fresh'
    & $installer -NotepadDirectory $fresh | Out-Null
    $installed = Join-Path $fresh 'plugins\KorEngTTS\KorEngTTS.dll'
    Require ((Get-FileHash -LiteralPath $installed).Hash -eq $expectedHash) 'Fresh installation did not match the built DLL.'
    $written = (Get-Item -LiteralPath $installed).LastWriteTimeUtc
    & $installer -NotepadDirectory $fresh | Out-Null
    Require ((Get-Item -LiteralPath $installed).LastWriteTimeUtc -eq $written) 'Installing the same DLL changed the file.'

    $unknownNew = New-TestEditor 'unknown-new'
    $unknownPath = Join-Path $unknownNew 'plugins\KorEngTTS\KorEngTTS.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $unknownPath) -Force | Out-Null
    [IO.File]::WriteAllText($unknownPath,'Unrecognized test DLL')
    $rejected = $false
    try { & $installer -NotepadDirectory $unknownNew | Out-Null } catch { $rejected = $_.Exception.Message -like '*unknown KorEngTTS.dll*' }
    Require ($rejected -and [IO.File]::ReadAllText($unknownPath) -eq 'Unrecognized test DLL') 'An unknown new DLL was replaced.'

    $unknownOld = New-TestEditor 'unknown-legacy'
    $oldPath = Join-Path $unknownOld 'plugins\SelectionTTS\SelectionTTS.dll'
    New-Item -ItemType Directory -Path (Split-Path -Parent $oldPath) -Force | Out-Null
    [IO.File]::WriteAllText($oldPath,'Unrecognized legacy test DLL')
    $rejected = $false
    try { & $installer -NotepadDirectory $unknownOld | Out-Null } catch { $rejected = $_.Exception.Message -like '*unknown SelectionTTS.dll*' }
    Require ($rejected -and [IO.File]::ReadAllText($oldPath) -eq 'Unrecognized legacy test DLL' -and !(Test-Path -LiteralPath (Join-Path $unknownOld 'plugins\KorEngTTS\KorEngTTS.dll'))) 'Unknown legacy DLL preflight did not preserve the installation.'

    if ($LegacyDll) {
        $legacyHash = (Get-FileHash -LiteralPath $LegacyDll).Hash
        $migration = New-TestEditor 'legacy-upgrade'
        $oldPath = Join-Path $migration 'plugins\SelectionTTS\SelectionTTS.dll'
        $settings = Join-Path $migration 'plugins\Config\SelectionTTS.ini'
        New-Item -ItemType Directory -Path (Split-Path -Parent $oldPath),(Split-Path -Parent $settings) -Force | Out-Null
        Copy-Item -LiteralPath $LegacyDll -Destination $oldPath
        [IO.File]::WriteAllText($settings,"[Speech]`r`nRate=3`r`n")
        $settingsHash = (Get-FileHash -LiteralPath $settings).Hash
        $lock = [IO.File]::Open($oldPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
        try {
            $rejected = $false
            try { & $installer -NotepadDirectory $migration | Out-Null } catch { $rejected = $_.Exception.Message -like '*close Notepad++*' }
            Require ($rejected -and (Test-Path -LiteralPath $oldPath) -and !(Test-Path -LiteralPath (Join-Path $migration 'plugins\KorEngTTS\KorEngTTS.dll'))) 'A locked legacy DLL was migrated.'
        } finally { $lock.Dispose() }
        & $installer -NotepadDirectory $migration | Out-Null
        $newPath = Join-Path $migration 'plugins\KorEngTTS\KorEngTTS.dll'
        Require (!(Test-Path -LiteralPath $oldPath) -and (Get-FileHash -LiteralPath $newPath).Hash -eq $expectedHash) 'Legacy migration left duplicate plugins or installed the wrong DLL.'
        $archives = @(Get-ChildItem -LiteralPath (Join-Path $env:LOCALAPPDATA 'KorEngTTS\backups') -Filter 'SelectionTTS.dll' -Recurse)
        Require ($archives.Count -eq 1 -and (Get-FileHash -LiteralPath $archives[0].FullName).Hash -eq $legacyHash) 'The legacy binary was not preserved in its archive.'
        Require ((Get-FileHash -LiteralPath $settings).Hash -eq $settingsHash) 'Installation changed the original settings.'
    }
    Write-Output "PASS: $script:checks installer checks; synthetic installations only."
    Write-Output "Test artifacts: $testRoot"
} finally { $env:LOCALAPPDATA = $savedLocalAppData }
