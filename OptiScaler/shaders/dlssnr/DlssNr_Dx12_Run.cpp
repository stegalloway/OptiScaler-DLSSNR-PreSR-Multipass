#include "pch.h"
#include <atomic>
#include <dlssnr/DlssNr_NrStabStatus.h>
#include "DlssNr_Dx12_State.h"

auto DlssNr_Dx12::State::Run(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* colour, ID3D12Resource* depth, ID3D12Resource* motion,
             ID3D12Resource* output, const DlssNrFrameInfo& frame, ID3D12CommandQueue* timingQueue) -> void
{
    std::lock_guard<std::recursive_mutex> nrLock(mutex);
    const Config& cfg = *Config::Instance();
    nr.spatialActive = false;

    if (nr.failed || cmdList == nullptr || colour == nullptr || depth == nullptr || motion == nullptr ||
        output == nullptr)
    {
        ReportSkipOnce(nr.failed ? "it already failed this session" : "a resource was missing");
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(nr.failed ? DlssNr::NrStabUi::FAILED : DlssNr::NrStabUi::INELIGIBLE,
                                 0, false, 0.0f, 0.0f);
        return;
    }

    ID3D12Resource* target = output;

    // Guard creation and dispatch together: either can record GPU work and alter compute bindings.
    const bool restoreRequired =
        cfg.RestoreComputeSignature.value_or_default() || cfg.RestoreGraphicSignature.value_or_default();
    if (restoreRequired && !frame.IndependentCommands && !D3D12Hooks::CanRestoreRootSignature(cmdList))
    {
        ReportSkipOnce("the upscaler could not restore state this frame");
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(nr.failed ? DlssNr::NrStabUi::FAILED : DlssNr::NrStabUi::INELIGIBLE,
                                 0, false, 0.0f, 0.0f);
        return;
    }
    lifetime.Record(cmdList);
    ScopedNrStateEnvelope stateEnvelope(cmdList);

    // A completed upscaler output normally arrives as a UAV. The pre-SR colour input instead arrives
    // readable. Track every transition so both paths return the resource exactly as their caller gave
    // it to us; a pre-SR resource without UAV support is written through a scratch-and-copy fallback.
    const D3D12_RESOURCE_STATES outputArrival =
        frame.PipelineManagedStates ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        : frame.FinishedPicture     ? (D3D12_RESOURCE_STATES) frame.OutputArrivalState
        : frame.BeforeUpscale ? (!frame.PrivateColorCopy && Config::Instance()->ColorResourceBarrier.has_value()
                                     ? (D3D12_RESOURCE_STATES) Config::Instance()->ColorResourceBarrier.value()
                                     : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
        : Config::Instance()->OutputResourceBarrier.has_value()
            ? (D3D12_RESOURCE_STATES) Config::Instance()->OutputResourceBarrier.value()
            : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES targetState = outputArrival;
    const auto TransitionTarget = [&](D3D12_RESOURCE_STATES to)
    {
        Barrier(cmdList, target, targetState, to);
        targetState = to;
    };

    Microsoft::WRL::ComPtr<ID3D12Device> deviceRef;

    if (FAILED(target->GetDevice(IID_PPV_ARGS(&deviceRef))))
    {
        ReportSkipOnce("the output texture belongs to no D3D12 device");
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(nr.failed ? DlssNr::NrStabUi::FAILED : DlssNr::NrStabUi::INELIGIBLE,
                                 0, false, 0.0f, 0.0f);
        return;
    }

    auto* device = deviceRef.Get();
    const D3D12_RESOURCE_DESC desc = target->GetDesc();
    const auto active =
        frame.BeforeUpscale
            ? DlssNr::PreSrColorExtent(desc, frame.RenderSubrectWidth, frame.RenderSubrectHeight)
            : std::optional<DlssNr::ColorExtent> { DlssNr::ColorExtent { (unsigned int) desc.Width, desc.Height } };
    if (!active)
    {
        ReportSkipOnce("the pre-SR active colour size is invalid");
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(nr.failed ? DlssNr::NrStabUi::FAILED : DlssNr::NrStabUi::INELIGIBLE,
                                 0, false, 0.0f, 0.0f);
        return;
    }
    const auto width = active->width;
    const auto height = active->height;
    const bool cropColor = frame.BeforeUpscale && (width != desc.Width || height != desc.Height);
    const bool targetSupportsUav = cropColor || (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;

    const auto guideDesc = depth->GetDesc();
    const auto motionDesc = motion->GetDesc();
    const auto guides = DlssNr::ResolveGuideRegions(
        { (unsigned int) guideDesc.Width, guideDesc.Height },
        { (unsigned int) motionDesc.Width, motionDesc.Height },
        { frame.RenderSubrectWidth, frame.RenderSubrectHeight }, { frame.OutputWidth, frame.OutputHeight },
        frame.MotionVectorsLowResolution, frame.DepthSubrectBaseX, frame.DepthSubrectBaseY,
        frame.MotionSubrectBaseX, frame.MotionSubrectBaseY);
    if (!guides.depth.valid() || !guides.motion.valid())
    {
        ReportSkipOnce("depth or motion-vector subrect is empty");
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(nr.failed ? DlssNr::NrStabUi::FAILED : DlssNr::NrStabUi::INELIGIBLE,
                                 0, false, 0.0f, 0.0f);
        return;
    }
    const auto guideWidth = guides.depth.width, guideHeight = guides.depth.height;
    // Live active motion rectangle after the base's MVLowRes resolution policy.
    const auto motionWidth = guides.motion.width, motionHeight = guides.motion.height;
    const auto motionBaseX = guides.motion.x, motionBaseY = guides.motion.y;

    if (frame.Reset)
    {
        nr.reset = true;
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending)
            nr.stabMvIgnorePending = true;
        if (!nr.stabMvSelfTestPassed)
        {
            nr.stabMvAttempts = 0;
            nr.stabMvMovingFrames = 0;
            nr.stabMvAccumSamples = 0;
            nr.stabMvAccumUnwarped = nr.stabMvAccumPlus = nr.stabMvAccumMinus = 0.0;
            nr.stabSelfTestWaitingMotion = true;
            nr.stabSelfTestWaitFrames = 0;
            nr.stabSelfTestWaitWarned = false;
        }

        ++resets;

        if (resets <= 3 || resets % 100 == 0)
            LOG_INFO("DLSS-NR: the game asked for a history reset ({} so far)", resets);
    }

    // Guide dimensions can change without rebuilding the model; report changes as they occur.

    const GuideReport guidesNow {
        true,  frame.DepthInverted, frame.MvScaleX, frame.MvScaleY, guideWidth, guideHeight,
        width, (unsigned int) height
    };

    if (loggedGuides != guidesNow)
    {
        loggedGuides = guidesNow;
        LOG_INFO("DLSS-NR guides: depth {}, motion vector scale {} x {}, guides {}x{} for a {}x{} frame",
                 frame.DepthInverted ? "inverted" : "not inverted", frame.MvScaleX, frame.MvScaleY,
                 guideWidth, guideHeight, width, height);
    }

    const unsigned int requestedPasses =
        std::clamp(cfg.DlssNrPasses.value_or_default(), 1u,
                   cfg.DlssNrUnlockPasses.value_or_default() ? DlssNr::MaxPassCount : DlssNr::DefaultMaxPassCount);
    for (auto& model : nr.models)
        model.Collect();
    if ((!NVNGXProxy::IsDx12Inited() && !NVNGXProxy::InitDx12(device)) || !DlssNr::Proxy::Context::Available())
    {
        nr.failed = true;
        nr.reason = "the NVIDIA NGX driver does not provide Neural Rendering";
        LOG_ERROR("DLSS-NR unavailable: {}", nr.reason);
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(nr.failed ? DlssNr::NrStabUi::FAILED : DlssNr::NrStabUi::INELIGIBLE,
                                 0, false, 0.0f, 0.0f);
        return;
    }

    // Only the model runs at working resolution; source and composition remain at native size.
    float workScale = cfg.DlssNrWorkingScale.value_or_default();
    if (!std::isfinite(workScale))
        workScale = 1.0f;
    workScale = std::clamp(workScale, 0.25f, 2.0f);
    const auto workWidth = (unsigned int) (width * workScale + 0.5f);
    const auto workHeight = (unsigned int) (height * workScale + 0.5f);
    const bool reduced = workWidth != width || workHeight != height;
    const auto spatialSettings = DlssNr::Spatial::ReadSettings(cfg);
    const auto spatialLayout = DlssNr::Spatial::Build(spatialSettings, width, height, workScale);
    const bool spatialSignatureChanged = nr.spatialSignatureValid &&
        (nr.spatialLayout != spatialLayout || nr.spatialColorFormat != desc.Format ||
         nr.spatialDepthFormat != guideDesc.Format || nr.spatialMotionFormat != motionDesc.Format ||
         nr.spatialDepthW != guideDesc.Width || nr.spatialDepthH != guideDesc.Height ||
         nr.spatialMotionW != motionDesc.Width || nr.spatialMotionH != motionDesc.Height);
    if (spatialSignatureChanged)
    {
        if (nr.spatialLayout.requested || spatialLayout.requested)
        {
            nr.reset = true;
            nr.stabHistoryValid = false;
            nr.stabPrevBaseValid = false;
            if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        }
        nr.spatialFallback = false;
        nr.spatialFallbackReason = "";
    }
    nr.spatialLayout = spatialLayout;
    nr.spatialSignatureValid = true;
    nr.spatialColorFormat = desc.Format;
    nr.spatialDepthFormat = guideDesc.Format;
    nr.spatialMotionFormat = motionDesc.Format;
    nr.spatialDepthW = (unsigned) guideDesc.Width;
    nr.spatialDepthH = guideDesc.Height;
    nr.spatialMotionW = (unsigned) motionDesc.Width;
    nr.spatialMotionH = motionDesc.Height;
    const bool spatial = spatialLayout.active && !nr.spatialFallback;
    if (!spatial && nr.spatialColor)
        ReleaseSpatialResources();
    if (spatial && (!shader.SpatialReady() || !PrepareSpatialResources(device, spatialLayout)))
    {
        nr.spatialFallback = true;
        nr.spatialFallbackReason = "the spatial shader or textures could not be created";
        nr.reset = true;
        modelRunning = false;
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(DlssNr::NrStabUi::INELIGIBLE, 0, false, 0.0f, 0.0f);
        ReportSkipOnce(nr.spatialFallbackReason);
        return;
    }
    const unsigned modelWidth = spatial ? spatialLayout.modelW : workWidth;
    const unsigned modelHeight = spatial ? spatialLayout.modelH : workHeight;
    if (!PrepareRunModels(cmdList, device, frame, desc, { width, height }, { modelWidth, modelHeight },
                          workScale, requestedPasses, spatial))
    {
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(nr.failed ? DlssNr::NrStabUi::FAILED : DlssNr::NrStabUi::INELIGIBLE,
                                 0, false, 0.0f, 0.0f);
        return;
    }
    // The parameter adapter already combined the HDR flag with the active color format.
    const bool isHdrBuffer = frame.ColourIsLinearHdr;

    if (!reportedHdr || reportedHdrValue != isHdrBuffer || reportedBefore != frame.BeforeUpscale)
    {
        reportedHdr = true;
        reportedHdrValue = isHdrBuffer;
        reportedBefore = frame.BeforeUpscale;
        LOG_INFO("DLSS-NR {} SR: the game's DLSS colour space is {} so the colour transform is {}",
                 frame.BeforeUpscale ? "before" : "after", isHdrBuffer ? "linear HDR" : "already tone-mapped",
                 isHdrBuffer ? "on" : "off");
    }

    if (!shader.IsInit())
    {
        nr.failed = true;
        nr.reason = "the colour codec would not compile";
        LOG_ERROR("DLSS-NR unavailable: {}", nr.reason);
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(nr.failed ? DlssNr::NrStabUi::FAILED : DlssNr::NrStabUi::INELIGIBLE,
                                 0, false, 0.0f, 0.0f);
        return;
    }

    // Advance capture scheduling only once the codec and models are ready.
    ++frames;
    lifetime.Collect();
    CheckCaptureTrigger();

    if (captureFrames.isActive())
    {
        const auto captureDir = Util::DllPath().remove_filename() / "dlssnr-capture";
        const auto written = captureFrames.write(captureDir);

        if (!written.empty())
            LOG_INFO("DLSS-NR wrote matched before/after frames to {}", written);
    }

    ResTrack_Dx12::HookLateNrQueue(device);
    if (gpuTime == nullptr)
        gpuTime = std::make_unique<DlssNrGpuTime>(device);

    if (ngxTime == nullptr)
        ngxTime = std::make_unique<DlssNrGpuTime>(device);

    gpuTime->Start(cmdList);

    // Copy just the live image, not the stale right/bottom margins. Do this only after model
    // creation/pending-submission early returns, and inside the measured GPU interval. The compact
    // texture lets every existing codec/compare/hold/capture path use unmodified pixel coordinates.
    ID3D12Resource* const gameColor = target;
    if (cropColor)
    {
        TransitionTarget(D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier(cmdList, nr.activeColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        DlssNr::CopyActiveColor(cmdList, nr.activeColor, gameColor, *active);
        TransitionTarget(outputArrival);
        Barrier(cmdList, nr.activeColor, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        target = nr.activeColor;
        targetState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    const auto FinishColor = [&](bool copyBack)
    {
        if (cropColor)
        {
            if (copyBack)
            {
                TransitionTarget(D3D12_RESOURCE_STATE_COPY_SOURCE);
                Barrier(cmdList, gameColor, outputArrival, D3D12_RESOURCE_STATE_COPY_DEST);
                DlssNr::CopyActiveColor(cmdList, gameColor, target, *active);
                Barrier(cmdList, gameColor, D3D12_RESOURCE_STATE_COPY_DEST, outputArrival);
            }
            TransitionTarget(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        else
        {
            TransitionTarget(outputArrival);
        }
    };

    EncodeContext encoded { cmdList, device, target, targetState, frame, workScale, targetSupportsUav, spatial };
    const bool encodeSucceeded = EncodeInput(encoded);
    targetState = encoded.targetState;
    if (!encodeSucceeded)
    {
        nr.reset = true;
        if (spatial)
        {
            nr.spatialFallback = true;
            nr.spatialFallbackReason = "the spatial frame's colour encode was not recorded";
            modelRunning = false;
        }
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(DlssNr::NrStabUi::INELIGIBLE, 0, false, 0.0f, 0.0f);
        ReportSkipOnce("the current frame's colour encode was not recorded");
        FinishColor(false);
        EndGpuTiming(cmdList);
        return;
    }
    auto* modelInput = encoded.modelInput;

    ID3D12Resource* depthIn = ReadableGuide(device, cmdList, depth, &nr.depthClone);
    ID3D12Resource* motionIn = ReadableGuide(device, cmdList, motion, &nr.motionClone);

    if (depthIn == nullptr || motionIn == nullptr)
    {
        nr.reset = true;
        ReportSkipOnce("the game's depth or motion vectors could not be made readable this frame");
        Barrier(cmdList, nr.colorCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (reduced && !spatial && nr.colorSmall)
            Barrier(cmdList, nr.colorSmall, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (depthIn && depthIn == nr.depthClone)
            Barrier(cmdList, nr.depthClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        if (motionIn && motionIn == nr.motionClone)
            Barrier(cmdList, nr.motionClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        EndGpuTiming(cmdList);
        FinishColor(false);
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(nr.failed ? DlssNr::NrStabUi::FAILED : DlssNr::NrStabUi::INELIGIBLE,
                                 0, false, 0.0f, 0.0f);
        return;
    }
    ID3D12Resource* const originalDepthIn = depthIn;
    ID3D12Resource* const originalMotionIn = motionIn;

    if (spatial)
    {
        const auto colorConstants = DlssNr::Spatial::MakeConstants(
            spatialLayout, 100, guides, frame.MvScaleX, frame.MvScaleY, width, height);
        const auto guideConstants = DlssNr::Spatial::MakeConstants(
            spatialLayout, 101, guides, frame.MvScaleX, frame.MvScaleY, width, height);
        const bool packedColor = shader.DispatchSpatial(cmdList, colorConstants, nr.colorCopy, nullptr, nullptr,
                                                        nr.spatialColor);
        const bool packedGuides = packedColor &&
            shader.DispatchSpatial(cmdList, guideConstants, nr.colorCopy, depthIn, motionIn,
                                   nr.spatialDepth, nr.spatialMotion);
        if (!packedGuides)
        {
            nr.spatialFallback = true;
            nr.spatialFallbackReason = "a spatial packing dispatch failed";
            nr.reset = true;
            modelRunning = false;
            nr.stabHistoryValid = false;
            nr.stabPrevBaseValid = false;
            if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
            DlssNr::NrStabUi::Publish(DlssNr::NrStabUi::INELIGIBLE, 0, false, 0.0f, 0.0f);
            ReportSkipOnce(nr.spatialFallbackReason);
            Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier(cmdList, nr.colorCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            FinishColor(false);
            EndGpuTiming(cmdList);
            if (originalDepthIn == nr.depthClone)
                Barrier(cmdList, nr.depthClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_COPY_DEST);
            if (originalMotionIn == nr.motionClone)
                Barrier(cmdList, nr.motionClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_COPY_DEST);
            return;
        }
        for (auto* packed : { nr.spatialColor, nr.spatialDepth, nr.spatialMotion })
            Barrier(cmdList, packed, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        modelInput = nr.spatialColor;
        depthIn = nr.spatialDepth;
        motionIn = nr.spatialMotion;
    }

    // The vectors were scaled to full-frame pixels; the image the model reprojects is the working size.
    const float mvToWorkX = width != 0 ? (float) workWidth / (float) width : 1.0f;
    const float mvToWorkY = height != 0 ? (float) workHeight / (float) height : 1.0f;

    // Below native (and not packed by spatial compression, which brings its own guides), the model
    // was handed a colour at the working size and depth and motion at the frame's size, with only
    // the vector magnitudes rescaled. Nothing says the model resamples a guide that is larger than
    // its colour, and the picture said it does not: every scale below 100% flickered and settled for
    // frames after the camera stopped, while the same model at 100% of a frame the game had already
    // shrunk was steady. So give it guides at its own size -- the same point resample the DLSS
    // enlargement path already builds for its private upscaler -- and describe them as a full,
    // zero-origin region. The vectors keep the game's units; the working-size scale below still
    // applies.
    bool matchedGuides = false;
    if (reduced && !spatial && workWidth < width && cfg.DlssNrMatchGuides.value_or_default())
    {
        if (nr.depthSmall == nullptr)
            nr.depthSmall = CreateScratch(device, DXGI_FORMAT_R32_FLOAT, workWidth, workHeight);
        if (nr.motionSmall == nullptr)
            nr.motionSmall = CreateScratch(device, DXGI_FORMAT_R32G32_FLOAT, workWidth, workHeight);
        if (nr.depthSmall != nullptr && nr.motionSmall != nullptr)
        {
            DlssNrConstants resize {};
            resize.Mode = DlssNrMode_ResizePrivateGuides;
            resize.Width = workWidth;
            resize.Height = workHeight;
            resize.GuideWidth = guides.depth.width;
            resize.GuideHeight = guides.depth.height;
            resize.DebugView = guides.depth.x;
            resize.CompareMode = guides.depth.y;
            resize.TransferStrength = float(guides.motion.width);
            resize.ColourStrength = float(guides.motion.height);
            resize.CompareSwap = guides.motion.x;
            resize.Transfer = guides.motion.y;
            resize.MvScaleX = resize.MvScaleY = 1.0f;
            if (shader.DispatchPass(cmdList, resize, depthIn, motionIn, nullptr, nullptr, nullptr, nr.depthSmall,
                                    nr.motionSmall))
            {
                Barrier(cmdList, nr.depthSmall, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Barrier(cmdList, nr.motionSmall, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                depthIn = nr.depthSmall;
                motionIn = nr.motionSmall;
                matchedGuides = true;
            }
        }
        static bool loggedMatch = false;
        if (!loggedMatch)
        {
            loggedMatch = true;
            if (matchedGuides)
                LOG_INFO("DLSS-NR guides matched to the working size: depth and motion {}x{} for a {}x{} model "
                         "(the frame's guides are {}x{})",
                         workWidth, workHeight, workWidth, workHeight, guides.depth.width, guides.depth.height);
            else
                LOG_WARN("DLSS-NR guides could not be matched to the working size; the model keeps the frame's "
                         "{}x{} guides for its {}x{} colour",
                         guides.depth.width, guides.depth.height, workWidth, workHeight);
        }
    }

    ngxTime->Start(cmdList);

    // Count only a contiguous set of ready, separate feature histories. A failed extra creation never
    // falls back to reusing the main feature: that tells one temporal model several frames elapsed in
    // one game frame and makes its history fight the later layers.
    unsigned int effectivePasses = 1;
    if (nr.passScratch != nullptr)
    {
        for (unsigned int pass = 1; pass < requestedPasses; ++pass)
        {
            if (!nr.models[pass].Ready(frame.SubmissionEpoch))
                break;
            ++effectivePasses;
        }
    }

    if (loggedConfigured != requestedPasses || loggedEffective != effectivePasses)
    {
        loggedConfigured = requestedPasses;
        loggedEffective = effectivePasses;
        LOG_INFO("DLSS-NR model passes: configured {}, effective {}", requestedPasses, effectivePasses);
    }

    // Keep the encoded base immutable; ping-pong model outputs and compose the final delta once.
    ID3D12Resource* passInput = modelInput;
    ID3D12Resource* passOutput = nr.output;
    ID3D12Resource* finalAnswer = nullptr;
    bool outputReadable = false;
    bool scratchReadable = false;
    bool clampReadable = false;
    bool clampFailed = false;
    uint32_t clampSlots[2] = { UINT32_MAX, UINT32_MAX };

    const auto MakeModelReadable = [&](ID3D12Resource* resource)
    {
        bool& readable = resource == nr.output      ? outputReadable
                         : resource == nr.passClamp ? clampReadable
                                                    : scratchReadable;
        if (!readable)
        {
            Barrier(cmdList, resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            readable = true;
        }
    };

    const auto MakeModelWritable = [&](ID3D12Resource* resource)
    {
        bool& readable = resource == nr.output      ? outputReadable
                         : resource == nr.passClamp ? clampReadable
                                                    : scratchReadable;
        if (readable)
        {
            Barrier(cmdList, resource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            readable = false;
        }
    };

    int result = NVSDK_NGX_Result_Success;
    const bool enlargementReset = nr.reset;
    bool compositionSucceeded = false;

    DlssNr::Proxy::Frame modelFrame {};
    modelFrame.depth = depthIn;
    modelFrame.motion = motionIn;
    modelFrame.size = { modelWidth, modelHeight };
    modelFrame.guides = (spatial || matchedGuides)
                            ? DlssNr::GuideRegions { { 0, 0, modelWidth, modelHeight }, { 0, 0, modelWidth, modelHeight } }
                            : guides;
    modelFrame.depthInverted = frame.DepthInverted;
    modelFrame.reset = nr.reset;
    modelFrame.mvScaleX = spatial ? 1.0f : frame.MvScaleX * mvToWorkX;
    modelFrame.mvScaleY = spatial ? 1.0f : frame.MvScaleY * mvToWorkY;

    for (unsigned int pass = 0; pass < effectivePasses && result == NVSDK_NGX_Result_Success; ++pass)
    {
        MakeModelWritable(passOutput);
        bool evaluated = false;
        modelFrame.color = passInput;
        modelFrame.output = passOutput;
        result = static_cast<int>(nr.models[pass].Run(cmdList, device, modelFrame, PassSettings(cfg, pass),
                                                     frame.SubmissionEpoch, &evaluated));
        modelRunning = evaluated && result == NVSDK_NGX_Result_Success;
        if (!evaluated || result != NVSDK_NGX_Result_Success)
            break;

        finalAnswer = passOutput;
        MakeModelReadable(finalAnswer);

        if (pass + 1 < effectivePasses)
        {
            MakeModelWritable(nr.passClamp);
            DlssNrConstants clamp {};
            clamp.Mode = DlssNrMode_ClampProxy;
            clamp.Width = modelWidth;
            clamp.Height = modelHeight;
            if (!shader.DispatchPass(cmdList, clamp, finalAnswer, nullptr, nullptr, nullptr, nullptr, nr.passClamp,
                                     nullptr, &clampSlots[pass % 2]))
            {
                // Keep this frame's last valid answer; later histories skipped a frame.
                clampFailed = true;
                effectivePasses = pass + 1;
                break;
            }
            MakeModelReadable(nr.passClamp);
            passInput = nr.passClamp;
            passOutput = passOutput == nr.output ? nr.passScratch : nr.output;
        }
    }

    ngxTime->End(cmdList);

    nr.reset = clampFailed || finalAnswer == nullptr;
    bool spatialUnpacked = false;
    ID3D12Resource* ordinaryProxy = modelInput;
    ID3D12Resource* ordinaryAnswer = finalAnswer;
    if (spatial && result == NVSDK_NGX_Result_Success && finalAnswer)
    {
        const auto unpackConstants = DlssNr::Spatial::MakeConstants(
            spatialLayout, 102, guides, frame.MvScaleX, frame.MvScaleY, width, height);
        spatialUnpacked = shader.DispatchSpatial(cmdList, unpackConstants, modelInput, finalAnswer, nullptr,
                                                 nr.spatialProxy, nr.spatialAnswer);
        if (spatialUnpacked)
        {
            for (auto* unpacked : { nr.spatialProxy, nr.spatialAnswer })
                Barrier(cmdList, unpacked, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ordinaryProxy = nr.spatialProxy;
            ordinaryAnswer = nr.spatialAnswer;
        }
        else
        {
            nr.spatialFallback = true;
            nr.spatialFallbackReason = "a spatial unpacking dispatch failed";
            nr.reset = true;
            modelRunning = false;
            ReportSkipOnce(nr.spatialFallbackReason);
            finalAnswer = nullptr;
        }
    }

    // Supersampling probe: report the model working ABOVE native so a test log tells us whether NGX even
    // accepts a super-native evaluate and what it returns. Once per working-size change, or on any error.
    if (modelWidth > width || modelHeight > height)
    {

        if (lastSuper != modelWidth || result != 1)
        {
            lastSuper = modelWidth;
            LOG_INFO("DLSS-NR SUPERSAMPLE: model at {}x{} = {:.2f}x native {}x{}, evaluate result {} ({})",
                     modelWidth, modelHeight, (float) modelWidth / (float) width, width, height, result,
                     NgxResultName((unsigned int) result));
        }
    }

    if (result == NVSDK_NGX_Result_Success && finalAnswer != nullptr)
    {
        // Resolve takes the difference between what the model returned and what it was shown, and adds
        // that back to the frame. At strength zero the result is what the upscaler produced, exactly, and
        // anything the model left alone is untouched rather than round-tripped through the curve.
        auto resolveParams = MakeResolveConstants(encoded, effectivePasses);

        // For spatial supersampling, both halves of the pair use the same filter. A failed paired
        // downsample skips composition this frame so a mismatched proxy cannot create an edit.
        bool superDownOk = false;
        bool spatialDownFailed = false;
        if (spatial && workScale > 1.0f)
        {
            const Scaler scaler = cfg.DlssNrScalingDownscaler.value_or_default();
            if (nr.nrScaler != scaler)
            {
                ReleaseSupersamplers();
                nr.nrScaler = scaler;
            }
            if (nr.superDown == nullptr)
                nr.superDown = new OS_Dx12("DLSS-NR supersample down", device, false, scaler);
            if (nr.spatialProxyDown == nullptr)
                nr.spatialProxyDown = new OS_Dx12("DLSS-NR spatial proxy down", device, false, scaler);
        }
        if (spatial && workScale > 1.0f)
        {
            const bool proxyDown = nr.superDown && nr.spatialProxyDown &&
                nr.spatialProxyNative && nr.spatialAnswerNative &&
                nr.spatialProxyDown->DispatchResources(cmdList, ordinaryProxy, nr.spatialProxyNative);
            const bool answerDown = proxyDown && nr.superDown->DispatchResources(
                cmdList, ordinaryAnswer, nr.spatialAnswerNative);
            if (answerDown)
            {
                for (auto* nativePair : { nr.spatialProxyNative, nr.spatialAnswerNative })
                    Barrier(cmdList, nativePair, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                superDownOk = true;
            }
            else
            {
                spatialDownFailed = true;
                nr.spatialFallback = true;
                nr.spatialFallbackReason = "a spatial supersample downsampling dispatch failed";
                nr.reset = true;
                modelRunning = false;
                ReportSkipOnce(nr.spatialFallbackReason);
            }
        }
        else if (!spatial && workScale > 1.0f && nr.superDown != nullptr && nr.outputNative != nullptr &&
                 nr.superDown->DispatchResources(cmdList, ordinaryAnswer, nr.outputNative))
        {
            Barrier(cmdList, nr.outputNative, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            superDownOk = true;
        }

        ID3D12Resource* resolveProxy = superDownOk
            ? (spatial ? nr.spatialProxyNative : nr.colorCopy) : ordinaryProxy;
        ID3D12Resource* resolveAnswer = superDownOk
            ? (spatial ? nr.spatialAnswerNative : nr.outputNative) : ordinaryAnswer;
        bool enlargementReady = !spatialDownFailed;
        const auto transfer = cfg.DlssNrTransfer.value_or_default();
        bool resizeFieldReadable = false;
        if (resolveParams.DebugView != 4 && !spatialDownFailed && DlssNrUsesDlssEnlargement(transfer) && reduced &&
            (transfer == 2 || workScale < 1.0f))
        {
            auto* enlarged = EnlargeMatchedResidual(cmdList, device, ordinaryProxy, ordinaryAnswer,
                                                    originalDepthIn, originalMotionIn,
                                                    frame, resolveParams, enlargementReset, timingQueue);
            enlargementReady = enlarged != nullptr;
            if (enlarged) { resolveAnswer = enlarged; resolveParams.Transfer = transfer; }
            if (enlarged && transfer == 4)
            {
                // Retain the spatial field for the inverse-HDR range guard.
                resolveProxy = enlarger->input.Get();
                Barrier(cmdList, resolveProxy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                resizeFieldReadable = true;
            }
            if (enlarged && (resolveParams.DebugView == 2 || (transfer == 4 && resolveParams.DebugView == 1)))
            {
                resolveProxy = ordinaryProxy; resolveAnswer = ordinaryAnswer;
                resolveParams.Transfer = DlssNrSpatialTransfer(transfer); // Inspect the actual model pair.
            }
        }
        else
        {
            ReleaseEnlarger();
            enlargementStatus.clear();
        }

        // Display the immutable packed model input; ordinary proxy display handles the scaling.
        if (resolveParams.DebugView == 4)
        {
            resolveProxy = modelInput;
            resolveParams.DebugView = 1;
        }

        // Resolve pre-SR inputs without UAV support through an owned scratch and copy-back.
        ID3D12Resource* resolveOriginal = targetSupportsUav ? nr.hdrCopy : target;
        ID3D12Resource* resolveTarget = targetSupportsUav ? target : nr.hdrCopy;

        bool nrStabHdrCopyWritable = false;

        if (targetSupportsUav)
        {
            TransitionTarget(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        if (!nr.stabAnnounced)
        {
            nr.stabAnnounced = true;
            LOG_INFO("[NRSTAB] checkpoint=d3fcc16f integration=V085_NRSTAB_SHADOWFLOOR_DX12 "
                     "mode=LIVE_AB variance_select=1 ui_controls=1 ab_toggle=1 "
                     "two_knobs=NRStabilizerK,NRStabilizerMotionRejectPx binary_selection=1 no_blend=1 "
                     "steady_state_readback=0 startup_mv_selftest=1 submitted_completion=1 sealed_recording=1");
        }

        bool resolved = false;
        bool nrStabFrameActive = false;
        const auto nrStabGame = Util::ExePath().filename().string();
        const bool nrStabEnabled = cfg.DlssNrStabilizerEnabled.value_or_default();
        const unsigned int nrStabEligibilityOutputWidth = frame.OutputWidth != 0u ? frame.OutputWidth : width;

        const unsigned int nrStabEligibilityOutputHeight = frame.OutputHeight != 0u ? frame.OutputHeight : height;

        const unsigned int nrStabMinMotionDimension =
            std::clamp(cfg.DlssNrStabilizerMinMotionDimension.value_or_default(), 1u, 4096u);

        const float nrStabMotionAspectTolerance =
            std::clamp(cfg.DlssNrStabilizerMotionAspectTolerance.value_or_default(), 1.0f, 4.0f);

        // MotionVectorsLowResolution determines the expected semantic MV
        // domain. This derivation trusts frame.MotionVectorsLowResolution.
        // If that flag is itself transitional during feature recreation, the
        // expected dimensions may be incorrect. The independent size/aspect
        // checks are defence in depth, not a substitute for coherent metadata.
        const bool nrStabMotionLowResolution = frame.MotionVectorsLowResolution;

        const unsigned int nrStabExpectedMotionWidth =
            nrStabMotionLowResolution ? (frame.RenderSubrectWidth != 0u ? frame.RenderSubrectWidth : width)
                                      : nrStabEligibilityOutputWidth;

        const unsigned int nrStabExpectedMotionHeight =
            nrStabMotionLowResolution ? (frame.RenderSubrectHeight != 0u ? frame.RenderSubrectHeight : height)
                                      : nrStabEligibilityOutputHeight;

        const char* nrStabMotionDomain = nrStabMotionLowResolution ? "RENDER" : "OUTPUT";

        const float nrStabExpectedMotionAspect =
            nrStabExpectedMotionHeight != 0u ? (float) nrStabExpectedMotionWidth / (float) nrStabExpectedMotionHeight
                                             : 0.0f;

        const float nrStabActualMotionAspect = motionHeight != 0u ? (float) motionWidth / (float) motionHeight : 0.0f;

        const float nrStabMotionAspectRatio =
            nrStabExpectedMotionAspect > 0.0f ? nrStabActualMotionAspect / nrStabExpectedMotionAspect : 0.0f;

        const bool nrStabMotionDimensionSane =
            motionWidth >= nrStabMinMotionDimension && motionHeight >= nrStabMinMotionDimension;

        const bool nrStabMotionAspectSane = nrStabMotionAspectRatio >= (1.0f / nrStabMotionAspectTolerance) &&
                                            nrStabMotionAspectRatio <= nrStabMotionAspectTolerance;

        const bool nrStabMotionShapeSane = nrStabMotionDimensionSane && nrStabMotionAspectSane;

        // Rejected guides interrupt the consecutive-frame startup MV diagnostic,
        // even if the game does not request a DLSS history reset. Keep a
        // validated convention and the process-session cache untouched.
        if (!nrStabMotionShapeSane && !nr.stabMvSelfTestPassed)
        {
            nr.stabPrevBaseValid = false;
            if (nr.stabMvReadbackPending)
                nr.stabMvIgnorePending = true;
        }

        const char* nrStabGuideRejectReason = !nrStabMotionDimensionSane ? "min_dimension"
                                              : !nrStabMotionAspectSane  ? "aspect"
                                                                         : "none";

        // Always recompute geometry from the live feature. Session cache
        // inheritance restores only the +/- convention.
        const float nrStabLiveMotionToOutputX =
            motionWidth != 0u ? (float) nrStabEligibilityOutputWidth / (float) motionWidth : 0.0f;

        const float nrStabLiveMotionToOutputY =
            motionHeight != 0u ? (float) nrStabEligibilityOutputHeight / (float) motionHeight : 0.0f;
        const bool nrStabEligible =
            nrStabEnabled && enlargementReady && nr.hdrCopy != nullptr &&
            !cfg.DlssNrResidualAcrossRr.value_or_default() && !cfg.DlssNrDeferredDlss.value_or_default() &&
            !frame.FinishedPicture && resolveParams.DebugView == 0 && resolveParams.CompareMode == 0 &&
            resolveParams.ApplyModel != 0 && motionWidth != 0u && motionHeight != 0u && nrStabMotionShapeSane;
        const float nrStabK = std::clamp(cfg.DlssNrStabilizerK.value_or_default(), 0.50f, 1.50f);
        const float nrStabMotionRejectPx =
            std::clamp(cfg.DlssNrStabilizerMotionRejectPx.value_or_default(), 0.50f, 3.00f);
        const unsigned int nrStabOutputWidth = frame.OutputWidth != 0u ? frame.OutputWidth : width;
        const unsigned int nrStabOutputHeight = frame.OutputHeight != 0u ? frame.OutputHeight : height;

        // Use the owned untouched-copy format rather than the game's resource
        // descriptor. This is important for pre-SR resources that are not UAV
        // resources themselves: hdrCopy is already known to be UAV-compatible.
        const DXGI_FORMAT nrStabFormat = nr.hdrCopy != nullptr ? nr.hdrCopy->GetDesc().Format : desc.Format;
        if (!nr.stabControlInitialized)
        {
            nr.stabControlInitialized = true;
            nr.stabAppliedEnabled = nrStabEnabled;
            nr.stabAppliedK = nrStabK;
            nr.stabAppliedMotionRejectPx = nrStabMotionRejectPx;
        }
        else if (nr.stabAppliedEnabled != nrStabEnabled || nr.stabAppliedK != nrStabK ||
                 nr.stabAppliedMotionRejectPx != nrStabMotionRejectPx)
        {
            const bool abChanged = nr.stabAppliedEnabled != nrStabEnabled;
            nr.stabHistoryValid = false;
            nr.stabFailureLogged = false;
            LOG_INFO("[NRSTAB] CONTROL game={} ab_state={} k={} motion_reject_px={} history_reset=1 mv_selftest={} "
                     "mv_direction={} raw_nr_when_bypassed=1",
                     nrStabGame, nrStabEnabled ? "ACTIVE" : "BYPASSED", nrStabK, nrStabMotionRejectPx,
                     nr.stabMvSelfTestPassed   ? "PASS"
                     : nr.stabMvSelfTestFailed ? "FAIL"
                                               : "PENDING",
                     nr.stabMvSign > 0.0f   ? "+1"
                     : nr.stabMvSign < 0.0f ? "-1"
                                            : "0");
            if (abChanged)
                LOG_INFO("[NRSTAB] A/B {} game={} history_reset=1", nrStabEnabled ? "ACTIVE" : "BYPASSED", nrStabGame);
            nr.stabAppliedEnabled = nrStabEnabled;
            nr.stabAppliedK = nrStabK;
            nr.stabAppliedMotionRejectPx = nrStabMotionRejectPx;
        }
        static constexpr unsigned kMvGrid = 64u;
        static constexpr unsigned kMvReadbackBytes = kMvGrid * kMvGrid * 16u;
        // A validated +/- MV convention belongs to the running game's
        // motion encoding, not to one DLSS quality-mode feature instance.
        // Keep it for this process so Quality/Balanced/Performance feature
        // recreation does not need another startup readback test.
        static constexpr uint32_t kNrStabMvValid = 1u << 0;
        static constexpr uint32_t kNrStabMvDirectionMinus = 1u << 1;
        static constexpr uint32_t kNrStabMvScaleXMinus = 1u << 2;
        static constexpr uint32_t kNrStabMvScaleYMinus = 1u << 3;
        static constexpr uint32_t kNrStabMvConflict = 1u << 31;

        // One coherent process-session convention word.
        //
        // CONFLICT is sticky for the lifetime of the process. It means two
        // independently validated NR instances disagreed. A conflicted cache
        // is never inherited and is never cleared by a later successful
        // validation; every subsequent instance runs its own startup test.
        static std::atomic<uint32_t> nrStabSessionMvConvention { 0u };

        const auto PackNrStabMvConvention = [&](float stabilizerSign) -> uint32_t
        {
            uint32_t packed = kNrStabMvValid;

            if (stabilizerSign < 0.0f)
                packed |= kNrStabMvDirectionMinus;

            if (frame.MvScaleX < 0.0f)
                packed |= kNrStabMvScaleXMinus;

            if (frame.MvScaleY < 0.0f)
                packed |= kNrStabMvScaleYMinus;

            return packed;
        };

        const auto PublishNrStabMvConvention = [&](uint32_t candidate)
        {
            const uint32_t current = nrStabSessionMvConvention.load(std::memory_order_acquire);

            // Load before CAS so an already-conflicted cache is distinct from
            // an ordinary CAS failure against another valid convention.
            if ((current & kNrStabMvConflict) != 0u)
                return;

            uint32_t expected = 0u;

            if (nrStabSessionMvConvention.compare_exchange_strong(expected, candidate, std::memory_order_acq_rel,
                                                                  std::memory_order_acquire))
            {
                return;
            }

            if (expected == candidate)
                return;

            if ((expected & kNrStabMvConflict) != 0u)
                return;

            const uint32_t previous = nrStabSessionMvConvention.fetch_or(kNrStabMvConflict, std::memory_order_acq_rel);

            if ((previous & kNrStabMvConflict) == 0u)
            {
                LOG_ERROR("[NRSTAB] MV SESSION CACHE CONFLICT "
                          "existing=0x{:08X} candidate=0x{:08X}; "
                          "cache inheritance disabled for remainder of process",
                          previous, candidate);
            }
        };

        // Placement is part of temporal identity. A history generated after SR
        // must never be consumed by a pre-SR frame, even if the two rasters
        // happen to have identical dimensions.
        if (!nr.stabPlacementInitialized || nr.stabBeforeUpscale != frame.BeforeUpscale)
        {
            const bool hadPlacement = nr.stabPlacementInitialized;
            const bool previousBeforeUpscale = nr.stabBeforeUpscale;

            nr.stabPlacementInitialized = true;
            nr.stabBeforeUpscale = frame.BeforeUpscale;
            nr.stabHistoryValid = false;
            nr.stabPrevBaseValid = false;
            nr.stabPathReady = false;
            nr.stabFailureLogged = false;

            if (nr.stabMvReadbackPending)
                nr.stabMvIgnorePending = true;

            // A confirmed sign is a property of the game's MV convention and
            // survives placement changes. An unfinished/failed vote does not.
            if (!nr.stabMvSelfTestPassed)
            {
                nr.stabMvSelfTestFailed = false;
                nr.stabMvAttempts = 0;
                nr.stabMvMovingFrames = 0;
                nr.stabMvAccumSamples = 0;
                nr.stabMvAccumUnwarped = 0.0;
                nr.stabMvAccumPlus = 0.0;
                nr.stabMvAccumMinus = 0.0;
                nr.stabMvSign = 0.0f;
                nr.stabSelfTestWaitingMotion = true;
                nr.stabSelfTestWaitFrames = 0;
                nr.stabSelfTestWaitWarned = false;
            }

            if (hadPlacement)
            {
                LOG_INFO("[NRSTAB] placement changed {} -> {}; temporal history reset; mv_direction={}",
                         previousBeforeUpscale ? "PRE_SR" : "POST_SR", frame.BeforeUpscale ? "PRE_SR" : "POST_SR",
                         nr.stabMvSelfTestPassed ? (nr.stabMvSign > 0.0f ? "+1" : "-1") : "UNSET");
            }
        }

        const auto NrStabShape = [&](ID3D12Resource* r, DXGI_FORMAT format, unsigned w, unsigned h)
        {
            if (!r)
                return false;
            const auto d = r->GetDesc();
            return d.Width == w && d.Height == h && d.Format == format;
        };

        const auto EnsureNrStabResources = [&]() -> bool
        {
            const bool shapeChanged =
                (nr.stabResolved && !NrStabShape(nr.stabResolved, nrStabFormat, width, height)) ||
                (nr.stabHistory[0] && !NrStabShape(nr.stabHistory[0], DXGI_FORMAT_R32G32B32A32_FLOAT, width, height)) ||
                (nr.stabHistory[1] && !NrStabShape(nr.stabHistory[1], DXGI_FORMAT_R32G32B32A32_FLOAT, width, height)) ||
                (nr.stabPrevBase && !NrStabShape(nr.stabPrevBase, nrStabFormat, width, height));
            if (shapeChanged)
            {
                for (ID3D12Resource** r :
                     { &nr.stabResolved, &nr.stabHistory[0], &nr.stabHistory[1], &nr.stabPrevBase,
                       &nr.stabMvDiag, &nr.stabMvReadback })
                    ParkNrResource(*r);
                nr.stabHistoryReadable[0] = nr.stabHistoryReadable[1] = false;
                nr.stabHistoryIndex = 0;
                nr.stabHistoryValid = false;
                nr.stabPrevBaseReadable = false;
                nr.stabPrevBaseValid = false;
                nr.stabMvReadbackPending = false;
                nr.stabMvCompletion = {};
                nr.stabMvIgnorePending = false;
                nr.stabMvAccumSamples = 0;
                nr.stabMvAccumUnwarped = nr.stabMvAccumPlus = nr.stabMvAccumMinus = 0.0;
                if (!nr.stabMvSelfTestPassed)
                {
                    nr.stabMvMovingFrames = 0;
                    nr.stabMvSign = 0.0f;
                }
                LOG_INFO("[NRSTAB] output shape changed; temporal resources/history reset to {}x{}", width, height);
            }
            if (!nr.stabResolved)
                nr.stabResolved = CreateScratch(device, nrStabFormat, width, height);
            for (auto& h : nr.stabHistory)
                if (!h)
                    h = CreateScratch(device, DXGI_FORMAT_R32G32B32A32_FLOAT, width, height);
            if (!nr.stabPrevBase)
                nr.stabPrevBase = CreateScratch(device, nrStabFormat, width, height);
            if (!nr.stabMvDiag)
                nr.stabMvDiag = CreateScratch(device, DXGI_FORMAT_R32G32B32A32_FLOAT, kMvGrid, kMvGrid);
            if (!nr.stabMvReadback)
            {
                D3D12_HEAP_PROPERTIES heap {};
                heap.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC rd {};
                rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                rd.Width = kMvReadbackBytes;
                rd.Height = 1;
                rd.DepthOrArraySize = 1;
                rd.MipLevels = 1;
                rd.Format = DXGI_FORMAT_UNKNOWN;
                rd.SampleDesc.Count = 1;
                rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                           IID_PPV_ARGS(&nr.stabMvReadback))))
                    nr.stabMvReadback = nullptr;
            }
            return nr.stabResolved && nr.stabHistory[0] && nr.stabHistory[1] && nr.stabPrevBase && nr.stabMvDiag &&
                   nr.stabMvReadback;
        };

        const auto StoreCurrentBase = [&]() -> bool
        {
            if (!nr.stabPrevBase)
                return false;
            Barrier(cmdList, nr.stabPrevBase,
                    nr.stabPrevBaseReadable ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
                                            : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COPY_DEST);
            Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmdList->CopyResource(nr.stabPrevBase, nr.hdrCopy);
            Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Barrier(cmdList, nr.stabPrevBase, D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            nr.stabPrevBaseReadable = true;
            nr.stabPrevBaseValid = true;
            return true;
        };

        const auto ConsumeMvSelfTest = [&]()
        {
            if (!nr.stabMvReadbackPending || !nr.stabMvReadback)
                return;
            using Completion = DlssNr::GpuLifetime::ReadbackState;
            const auto completed = nr.stabMvCompletion ? nr.stabMvCompletion() : Completion::Discarded;
            if (nr.stabMvIgnorePending || completed == Completion::Discarded || completed == Completion::Failed)
            {
                nr.stabMvReadbackPending = false;
                nr.stabMvIgnorePending = false;
                nr.stabMvCompletion = {};
                nr.stabPrevBaseValid = false;
                ParkNrResource(nr.stabMvDiag);
                ParkNrResource(nr.stabMvReadback);
                LOG_INFO("[NRSTAB] MV SELFTEST discarded: reset, discarded recording or failed completion");
                return;
            }
            if (completed != Completion::Complete)
                return;
            // The exact recording is sealed and all of its queue submissions completed.
            // This diagnostic allocation is never reused by a subsequent recording.
            void* mapped = nullptr;
            D3D12_RANGE range { 0, kMvReadbackBytes };
            if (FAILED(nr.stabMvReadback->Map(0, &range, &mapped)) || !mapped)
                return;
            const float* p = (const float*) mapped;
            double e0 = 0.0, ep = 0.0, em = 0.0;
            unsigned valid = 0;
            for (unsigned i = 0; i < kMvGrid * kMvGrid; ++i)
            {
                const float a = p[i * 4 + 3];
                if (a > 0.5f && std::isfinite(p[i * 4]) && std::isfinite(p[i * 4 + 1]) && std::isfinite(p[i * 4 + 2]))
                {
                    e0 += p[i * 4];
                    ep += p[i * 4 + 1];
                    em += p[i * 4 + 2];
                    ++valid;
                }
            }
            D3D12_RANGE none { 0, 0 };
            nr.stabMvReadback->Unmap(0, &none);
            nr.stabMvReadbackPending = false;
            if (nr.stabMvIgnorePending)
            {
                nr.stabMvIgnorePending = false;
                nr.stabPrevBaseValid = false;
                LOG_INFO("[NRSTAB] MV SELFTEST discarded reason=frame_reset");
                return;
            }
            static constexpr unsigned kMvMinMovingSamples = 128u;
            static constexpr unsigned kMvRequiredMovingFrames = 3u;
            static constexpr unsigned kMvMaxMovingFrames = 10u;
            static constexpr float kMvMinMotionPx = 0.75f;
            nr.stabMvValidSamples = valid;

            // A diagnostic frame with zero useful moving pixels says only "keep waiting". It does not
            // consume one of the convention attempts. Once ANY moving samples arrive, however, that
            // readback is a real attempt even if it does not reach the 128-sample decisiveness floor.
            // This separates "the MV path saw no motion" from "the MV path saw motion, but this sample
            // was too sparse to establish the sign".
            if (valid == 0u || e0 <= 1e-8)
            {
                nr.stabPrevBaseValid = false;
                nr.stabSelfTestWaitingMotion = true;
                LOG_INFO("[NRSTAB] MV SELFTEST WAIT reason=no_moving_samples samples={} min_motion_px={} attempts={} "
                         "moving_frames_collected={}/{} selftest_dispatches={}",
                         valid, kMvMinMotionPx, nr.stabMvAttempts, nr.stabMvMovingFrames, kMvRequiredMovingFrames,
                         nr.stabMvSelfTestDispatches);
                return;
            }

            ++nr.stabMvAttempts;
            nr.stabSelfTestWaitingMotion = false;
            nr.stabSelfTestWaitFrames = 0;
            nr.stabSelfTestWaitWarned = false;
            const bool enoughSamples = valid >= kMvMinMovingSamples;

            // Sparse motion is a real diagnostic attempt, but it is not admitted to the sign vote.
            // This preserves the distinction between "no motion" and "some motion, insufficient evidence"
            // without letting a 60-sample frame masquerade as one of the three sufficient-motion frames.
            if (!enoughSamples)
            {
                nr.stabPrevBaseValid = false;
                LOG_INFO("[NRSTAB] MV SELFTEST INCONCLUSIVE attempt={} samples={} enough_samples=0 min_samples={} "
                         "moving_selftest_frames={} selftest_dispatches={}",
                         nr.stabMvAttempts, valid, kMvMinMovingSamples, nr.stabMvMovingFrames,
                         nr.stabMvSelfTestDispatches);
                return;
            }

            ++nr.stabMvMovingFrames;
            nr.stabMvAccumSamples += valid;
            nr.stabMvAccumUnwarped += e0;
            nr.stabMvAccumPlus += ep;
            nr.stabMvAccumMinus += em;

            nr.stabMvErrUnwarped = (float) (nr.stabMvAccumUnwarped / (double) nr.stabMvAccumSamples);
            nr.stabMvErrPlus = (float) (nr.stabMvAccumPlus / (double) nr.stabMvAccumSamples);
            nr.stabMvErrMinus = (float) (nr.stabMvAccumMinus / (double) nr.stabMvAccumSamples);
            const float baseErr = nr.stabMvErrUnwarped;
            const float best = std::min(nr.stabMvErrPlus, nr.stabMvErrMinus);
            const float improve = (baseErr > 1e-8f) ? (baseErr - best) / baseErr : 0.0f;
            const float separation =
                (baseErr > 1e-8f) ? std::abs(nr.stabMvErrPlus - nr.stabMvErrMinus) / baseErr : 0.0f;
            const bool enoughFrames = nr.stabMvMovingFrames >= kMvRequiredMovingFrames;
            const bool decisive = enoughFrames && improve >= 0.05f && separation >= 0.02f;
            LOG_INFO("[NRSTAB] MV SELFTEST attempt={} moving_frame={} samples={} enough_samples=1 min_samples={} "
                     "pooled_samples={} unwarped={} plus={} minus={} best={} improvement_pct={} separation_pct={}",
                     nr.stabMvAttempts, nr.stabMvMovingFrames, valid, kMvMinMovingSamples, nr.stabMvAccumSamples,
                     nr.stabMvErrUnwarped, nr.stabMvErrPlus, nr.stabMvErrMinus,
                     nr.stabMvErrPlus < nr.stabMvErrMinus ? "PLUS" : "MINUS", improve * 100.0f, separation * 100.0f);
            if (decisive)
            {
                nr.stabMvSign = nr.stabMvErrPlus < nr.stabMvErrMinus ? 1.0f : -1.0f;
                nr.stabMvSelfTestPassed = true;
                nr.stabMvSelfTestFailed = false;
                nr.stabPrevBaseValid = false;
                PublishNrStabMvConvention(PackNrStabMvConvention(nr.stabMvSign));
                LOG_INFO("[NRSTAB] MV SELFTEST PASS sign={} mv_direction={} attempts={} movinframes={} "
                         "pooled_samples={} improvement_pct={} unwarped={} plus={} minus={}",
                         nr.stabMvSign > 0.0f ? "PLUS" : "MINUS", nr.stabMvSign > 0.0f ? "+1" : "-1", nr.stabMvAttempts,
                         nr.stabMvMovingFrames, nr.stabMvAccumSamples, improve * 100.0f, nr.stabMvErrUnwarped,
                         nr.stabMvErrPlus, nr.stabMvErrMinus);
            }
            else if (nr.stabMvMovingFrames >= kMvMaxMovingFrames)
            {
                nr.stabMvSelfTestFailed = true;
                nr.stabMvSelfTestPassed = false;
                nr.stabPrevBaseValid = false;
                LOG_ERROR("[NRSTAB] MV SELFTEST FAIL attempts={} movinframes={} pooled_samples={} no direction beat "
                          "unwarped decisively; stabilizer disabled for this session",
                          nr.stabMvAttempts, nr.stabMvMovingFrames, nr.stabMvAccumSamples);
            }
            else
            {
                nr.stabPrevBaseValid = false;
                LOG_INFO("[NRSTAB] MV SELFTEST COLLECT attempts={} moving_frames={}/{} max_moving_frames={} "
                         "current_decisive={}",
                         nr.stabMvAttempts, nr.stabMvMovingFrames, kMvRequiredMovingFrames, kMvMaxMovingFrames,
                         decisive ? 1 : 0);
            }
        };

        const auto QueueMvSelfTest = [&]() -> bool
        {
            if (!nr.stabPrevBaseValid || nr.stabMvReadbackPending)
                return false;
            // Fresh storage per diagnostic prevents a previous recording/replay from
            // overwriting a later attempt. Retirement retains old storage while in use.
            ParkNrResource(nr.stabMvDiag);
            ParkNrResource(nr.stabMvReadback);
            nr.stabMvCompletion = {};
            if (!EnsureNrStabResources())
                return false;
            DlssNrConstants t {};
            t.Mode = DlssNrResidualMode_MvSelfTest;
            t.Width = kMvGrid;
            t.Height = kMvGrid;
            t.GuideWidth = motionWidth;
            t.GuideHeight = motionHeight;
            t.ResidualMotionBaseX = motionBaseX;
            t.ResidualMotionBaseY = motionBaseY;
            t.MvScaleX = frame.MvScaleX / float(motionWidth);
            t.MvScaleY = frame.MvScaleY / float(motionHeight);
            t.ResidualFrameWidth = width;
            t.ResidualFrameHeight = height;
            t.ResidualOutputWidth = nrStabOutputWidth;
            t.ResidualOutputHeight = nrStabOutputHeight;
            // NRSTAB consumes the game's native motion domain, not the packed model's vectors.
            if (!shader.DispatchResidualPass(cmdList, t, nr.hdrCopy, nullptr, nr.stabPrevBase, originalMotionIn, nr.stabMvDiag,
                                             false))
                return false;
            D3D12_TEXTURE_COPY_LOCATION src {};
            src.pResource = nr.stabMvDiag;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dst {};
            dst.pResource = nr.stabMvReadback;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            dst.PlacedFootprint.Footprint.Width = kMvGrid;
            dst.PlacedFootprint.Footprint.Height = kMvGrid;
            dst.PlacedFootprint.Footprint.Depth = 1;
            dst.PlacedFootprint.Footprint.RowPitch = kMvGrid * 16u;
            Barrier(cmdList, nr.stabMvDiag, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            Barrier(cmdList, nr.stabMvDiag, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            nr.stabMvCompletion = lifetime.ReadbackProbe(cmdList);
            nr.stabMvReadbackPending = true;
            nr.stabMvReadbackFrame = frames;
            ++nr.stabMvSelfTestDispatches;
            return true;
        };

        const auto RunNrStabSelector = [&]() -> bool
        {
            const bool hadUsableHistory = nr.stabHistoryValid && !frame.Reset;
            const unsigned next = nr.stabHistoryIndex ^ 1u;
            auto* previous = hadUsableHistory ? nr.stabHistory[nr.stabHistoryIndex] : nr.stabResolved;
            auto* history = nr.stabHistory[next];
            if (nr.stabHistoryReadable[next])
            {
                Barrier(cmdList, history, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                nr.stabHistoryReadable[next] = false;
            }
            DlssNrConstants s {};
            s.Mode = DlssNrResidualMode_VarianceSelect;
            s.Width = width;
            s.Height = height;
            s.ResidualBlend = nrStabK;
            s.ResidualHistoryValid = hadUsableHistory ? 1u : 0u;
            s.ResidualMotionBaseX = motionBaseX;
            s.ResidualMotionBaseY = motionBaseY;
            s.GuideWidth = motionWidth;
            s.GuideHeight = motionHeight;
            s.MvScaleX = frame.MvScaleX / float(motionWidth);
            s.MvScaleY = frame.MvScaleY / float(motionHeight);
            s.MaxRatio = nrStabMotionRejectPx;
            s.ResidualMotionSign = nr.stabMvSign;
            s.ResidualFrameWidth = width;
            s.ResidualFrameHeight = height;
            s.ResidualOutputWidth = nrStabOutputWidth;
            s.ResidualOutputHeight = nrStabOutputHeight;
            const bool ok = shader.DispatchResidualPass(cmdList, s, nr.hdrCopy, nr.stabResolved, previous, originalMotionIn,
                                                        history, false);
            if (ok)
            {
                Barrier(cmdList, history, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                nr.stabHistoryReadable[next] = true;
                nr.stabHistoryIndex = next;
                nr.stabHistoryValid = true;
                ++nr.stabSelectorFrames;
                if (hadUsableHistory)
                    ++nr.stabWarmHistoryFrames;
            }
            return ok;
        };

        const bool nrStabResourcesReady = nrStabEligible ? EnsureNrStabResources() : false;
        if (nrStabEligible && nrStabResourcesReady && !nr.stabMvSelfTestPassed && !nr.stabMvSelfTestFailed)
        {
            const uint32_t cachedConvention = nrStabSessionMvConvention.load(std::memory_order_acquire);

            const bool cacheConflicted = (cachedConvention & kNrStabMvConflict) != 0u;

            const bool cacheValid = !cacheConflicted && (cachedConvention & kNrStabMvValid) != 0u;

            if (cacheConflicted && !nr.stabSessionCacheConflictLogged)
            {
                nr.stabSessionCacheConflictLogged = true;
                LOG_WARN("[NRSTAB] MV SESSION CACHE CONFLICT ACTIVE "
                         "cache=0x{:08X}; inheritance_disabled=1 "
                         "startup_selftest_required=1",
                         cachedConvention);
            }
            else if (cacheValid)
            {
                const int cachedMvDirection = (cachedConvention & kNrStabMvDirectionMinus) ? -1 : 1;

                const int cachedMvScaleSignX = (cachedConvention & kNrStabMvScaleXMinus) ? -1 : 1;

                const int cachedMvScaleSignY = (cachedConvention & kNrStabMvScaleYMinus) ? -1 : 1;

                const int currentMvScaleSignX = frame.MvScaleX < 0.0f ? -1 : 1;

                const int currentMvScaleSignY = frame.MvScaleY < 0.0f ? -1 : 1;

                const bool mvEncodingMatches =
                    cachedMvScaleSignX == currentMvScaleSignX && cachedMvScaleSignY == currentMvScaleSignY;

                if (mvEncodingMatches)
                {
                    // Cache hit restores ONLY the convention.
                    // History is unconditionally cold-started, including
                    // same-geometry feature recreation (Quality -> Quality).
                    // motion_to_output remains live-derived and is never cached.
                    nr.stabMvSign = (float) cachedMvDirection;
                    nr.stabMvSelfTestPassed = true;
                    nr.stabMvSelfTestFailed = false;

                    nr.stabHistoryValid = false;
                    nr.stabPrevBaseValid = false;
                    nr.stabPathReady = false;

                    nr.stabSelfTestWaitingMotion = false;
                    nr.stabSelfTestWaitFrames = 0;
                    nr.stabSelfTestWaitWarned = false;

                    nr.stabMvAttempts = 0;
                    nr.stabMvMovingFrames = 0;
                    nr.stabMvAccumSamples = 0;
                    nr.stabMvAccumUnwarped = 0.0;
                    nr.stabMvAccumPlus = 0.0;
                    nr.stabMvAccumMinus = 0.0;

                    if (nr.stabMvReadbackPending)
                        nr.stabMvIgnorePending = true;

                    LOG_INFO("[NRSTAB] MV SESSION CACHE HIT "
                             "mv_direction={} "
                             "mv_scale_sign={}x{} "
                             "frame={}x{} motion={}x{} "
                             "motion_to_output={}x{} "
                             "history_cold_start=1 "
                             "selftest_rerun=0",
                             cachedMvDirection > 0 ? "+1" : "-1", cachedMvScaleSignX, cachedMvScaleSignY, width, height,
                             motionWidth, motionHeight, nrStabLiveMotionToOutputX, nrStabLiveMotionToOutputY);
                }
                else if (!nr.stabSessionCacheEncodingMissLogged)
                {
                    nr.stabSessionCacheEncodingMissLogged = true;
                    LOG_INFO("[NRSTAB] MV SESSION CACHE MISS "
                             "reason=mv_encoding_changed "
                             "cached_direction={} "
                             "cached_scale_sign={}x{} "
                             "current_scale_sign={}x{} "
                             "startup_selftest_required=1",
                             cachedMvDirection > 0 ? "+1" : "-1", cachedMvScaleSignX, cachedMvScaleSignY,
                             currentMvScaleSignX, currentMvScaleSignY);
                }
            }
        }
        if (nrStabEligible)
            nr.stabSelfTestEverCalled = true;
        if (nrStabEligible && nrStabResourcesReady)
        {
            ConsumeMvSelfTest();
            if (!nr.stabMvSelfTestPassed && !nr.stabMvSelfTestFailed)
            {
                // Waiting is a distinct state from testing. Count wall frames only while the last
                // evidence said "no usable motion" (including the initial startup state). Once any
                // moving attempt fires, this resets to zero and stays there during TESTING/READBACK
                // until another zero-moving result explicitly puts the self-test back into WAIT_MOTION.
                if (nr.stabSelfTestWaitingMotion && !nr.stabMvReadbackPending)
                {
                    static constexpr unsigned long long kMvWaitWarnFrames = 500ull;
                    if (nr.stabSelfTestWaitFrames < kMvWaitWarnFrames)
                        ++nr.stabSelfTestWaitFrames;
                    if (nr.stabSelfTestWaitFrames >= kMvWaitWarnFrames && !nr.stabSelfTestWaitWarned)
                    {
                        nr.stabSelfTestWaitWarned = true;
                        LOG_WARN("[NRSTAB] MV SELFTEST waiting for motion wait_frames={} warning_threshold={} "
                                 "attempts={} moving_selftest_frames={} selftest_dispatches={}; counter saturated, "
                                 "continuing without consuming attempts",
                                 nr.stabSelfTestWaitFrames, kMvWaitWarnFrames, nr.stabMvAttempts, nr.stabMvMovingFrames,
                                 nr.stabMvSelfTestDispatches);
                    }
                }
                if (!nr.stabMvReadbackPending)
                {
                    if (!nr.stabPrevBaseValid)
                        StoreCurrentBase();
                    else
                    {
                        QueueMvSelfTest();
                        StoreCurrentBase();
                    }
                }
            }

            if (nr.stabMvSelfTestPassed)
            {
                const bool rawReady = shader.DispatchPass(cmdList, resolveParams, resolveProxy, resolveAnswer,
                                                          nr.hdrCopy, encoded.exposure, nullptr, nr.stabResolved, nullptr);
                bool selected = false;
                if (rawReady)
                {
                    Barrier(cmdList, nr.stabResolved, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    selected = RunNrStabSelector();
                    Barrier(cmdList, nr.stabResolved, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                }
                if (selected)
                {
                    DlssNrConstants a {};
                    a.Mode = DlssNrResidualMode_Apply;
                    a.Width = width;
                    a.Height = height;
                    a.TransferStrength = 1.0f;
                    ID3D12Resource* const nrStabApplyTarget = targetSupportsUav ? resolveTarget : nr.stabResolved;
                    resolved = shader.DispatchResidualPass(cmdList, a, nr.hdrCopy, nr.stabHistory[nr.stabHistoryIndex],
                                                           nullptr, nullptr, nrStabApplyTarget, false);
                    nrStabFrameActive = resolved;
                }
                if (nrStabFrameActive && !nr.stabPathReady)
                {
                    nr.stabPathReady = true;
                    LOG_INFO("[NRSTAB] PATH READY game={} mv_selftest=PASS mv_direction={} mv_sign={} "
                             "residual_mode=VARIANCE_SELECT k={} motion_reject_px={} binary_selection=1 no_blend=1 "
                             "steady_state_readback=0 frame={}x{} motion={}x{} motion_base={},{} mv_scale={}x{} "
                             "motion_to_output={}x{} warp=prev_current_{}motion",
                             nrStabGame, nr.stabMvSign > 0.0f ? "+1" : "-1", nr.stabMvSign > 0.0f ? "PLUS" : "MINUS",
                             nrStabK, nrStabMotionRejectPx, width, height, motionWidth, motionHeight, motionBaseX,
                             motionBaseY, frame.MvScaleX, frame.MvScaleY,
                             (float) nrStabOutputWidth / (float) motionWidth,
                             (float) nrStabOutputHeight / (float) motionHeight,
                             nr.stabMvSign > 0.0f ? "plus_" : "minus_");
                }
            }
        }

        if (!nrStabFrameActive)
        {
            if (nrStabEnabled && nr.stabMvSelfTestPassed)
                ++nr.stabFallbackFrames;
            nr.stabHistoryValid = false;
            if (!targetSupportsUav && !nrStabHdrCopyWritable)
            {
                Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                nrStabHdrCopyWritable = true;
            }
            resolved =
                enlargementReady && shader.DispatchPass(cmdList, resolveParams, resolveProxy, resolveAnswer,
                                                        resolveOriginal, encoded.exposure, nullptr, resolveTarget, nullptr);
            if (!nr.stabFailureLogged)
            {
                if (nrStabEnabled && !nrStabEligible)
                {
                    nr.stabFailureLogged = true;

                    if (!nrStabMotionShapeSane && motionWidth != 0u && motionHeight != 0u)
                    {
                        LOG_WARN("[NRSTAB] GUIDE REJECTED "
                                 "game={} reason={} "
                                 "motion={}x{} "
                                 "motion_low_res={} "
                                 "motion_domain={} "
                                 "expected_extent={}x{} "
                                 "expected_aspect={} "
                                 "actual_aspect={} "
                                 "aspect_ratio={} "
                                 "min_dimension={} "
                                 "aspect_tolerance={} "
                                 "motion_to_output={}x{} "
                                 "raw_nr_fallback=1 "
                                 "history_consumed=0 "
                                 "selftest_consumed=0",
                                 nrStabGame, nrStabGuideRejectReason, motionWidth, motionHeight,
                                 nrStabMotionLowResolution ? 1 : 0, nrStabMotionDomain, nrStabExpectedMotionWidth,
                                 nrStabExpectedMotionHeight, nrStabExpectedMotionAspect, nrStabActualMotionAspect,
                                 nrStabMotionAspectRatio, nrStabMinMotionDimension, nrStabMotionAspectTolerance,
                                 nrStabLiveMotionToOutputX, nrStabLiveMotionToOutputY);
                    }
                    else
                    {
                        LOG_WARN("[NRSTAB] INELIGIBLE "
                                 "game={} enabled=1 "
                                 "residual_across_rr={} "
                                 "deferred_dlss={} "
                                 "before_upscale={} "
                                 "finished_picture={} "
                                 "target_uav={} "
                                 "motion={}x{} "
                                 "debug={} compare={} apply={}; "
                                 "raw-NR fallback active",
                                 nrStabGame, cfg.DlssNrResidualAcrossRr.value_or_default() ? 1 : 0,
                                 cfg.DlssNrDeferredDlss.value_or_default() ? 1 : 0, frame.BeforeUpscale ? 1 : 0,
                                 frame.FinishedPicture ? 1 : 0, targetSupportsUav ? 1 : 0, motionWidth, motionHeight,
                                 resolveParams.DebugView, resolveParams.CompareMode, resolveParams.ApplyModel);
                    }
                }
                else if (nrStabEnabled && nrStabEligible && !nrStabResourcesReady)
                {
                    nr.stabFailureLogged = true;
                    LOG_ERROR("[NRSTAB] selector resources unavailable; raw-NR fallback active");
                }
            }
        }

        const ULONGLONG nrStabNow = GetTickCount64();
        if (nrStabNow - nr.stabLastStatusTick >= 1000ull)
        {
            nr.stabLastStatusTick = nrStabNow;
            const unsigned long long readbackPendingFrames =
                nr.stabMvReadbackPending && frames >= nr.stabMvReadbackFrame
                    ? frames - nr.stabMvReadbackFrame : 0ull;
            const char* selfState = nr.stabMvSelfTestPassed        ? "PASS"
                                    : nr.stabMvSelfTestFailed      ? "FAIL"
                                    : !nr.stabSelfTestEverCalled   ? "NOT_CALLED"
                                    : nr.stabMvReadbackPending     ? "READBACK"
                                    : nr.stabSelfTestWaitingMotion ? "WAIT_MOTION"
                                                                   : "TESTING";
            LOG_INFO(
                "[NRSTAB] STATUS game={} ab_state={} active={} enabled={} mv_selftest={} mv_direction={} mv_sign={} "
                "eligible={} resources_ready={} target_uav={} before_upscale={} finished_picture={} "
                "residual_across_rr={} deferred_dlss={} debug={} compare={} apply={} motion={}x{} k={} "
                "motion_reject_px={} history_valid={} selector_frames={} warm_history_frames={} fallback_frames={} "
                "selftest_wait_frames={} selftest_attempts={} moving_selftest_frames={} selftest_dispatches={} "
                "readback_wait_frames={} completion=submitted_sealed_fence steady_state_readback=0 startup_selftest_readback=1 motion_to_output={}x{}",
                nrStabGame, nrStabEnabled ? (nrStabFrameActive ? "ACTIVE" : "ARMING") : "BYPASSED",
                nrStabFrameActive ? 1 : 0, nrStabEnabled ? 1 : 0, selfState,
                nr.stabMvSign > 0.0f   ? "+1"
                : nr.stabMvSign < 0.0f ? "-1"
                                       : "0",
                nr.stabMvSign > 0.0f   ? "PLUS"
                : nr.stabMvSign < 0.0f ? "MINUS"
                                       : "UNSET",
                nrStabEligible ? 1 : 0, nrStabResourcesReady ? 1 : 0, targetSupportsUav ? 1 : 0,
                frame.BeforeUpscale ? 1 : 0, frame.FinishedPicture ? 1 : 0,
                cfg.DlssNrResidualAcrossRr.value_or_default() ? 1 : 0,
                cfg.DlssNrDeferredDlss.value_or_default() ? 1 : 0, resolveParams.DebugView, resolveParams.CompareMode,
                resolveParams.ApplyModel, motionWidth, motionHeight, nrStabK, nrStabMotionRejectPx,
                nr.stabHistoryValid ? 1 : 0, nr.stabSelectorFrames, nr.stabWarmHistoryFrames, nr.stabFallbackFrames,
                nr.stabSelfTestWaitFrames, nr.stabMvAttempts, nr.stabMvMovingFrames, nr.stabMvSelfTestDispatches,
                readbackPendingFrames, motionWidth ? (float) nrStabOutputWidth / (float) motionWidth : 0.0f,
                motionHeight ? (float) nrStabOutputHeight / (float) motionHeight : 0.0f);
            LOG_INFO("[NRSTAB] GUIDE STATUS "
                     "game={} "
                     "motion={}x{} "
                     "motion_low_res={} "
                     "motion_domain={} "
                     "expected_extent={}x{} "
                     "expected_aspect={} "
                     "actual_aspect={} "
                     "aspect_ratio={} "
                     "min_dimension={} "
                     "aspect_tolerance={} "
                     "guide_sane={} "
                     "motion_to_output={}x{}",
                     nrStabGame, motionWidth, motionHeight, nrStabMotionLowResolution ? 1 : 0, nrStabMotionDomain,
                     nrStabExpectedMotionWidth, nrStabExpectedMotionHeight, nrStabExpectedMotionAspect,
                     nrStabActualMotionAspect, nrStabMotionAspectRatio, nrStabMinMotionDimension,
                     nrStabMotionAspectTolerance, nrStabMotionShapeSane ? 1 : 0, nrStabLiveMotionToOutputX,
                     nrStabLiveMotionToOutputY);
        }

        const float nrUiScaleX = nrStabLiveMotionToOutputX;
        const float nrUiScaleY = nrStabLiveMotionToOutputY;
        DlssNr::NrStabUi::State nrUiState = !nrStabEnabled             ? DlssNr::NrStabUi::BYPASSED
                                            : !nrStabEligible          ? DlssNr::NrStabUi::INELIGIBLE
                                            : nr.stabMvSelfTestFailed  ? DlssNr::NrStabUi::FAILED
                                            : nrStabFrameActive        ? DlssNr::NrStabUi::ACTIVE
                                            : nr.stabMvReadbackPending ? DlssNr::NrStabUi::READBACK
                                            : !nr.stabMvSelfTestPassed && nr.stabSelfTestWaitingMotion
                                                ? DlssNr::NrStabUi::WAIT_MOTION
                                            : !nr.stabMvSelfTestPassed ? DlssNr::NrStabUi::TESTING
                                                                       : DlssNr::NrStabUi::READY;
        DlssNr::NrStabUi::Publish(nrUiState,
                                  nr.stabMvSign > 0.0f   ? 1
                                  : nr.stabMvSign < 0.0f ? -1
                                                         : 0,
                                  nr.stabHistoryValid, nrUiScaleX, nrUiScaleY);
        compositionSucceeded = resolved;
        if (resizeFieldReadable)
            Barrier(cmdList, enlarger->input.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        if (resolved && !targetSupportsUav)
        {
            ID3D12Resource* const copySource = nrStabFrameActive ? nr.stabResolved : nr.hdrCopy;

            const D3D12_RESOURCE_STATES copySourceRest = nrStabFrameActive
                                                             ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                                                             : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

            Barrier(cmdList, copySource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

            const D3D12_RESOURCE_STATES priorTargetState = targetState;
            TransitionTarget(D3D12_RESOURCE_STATE_COPY_DEST);
            cmdList->CopyResource(target, copySource);
            TransitionTarget(priorTargetState);

            Barrier(cmdList, copySource, D3D12_RESOURCE_STATE_COPY_SOURCE, copySourceRest);

            if (!nrStabFrameActive)
                nrStabHdrCopyWritable = false;
        }
        else if (!targetSupportsUav && nrStabHdrCopyWritable)
        {
            Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            nrStabHdrCopyWritable = false;
        }
        MakeModelWritable(nr.output);
        if (nr.passScratch != nullptr)
            MakeModelWritable(nr.passScratch);

        if (superDownOk)
        {
            if (spatial)
            {
                for (auto* nativePair : { nr.spatialProxyNative, nr.spatialAnswerNative })
                    Barrier(cmdList, nativePair, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            else
                Barrier(cmdList, nr.outputNative, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        // Schedule matched proxy/output capture for delayed readback.
        if (captureFrames.isActive())
        {
            captureFrames.record(cmdList, device, nr.colorCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                 target, targetState);

        }
    }
    else if (result != NVSDK_NGX_Result_Success)
    {
        if (spatial)
        {
            nr.spatialFallback = true;
            nr.spatialFallbackReason = "NGX rejected the packed model input";
            nr.reset = true;
            modelRunning = false;
            for (auto& model : nr.models)
                model.RetryAfterFailure();
            LOG_WARN("DLSS-NR spatial evaluate returned 0x{:X} ({}); trying ordinary NR next frame",
                     (uint32_t) result, NgxResultName((unsigned int) result));
        }
        else
        {
            nr.failed = true;
            nr.reason = "the model refused to run";
            LOG_ERROR("DLSS-NR evaluate returned 0x{:X} ({}); use Retry to recreate the model", (uint32_t) result,
                      NgxResultName((unsigned int) result));
        }
    }

    // Restore all intermediate surfaces to the UAV state expected by the next frame.
    MakeModelWritable(nr.output);
    if (nr.passScratch != nullptr)
        MakeModelWritable(nr.passScratch);

    Barrier(cmdList, nr.hdrCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (nr.passClamp != nullptr)
        MakeModelWritable(nr.passClamp);

    if (spatial)
    {
        for (auto* packed : { nr.spatialColor, nr.spatialDepth, nr.spatialMotion })
            Barrier(cmdList, packed, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (spatialUnpacked)
            for (auto* unpacked : { nr.spatialProxy, nr.spatialAnswer })
                Barrier(cmdList, unpacked, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    // The matched guides were written this frame and read by the model; back to UAV for the next.
    if (matchedGuides)
    {
        Barrier(cmdList, nr.depthSmall, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(cmdList, nr.motionSmall, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    // Failed evaluations leave the game's original image intact. A successful copy-back writes
    // only the active rectangle and restores both resources before DLSS consumes the image.
    FinishColor(compositionSucceeded);
    if (compositionSucceeded)
        ++nr.successfulDispatches;
    else
    {
        nr.stabHistoryValid = false;
        nr.stabPrevBaseValid = false;
        if (nr.stabMvReadbackPending) nr.stabMvIgnorePending = true;
        DlssNr::NrStabUi::Publish(nr.failed ? DlssNr::NrStabUi::FAILED : DlssNr::NrStabUi::INELIGIBLE,
                                 0, false, 0.0f, 0.0f);
    }
    nr.spatialActive = spatial && compositionSucceeded;

    EndGpuTiming(cmdList);

    // Restore guide clones to COPY_DEST for the next frame's refresh.
    if (originalDepthIn == nr.depthClone)
        Barrier(cmdList, nr.depthClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST);

    if (originalMotionIn == nr.motionClone)
        Barrier(cmdList, nr.motionClone, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST);

    if (reduced && !spatial && nr.colorSmall != nullptr)
        Barrier(cmdList, nr.colorSmall, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Leave the staging copy as the next frame expects to find it.
    Barrier(cmdList, nr.colorCopy, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}
