param(
    [Parameter(Mandatory=$true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$SourceRoot = (Resolve-Path $SourceRoot).Path
$ExpectedCommit = '8802b2b470db0462fa1ed03a125e793a7c06d735'

function Replace-LiteralOne {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Old,
        [Parameter(Mandatory=$true)][string]$New,
        [Parameter(Mandatory=$true)][string]$Label
    )
    # Windows Git checkouts can contain CRLF even when the pinned source was authored with LF.
    # Normalize both the file and match strings so guards remain exact but are not EOL-sensitive.
    $text = [IO.File]::ReadAllText($Path).Replace("`r`n", "`n")
    $oldNormalized = $Old.Replace("`r`n", "`n")
    $newNormalized = $New.Replace("`r`n", "`n")
    $first = $text.IndexOf($oldNormalized, [StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Could not find $Label in $Path. This kit is pinned to v0.8.4 commit $ExpectedCommit." }
    $second = $text.IndexOf($oldNormalized, $first + $oldNormalized.Length, [StringComparison]::Ordinal)
    if ($second -ge 0) { throw "Found more than one $Label in $Path; refusing an ambiguous patch." }
    $patched = $text.Substring(0, $first) + $newNormalized + $text.Substring($first + $oldNormalized.Length)
    [IO.File]::WriteAllText($Path, $patched, [Text.UTF8Encoding]::new($false))
}

function Convert-ShaderToHeader {
    param(
        [Parameter(Mandatory=$true)][string]$InputFile,
        [Parameter(Mandatory=$true)][string]$OutputFile,
        [Parameter(Mandatory=$true)][string]$ArrayName
    )
    $bytes = [IO.File]::ReadAllBytes($InputFile)
    if ($bytes.Length -eq 0) { throw "Shader bytecode is empty: $InputFile" }
    $sb = [Text.StringBuilder]::new()
    [void]$sb.Append("#pragma once`n`n")
    [void]$sb.Append("inline static const unsigned char $ArrayName[] = {`n    ")
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        [void]$sb.Append(('0x{0:x2}' -f $bytes[$i]))
        if ($i -lt $bytes.Length - 1) { [void]$sb.Append(', ') }
        if ((($i + 1) % 12) -eq 0) { [void]$sb.Append("`n    ") }
    }
    [void]$sb.Append("`n};`n")
    [IO.File]::WriteAllText($OutputFile, $sb.ToString(), [Text.UTF8Encoding]::new($false))
}

Write-Host 'Applying validated v0.8.4 master baseline: ShadowFloor + Uncharted NR diagnostic bypass + RTX40 legacy Streamline 2.14 loading.'

# Config: a separate persisted option. 0.0 means stock v0.8.4 behaviour.
$configH = Join-Path $SourceRoot 'OptiScaler/Config.h'
Replace-LiteralOne -Path $configH `
    -Old '    CustomOptional<float> DlssNrMaxRatio { 2.0f };' `
    -New "    CustomOptional<float> DlssNrMaxRatio { 2.0f };`n    // Minimum relative brightness NR may leave. 0 keeps stock MaxRatio darkening behaviour.`n    CustomOptional<float> DlssNrShadowFloor { 0.0f };" `
    -Label 'DlssNrMaxRatio declaration'

$configCpp = Join-Path $SourceRoot 'OptiScaler/Config.cpp'
Replace-LiteralOne -Path $configCpp `
    -Old '            DlssNrMaxRatio.set_from_config(readFloat("DlssNr", "MaxRatio"));' `
    -New "            DlssNrMaxRatio.set_from_config(readFloat(`"DlssNr`", `"MaxRatio`"));`n            DlssNrShadowFloor.set_from_config(readFloat(`"DlssNr`", `"ShadowFloor`"));" `
    -Label 'ShadowFloor config read insertion'
Replace-LiteralOne -Path $configCpp `
    -Old '    ini.SetValue("DlssNr", "MaxRatio", GetFloatValue(Instance()->DlssNrMaxRatio.value_for_config()).c_str());' `
    -New "    ini.SetValue(`"DlssNr`", `"MaxRatio`", GetFloatValue(Instance()->DlssNrMaxRatio.value_for_config()).c_str());`n    ini.SetValue(`"DlssNr`", `"ShadowFloor`", GetFloatValue(Instance()->DlssNrShadowFloor.value_for_config()).c_str());" `
    -Label 'ShadowFloor config write insertion'

# Blend UI. Keep Highlight guard as the brightening bound; add an independent darkening floor.
$menu = Join-Path $SourceRoot 'OptiScaler/dlssnr/DlssNr_MenuBlend.cpp'
$oldMenu = @'
    HelpMarker("Limit pixel brightening and darkening.");
'@
$newMenu = @'
    HelpMarker("Maximum NR brightening. Shadow darkening is controlled separately below.");

    float shadowFloor = std::clamp(config->DlssNrShadowFloor.value_or_default(), 0.0f, 1.0f);
    if (ImGui::SliderFloat("Shadow minimum", &shadowFloor, 0.0f, 1.0f, "%.2fx"))
        config->DlssNrShadowFloor = shadowFloor;

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##shadowfloor"))
        config->DlssNrShadowFloor = 0.0f;

    HelpMarker("Minimum luminance NR may leave relative to the untouched frame. 0.80x = at most 20% darkening. 1.00x = no NR darkening. 0 = stock.");
'@
Replace-LiteralOne -Path $menu -Old $oldMenu.TrimEnd("`r", "`n") -New $newMenu.TrimEnd("`r", "`n") -Label 'Shadow minimum menu insertion'

# Main NR shader: reuse the existing ResidualBlend slot in this mode. The 256-byte CB ABI does not move.
$mainHlsl = Join-Path $SourceRoot 'OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl'
Replace-LiteralOne -Path $mainHlsl `
    -Old "    float gEnvironmentColour;`n};" `
    -New "    float gEnvironmentColour;`n    // In the normal NR shader this aliases DlssNrConstants::ResidualBlend, unused by this PSO.`n    float gShadowFloor;`n};" `
    -Label 'gShadowFloor cbuffer alias'
