[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'dist\SelectionTTS' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ Build Tools are required.' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Install the Visual Studio Desktop development with C++ workload.' }
& (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$buildRoot = Join-Path ([IO.Path]::GetTempPath()) ('selection-tts-build-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $buildRoot,$OutputDirectory -Force | Out-Null
$resource = Join-Path $buildRoot 'selection_tts.res'
& rc.exe /nologo "/fo$resource" (Join-Path $PSScriptRoot 'selection_tts.rc')
if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
$dll = Join-Path $OutputDirectory 'SelectionTTS.dll'
& cl.exe /nologo /std:c++17 /utf-8 /EHsc /O2 /MT /W4 /WX /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /LD "/I$(Join-Path $projectRoot 'reference')" "/Fo$(Join-Path $buildRoot 'selection_tts.obj')" (Join-Path $PSScriptRoot 'selection_tts.cpp') $resource /link "/OUT:$dll" "/IMPLIB:$(Join-Path $buildRoot 'SelectionTTS.lib')" /DYNAMICBASE /NXCOMPAT /CETCOMPAT user32.lib ole32.lib sapi.lib uuid.lib advapi32.lib comctl32.lib
if ($LASTEXITCODE -ne 0) { throw 'Plugin compilation failed.' }
$exports = & dumpbin.exe /nologo /exports $dll
foreach ($symbol in @('setInfo','getName','getFuncsArray','beNotified','messageProc','isUnicode')) {
    if (!($exports -match "\s$symbol\s*$")) { throw "Missing plugin export: $symbol" }
}
Get-FileHash -LiteralPath $dll -Algorithm SHA256 | Select-Object Path,Hash
