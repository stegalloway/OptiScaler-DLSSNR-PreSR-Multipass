#pragma once
#include <dlssnr/DlssNr_GpuLifetime.h>
#include <dlssnr/DlssNr_Proxy.h>
#include <dlssnr/PassProfiles.h>
#include <dlssnr/DlssNrFeature_Dx12.h>
#include <shaders/output_scaling/OS_Dx12.h>
#include "DlssNr_Spatial.h"

namespace DlssNr::Detail
{
struct ModelStateDx12
{
    unsigned long long successfulDispatches = 0;
    // Each model pass owns its NGX feature, parameters and temporal history.
    DlssNr::Proxy::Context models[DlssNr::MaxPassCount];
    bool passCreateFailed[DlssNr::MaxPassCount] = {};

    // The model cannot read and write one resource, so the frame is staged through these.
    ID3D12Resource* colorCopy = nullptr;
    ID3D12Resource* output = nullptr;

    // The second half of the model-output ping-pong. The base proxy stays immutable: pass 0 writes
    // output (A), pass 1 writes this (B), and pass 2 writes A again. Only the final answer is composed.
    ID3D12Resource* passScratch = nullptr;
    bool passScratchFailed = false;
    ID3D12Resource* passClamp = nullptr; // bounded input for the next model pass

    // The frame as the upscaler wrote it. The resolve adds the model's edit to this rather than
    // reconstructing it by inverting the tone curve, which is what turned every light in the frame into
    // a string of coloured cells.
    ID3D12Resource* hdrCopy = nullptr;
    ID3D12Resource* exposureMeter = nullptr;
    ID3D12Resource* exposure = nullptr;
    bool exposureReadable = false;
    bool exposureValid = false; // Successful exposure production, independent of the resource state.
    unsigned exposureSource = 0;
    float exposurePreExposure = 1;

    // Compact origin-zero pre-SR image, only needed when Color has allocation padding. All codec,
    // hold and capture paths then see the real raster. UAV at rest, retired with the scratch set.
    ID3D12Resource* activeColor = nullptr;

    // The frame shrunk for the model, when it is working below full resolution.
    ID3D12Resource* colorSmall = nullptr;

    // Peripheral compression keeps a packed model pair and packed guides separate from the
    // ordinary uniform-scale pair consumed by composition and optional DLSS enlargement.
    ID3D12Resource* spatialColor = nullptr;
    ID3D12Resource* spatialDepth = nullptr;
    ID3D12Resource* spatialMotion = nullptr;
    ID3D12Resource* spatialProxy = nullptr;
    ID3D12Resource* spatialAnswer = nullptr;
    ID3D12Resource* spatialProxyNative = nullptr;
    ID3D12Resource* spatialAnswerNative = nullptr;
    Spatial::Layout spatialLayout {};
    bool spatialSignatureValid = false;
    DXGI_FORMAT spatialColorFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT spatialDepthFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT spatialMotionFormat = DXGI_FORMAT_UNKNOWN;
    unsigned spatialDepthW = 0, spatialDepthH = 0, spatialMotionW = 0, spatialMotionH = 0;
    bool spatialFallback = false;
    const char* spatialFallbackReason = "";
    bool spatialActive = false;

    // Supersampling filters are allocated lazily; dispatch dimensions come from the resources.
    OS_Dx12* superUp = nullptr;

    // The down-leg returns the model answer to native size before composition.
    ID3D12Resource* outputNative = nullptr;
    OS_Dx12* superDown = nullptr;
    OS_Dx12* spatialProxyDown = nullptr;
    Scaler nrScaler = Scaler::Count;

    // NRSTAB steady-state GPU history and startup-only MV convention diagnostic.
    ID3D12Resource* stabResolved = nullptr;
    ID3D12Resource* stabHistory[2] = {};
    bool stabHistoryReadable[2] = {};
    unsigned int stabHistoryIndex = 0;
    bool stabHistoryValid = false;
    ID3D12Resource* stabPrevBase = nullptr;
    bool stabPrevBaseReadable = false;
    bool stabPrevBaseValid = false;
    ID3D12Resource* stabMvDiag = nullptr;
    ID3D12Resource* stabMvReadback = nullptr;
    std::function<DlssNr::GpuLifetime::ReadbackState()> stabMvCompletion;
    bool stabMvReadbackPending = false;
    bool stabMvIgnorePending = false;
    unsigned long long stabMvReadbackFrame = 0;
    unsigned int stabMvAttempts = 0;
    unsigned int stabMvMovingFrames = 0;
    bool stabMvSelfTestPassed = false;
    bool stabMvSelfTestFailed = false;
    float stabMvSign = 0.0f;
    float stabMvErrUnwarped = 0.0f, stabMvErrPlus = 0.0f, stabMvErrMinus = 0.0f;
    unsigned int stabMvValidSamples = 0;
    unsigned long long stabMvAccumSamples = 0;
    double stabMvAccumUnwarped = 0.0, stabMvAccumPlus = 0.0, stabMvAccumMinus = 0.0;
    unsigned long long stabSelectorFrames = 0, stabWarmHistoryFrames = 0, stabFallbackFrames = 0;
    unsigned long long stabSelfTestWaitFrames = 0, stabMvSelfTestDispatches = 0;
    bool stabSelfTestEverCalled = false, stabSelfTestWaitingMotion = true, stabSelfTestWaitWarned = false;
    ULONGLONG stabLastStatusTick = 0;
    bool stabAnnounced = false, stabPathReady = false, stabFailureLogged = false;
    bool stabControlInitialized = false, stabAppliedEnabled = true;
    float stabAppliedK = 1.0f, stabAppliedMotionRejectPx = 2.0f;
    bool stabPlacementInitialized = false;
    bool stabBeforeUpscale = false;
    // Diagnostic notices are per NR owner; do not reset them every frame.
    bool stabSessionCacheConflictLogged = false;
    bool stabSessionCacheEncodingMissLogged = false;

    // Frame hold (design/frame-hold.md): a persistent copy of the output taken on hold-on and restored
    // over the live output before the encode reads it while held, so a setting change re-renders the
    // same frame. heldWhitePoint preserves the encode scale for the comparison.
    ID3D12Resource* heldColor = nullptr;
    bool heldActive = false;
    unsigned int heldWidth = 0;
    unsigned int heldHeight = 0;
    DXGI_FORMAT heldFormat = DXGI_FORMAT_UNKNOWN;
    float heldWhitePoint = 1.0f;

    unsigned int workWidth = 0;
    unsigned int workHeight = 0;

    // Cloned unconditionally when running at present, and only for typeless formats otherwise.
    ID3D12Resource* depthClone = nullptr;
    ID3D12Resource* motionClone = nullptr;

    // Below-native working size, MatchGuides: depth and motion resampled to the working size so
    // the model's guides agree with the colour it is given (R32_FLOAT, R32G32_FLOAT).
    ID3D12Resource* depthSmall = nullptr;
    ID3D12Resource* motionSmall = nullptr;

    unsigned int width = 0;
    unsigned int height = 0;
    bool beforeUpscale = false;
    bool rayReconstruction = false;
    bool reset = true;

    // The preset, style and strengths each live feature was created with.
    ModelSettings builtSettings[DlssNr::MaxPassCount] {};

    // Latch failures until an explicit retry rather than recording failing GPU work every frame.
    bool failed = false;
    const char* reason = "";
};
}