Replace-LiteralOne -Path $mainHlsl `
    -Old "    const float guard = max(gMaxRatio, 1.0);`n    float boundedRatio = clamp(amplified, 1.0 / guard, guard);" `
    -New "    const float guard = max(gMaxRatio, 1.0);`n    const float stockShadowFloor = 1.0 / guard;`n    const float shadowFloor = gShadowFloor > 0.0 ? clamp(gShadowFloor, stockShadowFloor, 1.0) : stockShadowFloor;`n    float boundedRatio = clamp(amplified, shadowFloor, guard);" `
    -Label 'main NR shadow ratio clamp'

# Normal DX12 resolve: put ShadowFloor in ResidualBlend only for this shader dispatch.
$encodeCpp = Join-Path $SourceRoot 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Encode.cpp'
Replace-LiteralOne -Path $encodeCpp `
    -Old '    resolveParams.MaxRatio = cfg.DlssNrMaxRatio.value_or_default();' `
    -New "    resolveParams.MaxRatio = cfg.DlssNrMaxRatio.value_or_default();`n    resolveParams.ResidualBlend = std::clamp(cfg.DlssNrShadowFloor.value_or_default(), 0.0f, 1.0f); // gShadowFloor alias" `
    -Label 'normal DX12 ShadowFloor propagation'

# Finished-colour shader: MvScaleX is unused by this PSO. Map it as a float at offset 36.
$finishedHlsl = Join-Path $SourceRoot 'OptiScaler/shaders/dlssnr/precompile/dlssnr_finished_color.hlsl'
Replace-LiteralOne -Path $finishedHlsl `
    -Old "    uint mode; float exposureScale; uint width; uint height;`n    float sceneIsLinear; float curveUpdateWeight; uint curveHistoryValid; float maxRatio;`n};" `
    -New "    uint mode; float exposureScale; uint width; uint height;`n    float sceneIsLinear; float curveUpdateWeight; uint curveHistoryValid; float maxRatio;`n    uint unusedPassthrough; float shadowFloor; // DlssNrConstants::MvScaleX in this PSO`n};" `
    -Label 'finished-colour ShadowFloor cbuffer alias'

Replace-LiteralOne -Path $finishedHlsl `
    -Old '    float wantedY = clamp(finalY + delta, finalY / limit, finalY * limit);' `
    -New "    float lowerGain = shadowFloor > 0.0 ? clamp(shadowFloor, 1.0 / limit, 1.0) : 1.0 / limit;`n    float wantedY = clamp(finalY + delta, finalY * lowerGain, finalY * limit);" `
    -Label 'finished HDR-fit luminance floor'
