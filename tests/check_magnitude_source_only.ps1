$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$header=Join-Path $root 'OptiScaler\framegen\dlssg\mfgquality\magnitude_cubins.generated.hpp'
$park=$header+'.source-only-check'
$src=Join-Path $env:TEMP 'mfg_magnitude_source_only.cpp'
$exe=Join-Path $env:TEMP 'mfg_magnitude_source_only.exe'
@'
#define NOMINMAX
#include <windows.h>
#include "framegen/dlssg/MfgQuality.h"
#if MFGUNLOCK_HAS_GENERATED_MAGNITUDE_CUBINS
#error source-only magnitude check unexpectedly found local provider payloads
#endif
int main() {
    MfgQuality::Options options{5, false, 64};
    return options.mode == 5 && MfgMagnitude::IsValidDisplayThresholdPx(options.magnitudeThresholdPx) ? 0 : 1;
}
'@ | Set-Content -LiteralPath $src -Encoding UTF8
$parked=$false
try {
    if(Test-Path -LiteralPath $header){
        Move-Item -LiteralPath $header -Destination $park
        $parked=$true
    }
    $cmd=Join-Path $env:TEMP 'mfg_magnitude_source_only.cmd'
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /EHsc /W4 /I "$root\OptiScaler" "$src" /Fe:"$exe"
if errorlevel 1 exit /b 1
"$exe"
"@ | Set-Content -LiteralPath $cmd -Encoding ASCII
    & $cmd
    if($LASTEXITCODE){throw "source-only magnitude compile/check failed"}
    Write-Output 'PASS: source-only build compiles with magnitude payload table absent'
}
finally {
    if(Test-Path -LiteralPath $park){Move-Item -LiteralPath $park -Destination $header -Force}
}
