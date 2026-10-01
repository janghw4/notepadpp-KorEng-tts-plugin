[CmdletBinding()]
param([int]$AudioOutputIndex = -1, [switch]$CoreOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Visual Studio C++ Build Tools are required.' }
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('selection-tts-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$testExe = Join-Path $testRoot 'test_plugin.exe'
& cl.exe /nologo /std:c++17 /utf-8 /EHsc /O2 /MT /W4 /WX /permissive- /DUNICODE /D_UNICODE /DNOMINMAX "/I$(Join-Path $projectRoot 'reference')" "/Fo$(Join-Path $testRoot 'test_plugin.obj')" (Join-Path $PSScriptRoot 'test_plugin.cpp') /link "/OUT:$testExe" "/IMPLIB:$(Join-Path $testRoot 'test_plugin.lib')" user32.lib ole32.lib sapi.lib uuid.lib advapi32.lib comctl32.lib
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
$mode = if ($CoreOnly) { '--core-only' } else { '--full' }
& $testExe (Join-Path $testRoot 'audio') $AudioOutputIndex $mode
if ($LASTEXITCODE -ne 0) { throw 'Plugin tests failed.' }
Write-Output "Test artifacts: $testRoot"