Replace-LiteralOne -Path $finishedHlsl `
    -Old '    float minScale = (1.0 / limit) / min(gain.r, min(gain.g, gain.b));' `
    -New '    float minScale = lowerGain / min(gain.r, min(gain.g, gain.b));' `
    -Label 'finished HDR-fit gain floor'
Replace-LiteralOne -Path $finishedHlsl `
    -Old '        float3 gain = clamp(1.0 + (edited - base) / max(base, floorValue), 1.0 / limit, limit);' `
    -New "        float lowerGain = shadowFloor > 0.0 ? clamp(shadowFloor, 1.0 / limit, 1.0) : 1.0 / limit;`n        float3 gain = clamp(1.0 + (edited - base) / max(base, floorValue), lowerGain, limit);" `
    -Label 'finished residual encode shadow floor'
Replace-LiteralOne -Path $finishedHlsl `
    -Old '        float3 gain = exp2(clamp((carrier - 0.5) * 8.0, -log2(limit), log2(limit)));' `
    -New "        float lowerGain = shadowFloor > 0.0 ? clamp(shadowFloor, 1.0 / limit, 1.0) : 1.0 / limit;`n        float3 gain = exp2(clamp((carrier - 0.5) * 8.0, log2(lowerGain), log2(limit)));" `
    -Label 'finished residual apply shadow floor'

# Deferred finished-picture encode path.
$deferredCpp = Join-Path $SourceRoot 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_DeferredSr.cpp'
Replace-LiteralOne -Path $deferredCpp `
    -Old '            encode.MaxRatio = std::clamp(cfg.DlssNrMaxRatio.value_or_default(), 1.0f, 8.0f);' `
    -New "            encode.MaxRatio = std::clamp(cfg.DlssNrMaxRatio.value_or_default(), 1.0f, 8.0f);`n            encode.MvScaleX = std::clamp(cfg.DlssNrShadowFloor.value_or_default(), 0.0f, 1.0f); // finished shader alias" `
    -Label 'deferred finished-picture ShadowFloor propagation'

# Finished-picture apply path.
$finishedCpp = Join-Path $SourceRoot 'OptiScaler/shaders/dlssnr/DlssNr_Dx12_FinishedCompose.cpp'
Replace-LiteralOne -Path $finishedCpp `
    -Old '            apply.MaxRatio = std::clamp(Config::Instance()->DlssNrMaxRatio.value_or_default(), 1.0f, 8.0f);' `
    -New "            apply.MaxRatio = std::clamp(Config::Instance()->DlssNrMaxRatio.value_or_default(), 1.0f, 8.0f);`n            apply.MvScaleX = std::clamp(Config::Instance()->DlssNrShadowFloor.value_or_default(), 0.0f, 1.0f); // finished shader alias" `
    -Label 'finished-picture ShadowFloor propagation'




# Uncharted NR initialization fix: v0.8.4's diagnostic RuntimeReport hashes and reads
# version metadata from every nvngx_dlssnr.dll candidate before/after CreateFeature(18).
# A modified RTX40 compatibility runtime can terminate the process in that inspection,
# before the existing CompatibilityRuntime direct fallback is reached. Keep all creation
# and fallback code, but replace only those two diagnostic file-inspection calls.
$nrProxy = Join-Path $SourceRoot 'OptiScaler/dlssnr/DlssNr_Proxy.cpp'
Replace-LiteralOne -Path $nrProxy `
    -Old '        NgxDiagnostics::RuntimeReport(cmdList, device, "before CreateFeature(18)");' `
    -New '        LOG_INFO("NR compatibility: runtime file diagnostics bypassed before CreateFeature(18)");' `
    -Label 'pre-CreateFeature RuntimeReport call'
Replace-LiteralOne -Path $nrProxy `
    -Old '        NgxDiagnostics::RuntimeReport(cmdList, device, "after CreateFeature(18)");' `
    -New '        LOG_INFO("NR compatibility: runtime file diagnostics bypassed after CreateFeature(18)");' `
    -Label 'post-CreateFeature RuntimeReport call'





# RTX40/Ada compatibility for legacy Streamline hosts (for example Miles Morales).
# Some hosts permit OTA discovery/update but omit eLoadDownloadedPlugins, which leaves
# the bundled SL 2.9 DLSS-G plugin active and caps MFG at 3 generated frames.
# Enable NVIDIA's downloaded Streamline plugins before any OptiScaler FG route selection.
$streamlineHooks = Join-Path $SourceRoot 'OptiScaler/hooks/Streamline_Hooks.cpp'
$streamlineText = [IO.File]::ReadAllText($streamlineHooks)
$marker = 'RTX40 MFG legacy Streamline: eLoadDownloadedPlugins enabled before FG route selection'
if (-not $streamlineText.Contains($marker)) {
    Replace-LiteralOne -Path $streamlineHooks `
        -Old @'
    localPref.logLevel = sl::LogLevel::eCount;
    localPref.logMessageCallback = &streamlineLogCallback;

    // renderAPI is optional so need to be careful, should only matter for Vulkan
