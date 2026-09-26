#include <windows.h>
#include <d3d12.h>

#include <cassert>
#include <cstdio>

#include "../OptiScaler/framegen/dlssg/MfgHdrUiDiagnostics.h"

static sl::Resource MakeResource(uint32_t width, uint32_t height, uint32_t format, uintptr_t id)
{
    sl::Resource resource(sl::ResourceType::eTex2d, reinterpret_cast<void*>(id), D3D12_RESOURCE_STATE_COMMON);
    resource.width = width;
    resource.height = height;
    resource.nativeFormat = format;
    return resource;
}

int main()
{
    sl::DLSSGOptions options {};
    options.structVersion = sl::kStructVersion3;
    options.mode = sl::DLSSGMode::eOn;
    options.numFramesToGenerate = 3;
    auto observedOptions = MfgHdrUiDiagnostics::ObserveOptions(options);
    assert(observedOptions.seen && observedOptions.validKnownAbi);
    assert(!observedOptions.uiRecompositionKnown);

    options.structVersion = sl::kStructVersion4;
    options.enableUserInterfaceRecomposition = sl::Boolean::eTrue;
    const auto originalMode = options.mode;
    const auto originalFrames = options.numFramesToGenerate;
    const auto originalRecomposition = options.enableUserInterfaceRecomposition;
    observedOptions = MfgHdrUiDiagnostics::ObserveOptions(options);
    assert(observedOptions.uiRecompositionKnown && observedOptions.uiRecompositionEnabled);
    assert(options.mode == originalMode);
    assert(options.numFramesToGenerate == originalFrames);
    assert(options.enableUserInterfaceRecomposition == originalRecomposition);

    options.structVersion = 6;
    observedOptions = MfgHdrUiDiagnostics::ObserveOptions(options);
    assert(!observedOptions.validKnownAbi);
    assert(!observedOptions.uiRecompositionKnown);

    auto back = MakeResource(3440, 1440, 28, 0x1000);
    auto hudless = MakeResource(3440, 1440, 28, 0x2000);
    auto ui = MakeResource(3440, 1440, 28, 0x3000);

    sl::ResourceTag tags[] = {
        { &back, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle::eValidUntilPresent },
        { &hudless, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent },
        { &ui, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent },
    };

    auto* const originalBack = tags[0].resource;
    auto* const originalHudless = tags[1].resource;
    auto* const originalUi = tags[2].resource;
    const auto originalHudlessFormat = hudless.nativeFormat;
    const auto originalUiFormat = ui.nativeFormat;

    auto assessment = MfgHdrUiDiagnostics::AssessTags(
        tags, 3, false, {}, MfgHdrUiDiagnostics::FormatApi::Dxgi);
    assert(tags[0].resource == originalBack);
    assert(tags[1].resource == originalHudless);
    assert(tags[2].resource == originalUi);
    assert(hudless.nativeFormat == originalHudlessFormat);
    assert(ui.nativeFormat == originalUiFormat);
    assert(assessment.relevant);
    assert(assessment.hasCompletePair);
    assert(assessment.structurallyValidForRecomposition);
    assert(assessment.automaticRecompositionProven);
    assert(assessment.issues == MfgHdrUiDiagnostics::None);

    assessment = MfgHdrUiDiagnostics::AssessTags(
        tags, 3, true, {}, MfgHdrUiDiagnostics::FormatApi::Dxgi);
    assert(assessment.hasCompletePair);
    assert(assessment.structurallyValidForRecomposition);
    assert(!assessment.automaticRecompositionProven);
    assert((assessment.issues & MfgHdrUiDiagnostics::HdrTransferUnproven) != 0);

    hudless.width = 2560;
    assessment = MfgHdrUiDiagnostics::AssessTags(
        tags, 3, false, {}, MfgHdrUiDiagnostics::FormatApi::Dxgi);
    assert((assessment.issues & MfgHdrUiDiagnostics::HudlessExtentMismatch) != 0);
    hudless.width = 3440;

    hudless.nativeFormat = 10;
    assessment = MfgHdrUiDiagnostics::AssessTags(
        tags, 3, false, {}, MfgHdrUiDiagnostics::FormatApi::Dxgi);
    assert((assessment.issues & MfgHdrUiDiagnostics::HudlessFormatMismatch) != 0);
    hudless.nativeFormat = 28;

    ui.nativeFormat = 24;
    assessment = MfgHdrUiDiagnostics::AssessTags(
        tags, 3, false, {}, MfgHdrUiDiagnostics::FormatApi::Dxgi);
    assert((assessment.issues & MfgHdrUiDiagnostics::UiColorAlphaLowPrecision) != 0);

    tags[2].resource = nullptr;
    assessment = MfgHdrUiDiagnostics::AssessTags(
        tags, 3, false, {}, MfgHdrUiDiagnostics::FormatApi::Dxgi);
    assert(assessment.uiColorAlpha.cleared);
    assert(!assessment.uiColorAlpha.present);
    assert(!assessment.hasCompletePair);

    std::puts("PASS: HDR/UI diagnostics observe structure without mutation");
    return 0;
}
