[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'dist\releases' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$dll = Join-Path $projectRoot 'dist\KorEngTTS\KorEngTTS.dll'
if (!(Test-Path -LiteralPath $dll -PathType Leaf)) { throw 'Build the plugin before packaging.' }
$archive = Join-Path $OutputDirectory 'KorEngTTS_x64.zip'
if (Test-Path -LiteralPath $archive) { throw 'The release ZIP already exists. Choose an empty output directory.' }
$stage = Join-Path ([IO.Path]::GetTempPath()) ('selection-tts-package-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage,$OutputDirectory -Force | Out-Null
# Explicit file list keeps local reports and editor data out of release packages.
$files = @(
    '.gitignore', '.gitattributes', '.github\workflows\build.yml',
    'README.md', 'README.ko.md', 'CHANGELOG.md', 'LICENSE', 'install.cmd',
    'analysis\build.ps1', 'analysis\test.ps1', 'analysis\package.ps1',
    'analysis\install.ps1', 'analysis\test_install.ps1', 'analysis\test_plugin.cpp', 'analysis\test_notepad.py',
    'analysis\selection_tts.cpp', 'analysis\selection_tts.rc',
    'analysis\speech_core.hpp', 'analysis\resource.h',
    'reference\PluginInterface.h', 'reference\Notepad_plus_msgs.h',
    'reference\Scintilla.h', 'reference\Sci_Position.h',
    'reference\scintilla_license.txt', 'reference\provenance.json',
    'dist\KorEngTTS\KorEngTTS.dll'
)
foreach ($relative in $files) {
    $source = Join-Path $projectRoot $relative
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Required release file is missing: $relative" }
    $destination = Join-Path $stage $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}
$utf8 = New-Object Text.UTF8Encoding($false)
$dllHash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $stage 'SHA256SUMS'), "$dllHash  dist/KorEngTTS/KorEngTTS.dll`n", $utf8)
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
$zipHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($archive + '.sha256'), "$zipHash  KorEngTTS_x64.zip`n", $utf8)
Get-FileHash -LiteralPath $archive -Algorithm SHA256 | Select-Object Path,Hash