'@.TrimEnd("`r", "`n") `
        -New @'
    localPref.logLevel = sl::LogLevel::eCount;
    localPref.logMessageCallback = &streamlineLogCallback;

#if defined(OPTISCALER_RTX40_MFG)
    // Legacy hosts can allow OTA discovery/update while omitting eLoadDownloadedPlugins.
    // With Ada MFG unlock enabled, load NVIDIA's downloaded Streamline plugins before
    // any FG route selection so modern sl.dlss_g can expose all five generated frames.
    if (Config::Instance()->FGDLSSGAdaMfgUnlock.value_or_default())
    {
        localPref.flags |= sl::PreferenceFlags::eLoadDownloadedPlugins;
        LOG_INFO("RTX40 MFG legacy Streamline: eLoadDownloadedPlugins enabled before FG route selection");
    }
#endif

    // renderAPI is optional so need to be careful, should only matter for Vulkan
'@.TrimEnd("`r", "`n") `
        -Label 'early RTX40 Ada legacy Streamline downloaded-plugin enablement'
}
if (-not ([IO.File]::ReadAllText($streamlineHooks)).Contains($marker)) {
    throw 'RTX40 legacy Streamline compatibility verification failed.'
}
if (([IO.File]::ReadAllText($streamlineHooks)).Contains('interpolation override clamped for this host')) {
    throw 'Stale host-max interpolation clamp found; refusing to build master.'
}
if (-not ([IO.File]::ReadAllText($streamlineHooks)).Contains('newOptions.numFramesToGenerate = overrideCount;')) {
    throw 'Expected unclamped interpolation override path is missing.'
}


# Recompile the two DX12 shaders changed by ShadowFloor.
$precompile = Join-Path $SourceRoot 'OptiScaler/shaders/dlssnr/precompile'
$fxc = Join-Path $SourceRoot 'OptiScaler/shaders/shader_tools/fxc.exe'
if (-not (Test-Path -LiteralPath $fxc -PathType Leaf)) { throw "Shader compiler not found: $fxc" }

$mainCso = Join-Path $precompile 'DlssNr_Shader.cso'
$mainHeader = Join-Path $precompile 'DlssNr_Shader.h'
$finishedCso = Join-Path $precompile 'dlssnr_finished_color_Shader.cso'
$finishedHeader = Join-Path $precompile 'dlssnr_finished_color_Shader.h'
Push-Location $precompile
try {
    & $fxc -T cs_5_0 -E CSMain -O3 'dlssnr.hlsl' -Fo $mainCso
    if ($LASTEXITCODE -ne 0) { throw "fxc failed for dlssnr.hlsl: $LASTEXITCODE" }
    Convert-ShaderToHeader -InputFile $mainCso -OutputFile $mainHeader -ArrayName 'DlssNr_cso'

    & $fxc -T cs_5_0 -E CSMain -O3 'dlssnr_finished_color.hlsl' -Fo $finishedCso
    if ($LASTEXITCODE -ne 0) { throw "fxc failed for dlssnr_finished_color.hlsl: $LASTEXITCODE" }
    Convert-ShaderToHeader -InputFile $finishedCso -OutputFile $finishedHeader -ArrayName 'dlssnr_finished_color_cso'
}
finally { Pop-Location }



$required = @(
    @{ Path=(Join-Path $SourceRoot 'OptiScaler/Config.h'); Text='DlssNrShadowFloor' },
    @{ Path=(Join-Path $SourceRoot 'OptiScaler/dlssnr/DlssNr_Proxy.cpp'); Text='runtime file diagnostics bypassed before CreateFeature(18)' },
    @{ Path=(Join-Path $SourceRoot 'OptiScaler/hooks/Streamline_Hooks.cpp'); Text='RTX40 MFG legacy Streamline: eLoadDownloadedPlugins enabled before FG route selection' }
)
foreach ($r in $required) {
    if (-not ([IO.File]::ReadAllText($r.Path)).Contains($r.Text)) {
        throw "Master patch verification failed: $($r.Text)"
    }
}
Write-Host 'Validated v0.8.4 master baseline patch applied.'
