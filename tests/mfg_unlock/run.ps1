param([string]$Runtime)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $repo 'x64/rtx40-mfg-validation'
New-Item -ItemType Directory -Force "$out/seams/misc", "$out/seams/proxies" | Out-Null
foreach ($header in @('pch.h', 'SysUtils.h', 'Config.h', 'Util.h', 'misc/IdentifyGpu.h', 'proxies/KernelBase_Proxy.h')) {
    Set-Content -LiteralPath "$out/seams/$header" -Value '// Supplied by Mocks.h; patching and scanning are production code.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$build = @"
@echo off
call "$vs/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /DUNICODE /D_UNICODE /DOPTISCALER_RTX40_MFG /I "$out/seams" /I "$repo/OptiScaler" "$PSScriptRoot/PatchTests.cpp" /Fe:"$out/mfg-patch.exe" /Fo:"$out/mfg-patch.obj"
"@
Set-Content -LiteralPath "$out/build.cmd" -Value $build
& "$out/build.cmd"
if ($LASTEXITCODE) { throw 'MFG regression build failed' }
foreach ($case in @('disabled', 'blackwell', 'ampere', 'other-vendor', 'restart', 'plan-invalid-pointer', 'missing-gate', 'duplicate-gate', 'mixed-families', 'unknown', 'bad-image', 'bad-section', 'no-kernel', 'malformed', 'legacy', '3109', 'sr-only', 'mixed-exports', 'neither-export', 'retain-failure', 'protect-fail-late', 'rollback-incomplete', 'second-provider', 'second-provider-unsupported', 'second-provider-fail-clean', 'second-provider-fail-unsafe', 'second-provider-retain-failure', 'second-provider-capacity', 'second-non-provider', 'second-directsr-only')) {
    & "$out/mfg-patch.exe" $case
    if ($LASTEXITCODE) { throw "MFG regression failed: $case" }
}
$transactionBuild = @"
@echo off
call "$vs/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 "$PSScriptRoot/TransactionTests.cpp" /Fe:"$out/mfg-transaction.exe" /Fo:"$out/mfg-transaction.obj"
"@
Set-Content -LiteralPath "$out/transaction-build.cmd" -Value $transactionBuild
& "$out/transaction-build.cmd"
if ($LASTEXITCODE) { throw 'MFG transaction regression build failed' }
& "$out/mfg-transaction.exe"
if ($LASTEXITCODE) { throw 'MFG transaction regression failed' }
Write-Output 'PASS MFG transaction rollback/protection/cache checks'
# Host checks of the option helpers (provider discovery, plugin ceiling, PTX rewrite, temporal method, flip
# metering). They compile alone against synthetic PE images; no NVIDIA code executes.
foreach ($smoke in @('mfg_provider_smoke', 'mfg_ceiling_smoke', 'mfg_ptx_smoke', 'mfg_method_smoke', 'mfg_flipmeter_smoke')) {
    $smokeBuild = @"
@echo off
call "$vs/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 "$repo/tests/$smoke.cpp" /Fe:"$out/$smoke.exe" /Fo:"$out/$smoke.obj"
"@
    Set-Content -LiteralPath "$out/$smoke.cmd" -Value $smokeBuild
    & "$out/$smoke.cmd"
    if ($LASTEXITCODE) { throw "$smoke build failed" }
    & "$out/$smoke.exe"
    if ($LASTEXITCODE) { throw "$smoke failed" }
}
if ($Runtime) {
    $before = (Get-FileHash -LiteralPath $Runtime -Algorithm SHA256).Hash
    & "$out/mfg-patch.exe" runtime $Runtime
    if ($LASTEXITCODE) { throw 'Installed runtime patch smoke failed' }
    foreach ($qualityCase in @('runtime-quality-1', 'runtime-quality-2', 'runtime-quality-3',
                               'runtime-quality-4', 'runtime-quality-5-16', 'runtime-quality-5-32',
                               'runtime-quality-5-48', 'runtime-quality-5-64',
                               'runtime-quality-1-warp', 'runtime-quality-2-warp', 'runtime-quality-3-warp',
                               'runtime-quality-4-fail-clean', 'runtime-quality-4-fail-unsafe',
                               'runtime-quality-4-bad-fingerprint',
                               'runtime-quality-5-64-fail-clean', 'runtime-quality-5-64-fail-unsafe',
                               'runtime-quality-5-64-bad-fingerprint')) {
        & "$out/mfg-patch.exe" $qualityCase $Runtime
        if ($LASTEXITCODE) { throw "Quality profile failed: $qualityCase" }
    }
    if ((Get-FileHash -LiteralPath $Runtime -Algorithm SHA256).Hash -ne $before) { throw 'Runtime file changed' }
}
