param(
    [string]$LogDirectory = $env:TEMP
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

$repo=(Resolve-Path "$PSScriptRoot/..").Path
$stamp=Get-Date -Format yyyyMMdd-HHmmss
$out=Join-Path $LogDirectory "rdr2-compat-$stamp"
New-Item -ItemType Directory -Force -Path $out | Out-Null

& cl.exe /nologo /std:c++20 /EHsc /W4 "$PSScriptRoot/rdr2_clock_tests.cpp" "/Fe:$out/rdr2-clock.exe" "/Fo:$out/rdr2-clock.obj"
if($LASTEXITCODE){throw 'RDR2 clock compilation failed'}
& "$out/rdr2-clock.exe"
if($LASTEXITCODE){throw 'RDR2 clock tests failed'}

$guard=Get-Content -Raw -LiteralPath "$repo/OptiScaler/hooks/Rdr2PureDark.h"
if($guard -notmatch 'RDR2\.exe'){throw 'RDR2 executable guard missing'}

$dllmain=Get-Content -Raw -LiteralPath "$repo/OptiScaler/dllmain.cpp"
if($dllmain -notmatch 'IsRdr2PureDarkCoexistence\(\)'){throw 'RDR2 dllmain ownership guard missing'}
if($dllmain -notmatch 'FGInput::NoFG' -or $dllmain -notmatch 'FGOutput::NoFG'){throw 'RDR2 must disable OptiScaler FG ownership'}
foreach($setting in @('StreamlineSpoofing','DxgiSpoofing','UseFakenvapi','OverlayMenu')){
    if($dllmain -notmatch ($setting+'\.set_volatile_value\(false\)')){throw "RDR2 setting not forced off: $setting"}
}

foreach($file in @('D3D11_Hooks.cpp','D3D12_Hooks.cpp','Dxgi_Hooks.cpp')){
    $text=Get-Content -Raw -LiteralPath "$repo/OptiScaler/hooks/$file"
    if($text -notmatch 'IsRdr2PureDarkCoexistence\(\)'){throw "RDR2 global hook bypass missing: $file"}
}

$stream=Get-Content -Raw -LiteralPath "$repo/OptiScaler/hooks/Streamline_Hooks.cpp"
foreach($name in @('hookInterposer','hookDlss','hookLocalDlssg','hookReflex','hookPcl','hookCommon')){
    if($stream -notmatch ('void StreamlineHooks::'+$name+'\([^)]*\)[\s\S]{0,260}IsRdr2PureDark')){
        throw "Missing PureDark ownership guard: $name"
    }
}
if($stream -notmatch 'RDR2 PureDark MFG bridge' -or
   $stream -notmatch 'strcmp\(functionName, "slDLSSGSetOptions"\)' -or
   $stream -notmatch 'strcmp\(functionName, "slDLSSGGetState"\)'){
    throw 'Narrow RDR2 DLSS-G SetOptions/GetState bridge missing'
}
if($stream -notmatch 'DlssgHookLifecycle::ValidateForDetach' -or $stream -notmatch 'retired: stale-discarded'){
    throw 'Current generation-safe DLSS-G hook lifetime logic missing'
}

$nvngx=Get-Content -Raw -LiteralPath "$repo/OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp"
if($nvngx -notmatch 'ShouldApplyDlssgEvaluationOverride[\s\S]{0,550}IsRdr2PureDarkCoexistence\(\)[\s\S]{0,160}return false'){
    throw 'Direct NVNGX DLSS-G override must be disabled under PureDark'
}

$d3d=Get-Content -Raw -LiteralPath "$repo/OptiScaler/hooks/D3D12_Hooks.cpp"
if($d3d -notmatch 'o_Rdr2Reset\s*=\s*\(PFN_Rdr2Reset\) pVTable\[10\]'){throw 'Process-lifetime RDR2 Reset hook missing'}
if($d3d -notmatch 'const HRESULT result = o_Rdr2Reset[\s\S]{0,240}NoteRdr2CommandListReset[\s\S]{0,120}SUCCEEDED\(result\)'){
    throw 'RDR2 Reset must observe the real Reset result before advancing recording generation'
}

$tracking=Get-Content -Raw -LiteralPath "$repo/OptiScaler/resource_tracking/ResTrack_dx12.cpp"
if($tracking -notmatch 'o_ExecuteCommandLists\(queue, count, lists\);\s*DlssNr::NoteRdr2CommandListsSubmitted\(count, lists\);'){
    throw 'RDR2 private submission epoch must advance only after real ExecuteCommandLists'
}
if($tracking -notmatch 'RDR2 PureDark late-NR device[\s\S]{0,360}HookNrQueue\(device\)'){
    throw 'RDR2 late-NR queue hook must unwrap the real D3D12 device first'
}

$feature=Get-Content -Raw -LiteralPath "$repo/OptiScaler/upscalers/IFeature_Dx12.cpp"
if($feature -notmatch 'submissionEpoch\s*=\s*DlssNr::SubmissionEpoch_Dx12\(InCommandList\)'){
    throw 'RDR2/private submission epoch is not feeding ordinary non-interop NR'
}

$eval=Get-Content -Raw -LiteralPath "$repo/OptiScaler/shaders/dlssnr/DlssNr_Dx12_Evaluate.cpp"
if($eval -notmatch '\(interop \|\| IsRdr2PureDarkCoexistence\(\)\) \? submissionEpoch'){
    throw 'Modern native-DX12 seam still discards the RDR2 private submitted epoch'
}

$nr=Get-Content -Raw -LiteralPath "$repo/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp"
if($nr -notmatch 'rdr2NrClock\.Register' -or $nr -notmatch 'rdr2NrClock\.Submitted' -or $nr -notmatch 'rdr2NrClock\.Clear'){
    throw 'RDR2 private clock register/submit/shutdown wiring incomplete'
}

$project=Get-Content -Raw -LiteralPath "$repo/OptiScaler/OptiScaler.vcxproj"
foreach($header in @('DlssNr_Rdr2Clock.h','DlssNr_Rdr2Epoch.h','Rdr2PureDark.h')){
    if($project -notmatch [regex]::Escape($header)){throw "Project registration missing: $header"}
}

Write-Output 'PASS RDR2 compatibility: PureDark ownership, narrow MFG bridge, generation-safe hook lifetime, private Reset/post-submit epoch and modern DX12 seam'
