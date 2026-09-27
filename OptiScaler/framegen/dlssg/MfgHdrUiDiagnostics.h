/*
 * Read-only Streamline DLSS-G HDR/UI diagnostics.
 * SPDX-License-Identifier: MIT
 *
 * Structural checks are adapted from RenoDX MFGAdaUnlock 1.1.5 quality_guard.hpp.
 * This file never rewrites ResourceTag, DLSSGOptions, Constants, or caller resources.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include <sl_core_types.h>
#include <sl_dlss_g.h>

namespace MfgHdrUiDiagnostics
{
enum class FormatApi : uint32_t
{
    Unknown,
    Dxgi,
    Vulkan
};

enum Issue : uint32_t
{
    None = 0,
    HdrTransferUnproven = 1u << 0,
    InvalidOptionalResource = 1u << 1,
    HudlessExtentMismatch = 1u << 2,
    HudlessFormatMismatch = 1u << 3,
    UiExtentMismatch = 1u << 4,
    UiColorAlphaLowPrecision = 1u << 5,
    OptionalExtensionPresent = 1u << 6,
};

inline constexpr uint32_t StructuralIssueMask = InvalidOptionalResource | HudlessExtentMismatch |
                                                HudlessFormatMismatch | UiExtentMismatch | UiColorAlphaLowPrecision;

enum class AutoRecompositionAction : uint8_t
{
    Preserve,
    Enable,
    DisableUnsafe,
};

struct OutputDescription
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;

    [[nodiscard]] bool HasDimensions() const { return width != 0 && height != 0; }
    [[nodiscard]] bool HasFormat() const { return format != 0; }
};

struct ResourceObservation
{
    bool mentioned = false;
    bool present = false;
    bool cleared = false;
    bool valid = false;
    bool hasExtension = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t resourceWidth = 0;
    uint32_t resourceHeight = 0;
    uint32_t format = 0;
    uint32_t lifecycle = 0;
};

struct BatchObservation
{
    uint32_t issues = None;
    bool relevant = false;
    bool hasCompletePair = false;
    bool structurallyValidForRecomposition = false;
    bool automaticRecompositionProven = false;
    OutputDescription output {};
    ResourceObservation hudless {};
    ResourceObservation uiColorAlpha {};
    ResourceObservation uiAlpha {};
};

struct OptionsObservation
{
    bool seen = false;
    bool validKnownAbi = false;
    size_t structVersion = 0;
    uint32_t mode = 0;
    uint32_t generatedFrames = 0;
    uint32_t flags = 0;
    bool uiRecompositionKnown = false;
    bool uiRecompositionEnabled = false;
    bool dynamicTargetKnown = false;
    float dynamicTargetFps = 0.0f;
};

struct Snapshot
{
    bool seen = false;
    uint64_t tagBatches = 0;
    uint64_t frameAwareBatches = 0;
    uint64_t optionCalls = 0;
    uint64_t changes = 0;
    uint64_t automaticApplications = 0;
    AutoRecompositionAction automaticAction = AutoRecompositionAction::Preserve;

    bool viewportKnown = false;
    uint32_t viewport = 0;
    bool frameKnown = false;
    uint32_t frame = 0;

    bool outputColorSpaceKnown = false;
    bool hdrOutput = false;
    uint32_t dxgiColorSpace = 0;

    uint32_t issues = None;
    bool hasCompletePair = false;
    bool structurallyValidForRecomposition = false;
    bool automaticRecompositionProven = false;

    OutputDescription output {};
    ResourceObservation hudless {};
    ResourceObservation uiColorAlpha {};
    ResourceObservation uiAlpha {};
    OptionsObservation options {};
};

inline constexpr bool LowPrecisionUiAlpha(uint32_t format, FormatApi api)
{
    // DXGI_FORMAT_R10G10B10A2_UNORM has two alpha bits. Reno's guard treats
    // packed 10:10:10:2 UI colour+alpha as unsuitable for the separation mask.
    return api == FormatApi::Dxgi ? format == 24
                                  : api == FormatApi::Vulkan ? (format >= 58 && format <= 69) : false;
}

inline bool IsHudUiType(sl::BufferType type)
{
    return type == sl::kBufferTypeHUDLessColor || type == sl::kBufferTypeUIColorAndAlpha ||
           type == sl::kBufferTypeUIAlpha;
}

inline OutputDescription Describe(const sl::ResourceTag& tag, bool* valid = nullptr, bool* hasExtension = nullptr)
{
    const bool lifecycleValid = tag.lifecycle == sl::ResourceLifecycle::eOnlyValidNow ||
                                tag.lifecycle == sl::ResourceLifecycle::eValidUntilPresent ||
                                tag.lifecycle == sl::ResourceLifecycle::eValidUntilEvaluate;

    bool localValid = tag.structType == sl::ResourceTag::s_structType &&
                      tag.structVersion == sl::kStructVersion1 && lifecycleValid;
    bool extension = tag.next != nullptr;
    OutputDescription result {};

    if (tag.extent.width != 0 && tag.extent.height != 0)
    {
        result.width = tag.extent.width;
        result.height = tag.extent.height;
    }

    if (tag.resource != nullptr)
    {
        extension |= tag.resource->next != nullptr;
        localValid = localValid && tag.resource->structType == sl::Resource::s_structType &&
                     tag.resource->structVersion == sl::kStructVersion1 && tag.resource->native != nullptr;

        if (!result.HasDimensions())
        {
            result.width = tag.resource->width;
            result.height = tag.resource->height;
        }
        result.format = tag.resource->nativeFormat;

        if (tag.extent.width != 0 && tag.extent.height != 0 && tag.resource->width != 0 &&
            tag.resource->height != 0)
        {
            const uint64_t right = static_cast<uint64_t>(tag.extent.left) + tag.extent.width;
            const uint64_t bottom = static_cast<uint64_t>(tag.extent.top) + tag.extent.height;
            localValid = localValid && right <= tag.resource->width && bottom <= tag.resource->height;
        }
    }

    if (valid != nullptr)
        *valid = localValid;
    if (hasExtension != nullptr)
        *hasExtension = extension;
    return result;
}

inline ResourceObservation ObserveResource(const sl::ResourceTag& tag)
{
    ResourceObservation out {};
    out.mentioned = true;
    out.lifecycle = static_cast<uint32_t>(tag.lifecycle);
    out.hasExtension = tag.next != nullptr;

    if (tag.resource == nullptr)
    {
        out.cleared = true;
        out.valid = tag.structType == sl::ResourceTag::s_structType && tag.structVersion == sl::kStructVersion1;
        return out;
    }

    out.present = true;
    bool valid = false;
    bool extension = false;
    const auto desc = Describe(tag, &valid, &extension);
    out.valid = valid;
    out.hasExtension = extension;
    out.width = desc.width;
    out.height = desc.height;
    out.format = desc.format;
    out.resourceWidth = tag.resource->width;
    out.resourceHeight = tag.resource->height;
    return out;
}

inline BatchObservation AssessTags(const sl::ResourceTag* tags, uint32_t count, bool hdr,
                                   const OutputDescription& previousOutput = {},
                                   FormatApi api = FormatApi::Dxgi)
{
    BatchObservation result {};
    if (tags == nullptr || count == 0)
        return result;

    OutputDescription output = previousOutput;

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& tag = tags[i];
        if (tag.structVersion != sl::kStructVersion1 || tag.type != sl::kBufferTypeBackbuffer ||
            tag.resource == nullptr)
            continue;

        bool valid = false;
        const auto current = Describe(tag, &valid);
        if (!valid)
            continue;
        if (current.HasDimensions())
        {
            output.width = current.width;
            output.height = current.height;
        }
        if (current.HasFormat())
            output.format = current.format;
    }

    result.output = output;

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& tag = tags[i];
        if (!IsHudUiType(tag.type))
            continue;

        result.relevant = true;
        auto observation = ObserveResource(tag);
        ResourceObservation* destination = nullptr;
        if (tag.type == sl::kBufferTypeHUDLessColor)
            destination = &result.hudless;
        else if (tag.type == sl::kBufferTypeUIColorAndAlpha)
            destination = &result.uiColorAlpha;
        else
            destination = &result.uiAlpha;
        *destination = observation;

        if (observation.hasExtension)
            result.issues |= OptionalExtensionPresent;
        if (observation.present && !observation.valid)
            result.issues |= InvalidOptionalResource;
        if (!observation.present)
            continue;

        if (observation.width != 0 && observation.height != 0 && output.HasDimensions() &&
            (observation.width != output.width || observation.height != output.height))
        {
            result.issues |= tag.type == sl::kBufferTypeHUDLessColor ? HudlessExtentMismatch : UiExtentMismatch;
        }

        if (tag.type == sl::kBufferTypeHUDLessColor && observation.format != 0 && output.HasFormat() &&
            observation.format != output.format)
            result.issues |= HudlessFormatMismatch;

        if (tag.type == sl::kBufferTypeUIColorAndAlpha && LowPrecisionUiAlpha(observation.format, api))
            result.issues |= UiColorAlphaLowPrecision;
    }

    const bool uiPresent = result.uiColorAlpha.present || result.uiAlpha.present;
    result.hasCompletePair = result.hudless.present && uiPresent;

    result.structurallyValidForRecomposition = result.hasCompletePair && (result.issues & StructuralIssueMask) == 0;

    if (hdr && result.relevant)
        result.issues |= HdrTransferUnproven;

    // This is diagnostic only. "Proven" means the metadata is sufficient for
    // structural SDR compatibility; no runtime option is changed here.
    result.automaticRecompositionProven = !hdr && result.structurallyValidForRecomposition;
    return result;
}

inline AutoRecompositionAction DecideAutomaticRecomposition(bool policyEnabled, const Snapshot& snapshot,
                                                            const sl::DLSSGOptions& options)
{
    // Eligibility comes from the game's original observed ABI, not from a temporary
    // version promotion used by another OptiScaler override such as Dynamic MFG.
    if (!policyEnabled || options.mode == sl::DLSSGMode::eOff || options.structVersion < sl::kStructVersion4 ||
        options.structVersion > sl::kStructVersion5 || !snapshot.options.uiRecompositionKnown ||
        snapshot.tagBatches == 0)
        return AutoRecompositionAction::Preserve;

    const bool requested = options.enableUserInterfaceRecomposition == sl::Boolean::eTrue;
    if ((snapshot.issues & StructuralIssueMask) != 0u)
        return requested ? AutoRecompositionAction::DisableUnsafe : AutoRecompositionAction::Preserve;

    if (!snapshot.outputColorSpaceKnown)
        return AutoRecompositionAction::Preserve;

    if (!snapshot.hdrOutput && snapshot.structurallyValidForRecomposition && !requested)
        return AutoRecompositionAction::Enable;

    // HDR transfer compatibility is not inferable from ResourceTag metadata.
    return AutoRecompositionAction::Preserve;
}

inline constexpr const char* AutoRecompositionActionName(AutoRecompositionAction action)
{
    switch (action)
    {
    case AutoRecompositionAction::Enable:
        return "enable";
    case AutoRecompositionAction::DisableUnsafe:
        return "disable-unsafe";
    default:
        return "preserve";
    }
}

inline OptionsObservation ObserveOptions(const sl::DLSSGOptions& options)
{
    OptionsObservation out {};
    out.seen = true;
    out.structVersion = options.structVersion;
    out.validKnownAbi = options.structVersion >= sl::kStructVersion1 && options.structVersion <= sl::kStructVersion5;
    out.mode = static_cast<uint32_t>(options.mode);
    out.generatedFrames = options.numFramesToGenerate;
    out.flags = static_cast<uint32_t>(options.flags);

    if (options.structVersion >= sl::kStructVersion4 && options.structVersion <= sl::kStructVersion5)
    {
        out.uiRecompositionKnown = true;
        out.uiRecompositionEnabled = options.enableUserInterfaceRecomposition == sl::Boolean::eTrue;
    }

    if (options.structVersion >= sl::kStructVersion5 && options.structVersion <= sl::kStructVersion5)
    {
        out.dynamicTargetKnown = true;
        out.dynamicTargetFps = options.dynamicTargetFrameRate;
    }

    return out;
}
} // namespace MfgHdrUiDiagnostics
