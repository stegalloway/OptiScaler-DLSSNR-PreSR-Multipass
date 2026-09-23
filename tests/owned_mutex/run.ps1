$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo 'x64/owned-mutex-validation'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$source = Get-Content -Raw -LiteralPath (Join-Path $repo 'OptiScaler/OwnedMutex.h')
$needle = '#include "SysUtils.h"'
if (-not $source.Contains($needle)) { throw 'Production header layout changed' }
$source = $source.Replace($needle, '#define LOG_WARN(...) ((void)0)')
Set-Content -LiteralPath (Join-Path $out 'OwnedMutexUnderTest.h') -Value $source -Encoding utf8
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
& cl.exe /nologo /std:c++20 /EHsc /W4 /I $out (Join-Path $PSScriptRoot 'OwnedMutexTests.cpp') "/Fe:$out/OwnedMutexTests.exe" "/Fo:$out/OwnedMutexTests.obj"
if ($LASTEXITCODE) { throw 'OwnedMutex test compilation failed' }
& (Join-Path $out 'OwnedMutexTests.exe')
if ($LASTEXITCODE) { throw 'OwnedMutex test failed' }
