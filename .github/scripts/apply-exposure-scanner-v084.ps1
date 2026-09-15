param(
    [Parameter(Mandatory=$true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$SourceRoot = (Resolve-Path $SourceRoot).Path

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

Write-Host 'Applying Exposure ScannerFix2 variant on top of the validated v0.8.4 master baseline.'

# Safe exposure scan.
#
# Upstream v0.8.4 reads every candidate from Tick() by blindly asserting the game-owned
# resource is in D3D12_RESOURCE_STATE_UNORDERED_ACCESS. That is not a contract and can
# device-remove games such as Uncharted when the real state differs.
#
# This patch does NOT guess or cache a global resource state. Instead it hooks the legacy
# ResourceBarrier call already used by the game and samples a tracked candidate only at an
# exact safe point: a non-split transition whose StateBefore is explicitly UAV. The copy is
# inserted on that SAME command list immediately before the game's transition, using:
#
#   game's declared UAV -> COPY_SOURCE -> copy -> UAV -> game's original barrier
#
# Split barriers, unknown states, enhanced barriers and candidates which never expose a
# legacy UAV transition are skipped. Failure mode becomes "no scan reading", not a crash.
# Readback validity is tracked per candidate so skipped candidates can never be interpreted
# as stale values.
$scanHeader = Join-Path $SourceRoot 'OptiScaler/dlssnr/DlssNr_ExposureScan.h'
Replace-LiteralOne -Path $scanHeader `
    -Old 'struct D3D12_UNORDERED_ACCESS_VIEW_DESC;' `
    -New "struct D3D12_UNORDERED_ACCESS_VIEW_DESC;`nstruct D3D12_RESOURCE_BARRIER;" `
    -Label 'D3D12_RESOURCE_BARRIER forward declaration'
Replace-LiteralOne -Path $scanHeader `
    -Old @'
void NoteResource(const D3D12_RESOURCE_DESC* desc, ID3D12Resource* resource);

// How many resources have been looked at.
'@.TrimEnd("`r", "`n") `
    -New @'
void NoteResource(const D3D12_RESOURCE_DESC* desc, ID3D12Resource* resource);

// Called by the D3D12 ResourceBarrier hook. The scanner never guesses a resource state:
// it captures only immediately before a non-split transition whose StateBefore is explicitly UAV.
void NoteBarriers(ID3D12GraphicsCommandList* commandList, unsigned int numBarriers,
                  const D3D12_RESOURCE_BARRIER* barriers);

// How many resources have been looked at.
'@.TrimEnd("`r", "`n") `
    -Label 'safe barrier capture declaration'
Replace-LiteralOne -Path $scanHeader `
    -Old 'float BestValue(int* outIndex = nullptr, float* outLowest = nullptr, float* outHighest = nullptr);' `
    -New "float BestValue(int* outIndex = nullptr, float* outLowest = nullptr, float* outHighest = nullptr);`n// Best recent sane candidate for explicit user anchoring. Unlike BestValue this does not require movement or auto-lock.`nfloat BestAnchorValue(int* outIndex = nullptr, float* outLowest = nullptr, float* outHighest = nullptr);" `
    -Label 'manual anchor candidate API'

$scanInternal = Join-Path $SourceRoot 'OptiScaler/dlssnr/DlssNr_ExposureScan_Internal.h'
Replace-LiteralOne -Path $scanInternal `
    -Old 'constexpr size_t kMaxCandidates = 64;' `
    -New "constexpr size_t kMaxCandidates = 64;`n// Generic buffers cannot consume the whole table; reserve room for the tiny float textures most games use for eye adaptation.`nconstexpr size_t kMaxBufferCandidates = 48;" `
    -Label 'exposure texture candidate reserve'
Replace-LiteralOne -Path $scanInternal `
    -Old @'
// Ring depth for the readbacks. Four, so the slot being read is four frames behind the slot being
// written and the read never waits on the GPU. Same depth and the same reason as the meter's.
constexpr unsigned int kSlots = 4;
'@.TrimEnd("`r", "`n") `
    -New @'
// Five slots because safe barrier capture happens earlier in the frame than Tick(). Tick reads the
// slot which will be reused NEXT frame, keeping the CPU read four complete frames behind its GPU copy.
constexpr unsigned int kSlots = 5;
'@.TrimEnd("`r", "`n") `
    -Label 'safe scan readback ring depth'
Replace-LiteralOne -Path $scanInternal `
    -Old @'
    ID3D12Resource* readback[kSlots] = {};
    size_t readbackCounts[kSlots] = {};
    unsigned long long frames = 0;
'@.TrimEnd("`r", "`n") `
    -New @'
    ID3D12Resource* readback[kSlots] = {};
    size_t readbackCounts[kSlots] = {};
    uint64_t readbackValid[kSlots] = {}; // bit i means candidate i was safely copied into this slot
    ID3D12GraphicsCommandList* readbackWriter[kSlots] = {}; // one writer command list per slot; not retained
    unsigned int writeSlot = 0;
    unsigned long long frames = 0;
    bool captureActive = false;
    bool safeCaptureLogged = false;
'@.TrimEnd("`r", "`n") `
    -Label 'safe scan readback validity state'

$scanReadback = Join-Path $SourceRoot 'OptiScaler/dlssnr/DlssNr_ExposureReadback.cpp'
# Decode the packed R channel of DXGI_FORMAT_R11G11B10_FLOAT so a 4-byte packed texel is not
# misinterpreted as an IEEE-754 float.
Replace-LiteralOne -Path $scanReadback `
    -Old @'
    return out;
}

void Barrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* res, D3D12_RESOURCE_STATES from,
'@.TrimEnd("`r", "`n") `
    -New @'
    return out;
}

float R11ToFloat(uint32_t packed)
{
    const uint32_t v = packed & 0x7FFu;
    const uint32_t exponent = (v >> 6) & 0x1Fu;
    const uint32_t mantissa = v & 0x3Fu;

    if (exponent == 0)
        return std::ldexp((float) mantissa, -20); // 2^(1-15-6)

    if (exponent == 31)
        return kCeiling; // infinity/NaN: sample gate will reject it

    return std::ldexp((float) (64u + mantissa), (int) exponent - 21);
}

void Barrier(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* res, D3D12_RESOURCE_STATES from,
'@.TrimEnd("`r", "`n") `
    -Label 'R11G11B10 exposure decode helper'

Replace-LiteralOne -Path $scanReadback `
    -Old @'
                if (t.bytes == 2)
                {
                    uint16_t half = 0;
                    std::memcpy(&half, at, sizeof(half));
                    value = HalfToFloat(half);
                }
                else
                {
                    std::memcpy(&value, at, sizeof(value));
                }
'@.TrimEnd("`r", "`n") `
    -New @'
                if (!t.isBuffer && t.texFormat == DXGI_FORMAT_R11G11B10_FLOAT)
                {
                    uint32_t packed = 0;
                    std::memcpy(&packed, at, sizeof(packed));
                    value = R11ToFloat(packed);
                }
                else if (t.bytes == 2)
                {
                    uint16_t half = 0;
                    std::memcpy(&half, at, sizeof(half));
                    value = HalfToFloat(half);
                }
                else
                {
                    std::memcpy(&value, at, sizeof(value));
                }
'@.TrimEnd("`r", "`n") `
    -Label 'R11G11B10 exposure readback decode'

Replace-LiteralOne -Path $scanReadback `
    -Old @'
ScanState g_scan;
std::mutex g_scanMutex;
std::mutex g_tickMutex;
'@.TrimEnd("`r", "`n") `
    -New @'
ScanState g_scan;
std::mutex g_scanMutex;
std::mutex g_tickMutex;
// Prevent our injected UAV->COPY_SOURCE->UAV barriers recursively re-entering the capture hook.
thread_local bool g_scanBarrierInjection = false;
'@.TrimEnd("`r", "`n") `
    -Label 'safe scan barrier recursion guard'

$oldTickStart = @'
}
using namespace Detail;
void Tick(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, uint64_t submissionEpoch)
'@
$newTickStart = @'
}
using namespace Detail;

void NoteBarriers(ID3D12GraphicsCommandList* commandList, unsigned int numBarriers,
                  const D3D12_RESOURCE_BARRIER* barriers)
{
    if (!Wanted() || g_scanBarrierInjection || commandList == nullptr || barriers == nullptr || numBarriers == 0)
        return;

    // Keep all scan copies on a direct command list. Compute/copy-queue candidates are skipped rather
    // than creating cross-queue ownership/synchronisation that the scanner cannot prove safe.
    if (commandList->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return;

    // Never stall the game's command-recording threads behind the scanner. Missing one opportunity is
    // harmless: another UAV transition can be sampled later. Crucially, no stale state is remembered.
    std::unique_lock<std::mutex> lock(g_scanMutex, std::try_to_lock);
    if (!lock.owns_lock() || g_scan.tracked.empty())
        return;

    if (!g_scan.captureActive)
    {
        g_scan.captureActive = true;
        g_scan.frames = 0;
        g_scan.writeSlot = 0;
        std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
        std::fill(std::begin(g_scan.readbackValid), std::end(g_scan.readbackValid), 0);
        std::fill(std::begin(g_scan.readbackWriter), std::end(g_scan.readbackWriter), nullptr);
    }

    const unsigned int slot = g_scan.writeSlot;
    ID3D12Resource* dst = g_scan.readback[slot];
    if (dst == nullptr)
        return; // Tick allocates the readback ring; capture starts on a later frame.

    // A readback slot has exactly one command-list writer. This avoids unsynchronised writes to the
    // same readback resource from multiple game command lists/queues in one frame.
    if (g_scan.readbackWriter[slot] != nullptr && g_scan.readbackWriter[slot] != commandList)
        return;

    static_assert(kMaxCandidates <= 64, "readbackValid uses one uint64_t bit per candidate");

    for (unsigned int barrierIndex = 0; barrierIndex < numBarriers; ++barrierIndex)
    {
        const D3D12_RESOURCE_BARRIER& b = barriers[barrierIndex];
        if (b.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION || b.Flags != D3D12_RESOURCE_BARRIER_FLAG_NONE ||
            b.Transition.pResource == nullptr ||
            b.Transition.StateBefore != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            continue;

        for (size_t i = 0; i < g_scan.tracked.size(); ++i)
        {
            Tracked& t = g_scan.tracked[i];
            if (t.resource != b.Transition.pResource || t.resource == nullptr)
                continue;

            // For textures we copy subresource 0. A transition for some other subresource tells us
            // nothing about subresource 0, so skip it. Buffers have a single logical subresource.
            if (!t.isBuffer && b.Transition.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
                b.Transition.Subresource != 0)
                break;

            const uint64_t bit = uint64_t(1) << i;
            if ((g_scan.readbackValid[slot] & bit) != 0)
                break; // one sample per candidate per frame is enough

            g_scanBarrierInjection = true;
            Barrier(commandList, t.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);

            if (t.isBuffer)
            {
                commandList->CopyBufferRegion(dst, i * kStride, t.resource, 0, t.bytes);
            }
            else
            {
                D3D12_TEXTURE_COPY_LOCATION src {};
                src.pResource = t.resource;
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = 0;

                D3D12_TEXTURE_COPY_LOCATION to {};
                to.pResource = dst;
                to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                to.PlacedFootprint.Offset = i * kStride;
                to.PlacedFootprint.Footprint.Format = t.texFormat;
                to.PlacedFootprint.Footprint.Width = 1;
                to.PlacedFootprint.Footprint.Height = 1;
                to.PlacedFootprint.Footprint.Depth = 1;
                to.PlacedFootprint.Footprint.RowPitch = 256;

                D3D12_BOX one { 0, 0, 0, 1, 1, 1 };
                commandList->CopyTextureRegion(&to, 0, 0, 0, &src, &one);
            }

            Barrier(commandList, t.resource, D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            g_scanBarrierInjection = false;

            g_scan.readbackWriter[slot] = commandList;
            g_scan.readbackValid[slot] |= bit;
            g_scan.readbackCounts[slot] = std::max(g_scan.readbackCounts[slot], i + 1);
            break;
        }
    }
}

void Tick(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, uint64_t submissionEpoch)
'@
Replace-LiteralOne -Path $scanReadback -Old $oldTickStart.TrimEnd("`r", "`n") -New $newTickStart.TrimEnd("`r", "`n") -Label 'safe barrier capture implementation'

Replace-LiteralOne -Path $scanReadback `
    -Old @'
    if (!Wanted())
        return;

    if (device == nullptr || cmdList == nullptr)
'@.TrimEnd("`r", "`n") `
    -New @'
    if (!Wanted())
    {
        // Tick still runs while NR is active. Clear capture metadata on the off edge so a later
        // re-enable cannot consume stale slots or suppress new samples with old validity bits.
        std::lock_guard<std::mutex> offLock(g_scanMutex);
        if (g_scan.captureActive)
        {
            g_scan.captureActive = false;
            g_scan.frames = 0;
            g_scan.writeSlot = 0;
            std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
            std::fill(std::begin(g_scan.readbackValid), std::end(g_scan.readbackValid), 0);
            std::fill(std::begin(g_scan.readbackWriter), std::end(g_scan.readbackWriter), nullptr);
        }
        return;
    }

    if (device == nullptr || cmdList == nullptr)
'@.TrimEnd("`r", "`n") `
    -Label 'safe scan toggle-session reset'

Replace-LiteralOne -Path $scanReadback `
    -Old @'
    std::lock_guard<std::mutex> lock(g_scanMutex);

    if (g_scan.tracked.empty())
'@.TrimEnd("`r", "`n") `
    -New @'
    std::lock_guard<std::mutex> lock(g_scanMutex);

    if (!g_scan.captureActive)
    {
        g_scan.captureActive = true;
        g_scan.frames = 0;
        g_scan.writeSlot = 0;
        std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
        std::fill(std::begin(g_scan.readbackValid), std::end(g_scan.readbackValid), 0);
        std::fill(std::begin(g_scan.readbackWriter), std::end(g_scan.readbackWriter), nullptr);
    }

    if (!g_scan.safeCaptureLogged)
    {
        LOG_INFO("DLSS-NR exposure scan: safe UAV-barrier capture active; unknown/split states are skipped");
        g_scan.safeCaptureLogged = true;
    }

    if (g_scan.tracked.empty())
'@.TrimEnd("`r", "`n") `
    -Label 'safe scan activation marker'

Replace-LiteralOne -Path $scanReadback `
    -Old @'
    // Read the slot written four frames ago before overwriting it. Retired by now, so this reads
    // mapped memory rather than waiting on the GPU.
    if (g_scan.frames >= kSlots)
    {
        ID3D12Resource* old = g_scan.readback[g_scan.frames % kSlots];
'@.TrimEnd("`r", "`n") `
    -New @'
    // Barrier capture writes earlier in the frame. Read the slot which will be reused next frame;
    // with five slots it is four complete frames old and therefore not the slot the GPU is writing now.
    if (g_scan.frames >= kSlots)
    {
        const unsigned int readSlot = (g_scan.writeSlot + 1) % kSlots;
        ID3D12Resource* old = g_scan.readback[readSlot];
        const uint64_t validMask = g_scan.readbackValid[readSlot];
'@.TrimEnd("`r", "`n") `
    -Label 'safe scan old-slot selection'
Replace-LiteralOne -Path $scanReadback `
    -Old '            const auto readableCount = std::min(g_scan.tracked.size(), g_scan.readbackCounts[g_scan.frames % kSlots]);' `
    -New '            const auto readableCount = std::min(g_scan.tracked.size(), g_scan.readbackCounts[readSlot]);' `
    -Label 'safe scan readable count slot'
Replace-LiteralOne -Path $scanReadback `
    -Old @'
            for (size_t i = 0; i < readableCount; ++i)
            {
                Tracked& t = g_scan.tracked[i];
                const unsigned char* at = base + i * kStride;
'@.TrimEnd("`r", "`n") `
    -New @'
            for (size_t i = 0; i < readableCount; ++i)
            {
                if ((validMask & (uint64_t(1) << i)) == 0)
                    continue; // never interpret an uncopied/skipped candidate as stale exposure data

                Tracked& t = g_scan.tracked[i];
                const unsigned char* at = base + i * kStride;
'@.TrimEnd("`r", "`n") `
    -Label 'safe scan valid-candidate gate'

$oldUnsafeCopy = @'
    ID3D12Resource* dst = g_scan.readback[g_scan.frames % kSlots];

    if (dst == nullptr)
        return;

    // The state a candidate is in is the game's business and nothing here has a contract about it.
    //
    // UNORDERED_ACCESS is the assumption, and it is the reasonable one: every candidate got here by
    // having an unordered access view created on it, which is what a compute shader writes through,
    // and an eye adaptation buffer is written every frame and read by the next pass. It is still an
    // assumption, which is why the whole scan is behind a setting that is off by default -- getting
    // this wrong on someone's machine costs them a frame or a device, and nobody who has not asked
    // for the scan should be exposed to that.
    for (size_t i = 0; i < g_scan.tracked.size(); ++i)
    {
        Tracked& t = g_scan.tracked[i];

        if (t.resource == nullptr)
            continue;

        Barrier(cmdList, t.resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);

        if (t.isBuffer)
        {
            cmdList->CopyBufferRegion(dst, i * kStride, t.resource, 0, t.bytes);
        }
        else
        {
            D3D12_TEXTURE_COPY_LOCATION src {};
            src.pResource = t.resource;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;

            D3D12_TEXTURE_COPY_LOCATION to {};
            to.pResource = dst;
            to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            to.PlacedFootprint.Offset = i * kStride;
            to.PlacedFootprint.Footprint.Format = t.texFormat;
            to.PlacedFootprint.Footprint.Width = 1;
            to.PlacedFootprint.Footprint.Height = 1;
            to.PlacedFootprint.Footprint.Depth = 1;
            to.PlacedFootprint.Footprint.RowPitch = 256;

            D3D12_BOX one { 0, 0, 0, 1, 1, 1 };
            cmdList->CopyTextureRegion(&to, 0, 0, 0, &src, &one);
        }

        Barrier(cmdList, t.resource, D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    g_scan.readbackCounts[g_scan.frames % kSlots] = g_scan.tracked.size();
    g_scan.frames++;
    g_scan.status = "";
'@
$newSafeRotate = @'
    // Safe copies were recorded by NoteBarriers earlier in the frame. Retire the oldest slot and
    // make it the destination for the NEXT frame. No game-owned resource is touched from Tick().
    const unsigned int nextSlot = (g_scan.writeSlot + 1) % kSlots;
    g_scan.readbackCounts[nextSlot] = 0;
    g_scan.readbackValid[nextSlot] = 0;
    g_scan.readbackWriter[nextSlot] = nullptr;
    g_scan.writeSlot = nextSlot;
    g_scan.frames++;
    g_scan.status = "";
'@
Replace-LiteralOne -Path $scanReadback -Old $oldUnsafeCopy.TrimEnd("`r", "`n") -New $newSafeRotate.TrimEnd("`r", "`n") -Label 'unsafe Tick UAV state assumption block'

Replace-LiteralOne -Path $scanReadback `
    -Old @'
    // The ring may still contain copies of the old candidates. Do not interpret those values
    // as newly adopted resources that happen to occupy the same list positions.
    std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
}
'@.TrimEnd("`r", "`n") `
    -New @'
    // The ring may still contain copies of the old candidates. Do not interpret those values
    // as newly adopted resources that happen to occupy the same list positions.
    std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
    std::fill(std::begin(g_scan.readbackValid), std::end(g_scan.readbackValid), 0);
    std::fill(std::begin(g_scan.readbackWriter), std::end(g_scan.readbackWriter), nullptr);
}
'@.TrimEnd("`r", "`n") `
    -Label 'ReleaseTrackedResources validity reset'
# The same readbackCounts reset appears a second time in Shutdown after the first replacement changed one.
Replace-LiteralOne -Path $scanReadback `
    -Old @'
    g_scan.frames = 0;
    std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
    g_scan.lastEpoch = UINT64_MAX;
'@.TrimEnd("`r", "`n") `
    -New @'
    g_scan.frames = 0;
    g_scan.writeSlot = 0;
    std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
    std::fill(std::begin(g_scan.readbackValid), std::end(g_scan.readbackValid), 0);
    std::fill(std::begin(g_scan.readbackWriter), std::end(g_scan.readbackWriter), nullptr);
    g_scan.captureActive = false;
    g_scan.safeCaptureLogged = false;
    g_scan.lastEpoch = UINT64_MAX;
'@.TrimEnd("`r", "`n") `
    -Label 'Shutdown safe-scan reset'


# Exposure candidate quality filter.
#
# Safe capture prevents invalid resource-state access, but upstream candidate selection still uses
# "widest travel wins". A random 4-byte UAV can therefore beat the real eye-adaptation texture by
# producing sentinels/counters such as 84, 315, 3841 or 1000000. The result is then allowed to drive
# NR paper white and can collapse it to the 0.01 emergency floor.
#
# Keep discovery broad, but make CONTROL conservative:
#   * only finite values inside format-specific sane ranges are eligible to control NR;
#   * >8x single-sample jumps are treated as spikes only for anonymous buffers; float textures may legitimately jump farther;
#   * 3 out-of-range reads reject a candidate; three >8x jumps reject only generic buffers;
#   * at least 8 sane reads and a 4-read sane streak are required before automatic selection;
#   * manual anchoring needs only recent sane reads and strongly prefers tiny float textures;
#   * once a candidate is chosen it is locked until stale/rejected, preventing source hopping;
#   * saved/manual anchors keep the broader original calibration range and are never discarded by candidate heuristics.

# Shared thresholds + per-candidate quality state.
Replace-LiteralOne -Path $scanInternal `
    -Old @'
constexpr float kFloor = 1e-6f;
constexpr float kCeiling = 1e4f;
'@.TrimEnd("`r", "`n") `
    -New @'
constexpr float kFloor = 1e-6f;
constexpr float kCeiling = 1e4f;

// Discovery remains broad. Runtime control is conservative but game-scaled exposure values above 32
// are legitimate (Spider-Man has calibrated points around 91), so buffers and float textures get
// separate ceilings. Hard sentinels still reject immediately.
constexpr float kDriveFloor = 1e-4f;
constexpr float kBufferDriveCeiling = 128.0f;
constexpr float kTextureDriveCeiling = 512.0f;
constexpr float kHardSentinel = 1024.0f;
constexpr float kMaxBufferSingleStep = 8.0f;
constexpr unsigned int kMinDriveReads = 8;
constexpr unsigned int kMinDriveStreak = 4;
constexpr unsigned int kRejectInvalidReads = 3;
constexpr unsigned int kRejectSpikeReads = 3;
constexpr unsigned long long kCandidateStaleFrames = 300;
constexpr unsigned long long kSelectionGraceFrames = 120;
'@.TrimEnd("`r", "`n") `
    -Label 'exposure candidate quality thresholds'

Replace-LiteralOne -Path $scanInternal `
    -Old @'
    unsigned int inRange = 0;   // reads that could plausibly be an exposure
    bool moves = false;
'@.TrimEnd("`r", "`n") `
    -New @'
    unsigned int inRange = 0;   // sane reads eligible to drive NR
    unsigned int saneStreak = 0;
    unsigned int invalidReads = 0;
    unsigned int spikeReads = 0;
    float lastSane = 0.0f;
    unsigned long long lastSaneFrame = 0;
    bool rejected = false;
    bool moves = false;
'@.TrimEnd("`r", "`n") `
    -Label 'exposure candidate quality state'

Replace-LiteralOne -Path $scanInternal `
    -Old @'
    bool captureActive = false;
    bool safeCaptureLogged = false;
'@.TrimEnd("`r", "`n") `
    -New @'
    bool captureActive = false;
    bool safeCaptureLogged = false;
    int activeCandidate = -1; // locked validated source; -1 until one proves itself
    unsigned long long selectionReadyFrame = 0; // grace window lets a better texture candidate emerge
'@.TrimEnd("`r", "`n") `
    -Label 'locked exposure candidate state'

# Replace upstream's broad range accounting with a sane sample gate. Invalid/spike samples never
# become Latest/Lowest/Highest, so one sentinel cannot poison the candidate before it is rejected.
$oldSampleQuality = @'
                if (!std::isfinite(value))
                    continue;

                // Only values that could BE an exposure are allowed into the range, and this is
                // the whole of "8 watching, none moving" never changing.
                //
                // A buffer is usually zero the first time it is read -- created but not yet
                // written, or read a frame before the game fills it. That zero became `lowest`,
                // and since movement is a ratio guarded by `lowest > kFloor`, one early zero
                // disqualified that candidate for the rest of the session however the light
                // changed. The range has to be built from plausible samples, not from whichever
                // sample happened to be first.
                if (value <= kFloor || value >= kCeiling)
                {
                    t.latest = value;
                    t.reads++;
                    continue;
                }

                if (t.inRange == 0)
                {
                    t.lowest = value;
                    t.highest = value;
                }
                else
                {
                    t.lowest = std::min(t.lowest, value);
                    t.highest = std::max(t.highest, value);
                }

                t.inRange++;

                // "Moves" is the whole point of the readout, and the first version of this test
                // was wrong in a way that mattered: a spread of ten percent of the highest value
                // seen is a threshold of zero when the highest value seen is zero, so three buffers
                // sitting at 0.00000 with float noise around them all reported MOVES.
                //
                // Ratios, not differences, and only over values that could be an exposure at all.
                // An exposure is positive, is not a thousandth of a thousandth, and does not sit at
                // a million. Nioh 3's real one runs 0.0019 to 0.616 -- a factor of three hundred --
                // so a quarter is a low bar that noise cannot reach.
                if (t.inRange > 1 && t.highest > t.lowest * 1.25f)
                    t.moves = true;

                t.latest = value;
                t.reads++;
'@
$newSampleQuality = @'
                t.reads++;

                if (t.rejected)
                    continue;

                const float driveCeiling = t.isBuffer ? kBufferDriveCeiling : kTextureDriveCeiling;
                if (!std::isfinite(value) || value < kDriveFloor || value > driveCeiling)
                {
                    t.invalidReads++;
                    t.saneStreak = 0;

                    const bool hardSentinel = std::isfinite(value) && value >= kHardSentinel;
                    if (hardSentinel || t.invalidReads >= kRejectInvalidReads)
                    {
                        t.rejected = true;
                        t.moves = false;
                        if (g_scan.activeCandidate == (int) i)
                        {
                            g_scan.activeCandidate = -1;
                            g_scan.selectionReadyFrame = g_scan.frames + kSelectionGraceFrames;
                        }
                        LOG_WARN("DLSS-NR exposure scan: candidate {} ({}) rejected as junk: value {:.5f}, invalid reads {}",
                                 (unsigned int) (i + 1), t.shape, value, t.invalidReads);
                    }
                    continue;
                }

                // Anonymous buffers frequently contain counters/sentinels, so large one-sample jumps
                // remain suspicious there. Tiny float textures are different: real eye adaptation can
                // jump by well over 8x between loading screens, interiors and daylight. Rejecting those
                // jumps permanently is what made Spider-Man's real candidates disappear.
                if (t.isBuffer && t.lastSane > 0.0f)
                {
                    const float step = std::max(value / t.lastSane, t.lastSane / value);
                    if (step > kMaxBufferSingleStep)
                    {
                        t.spikeReads++;
                        t.saneStreak = 0;
                        if (t.spikeReads >= kRejectSpikeReads)
                        {
                            t.rejected = true;
                            t.moves = false;
                            if (g_scan.activeCandidate == (int) i)
                            {
                                g_scan.activeCandidate = -1;
                                g_scan.selectionReadyFrame = g_scan.frames + kSelectionGraceFrames;
                            }
                            LOG_WARN("DLSS-NR exposure scan: buffer candidate {} ({}) rejected after {} >{:.1f}x spikes",
                                     (unsigned int) (i + 1), t.shape, t.spikeReads, kMaxBufferSingleStep);
                        }
                        continue;
                    }
                }

                if (t.inRange == 0)
                {
                    t.lowest = value;
                    t.highest = value;
                }
                else
                {
                    t.lowest = std::min(t.lowest, value);
                    t.highest = std::max(t.highest, value);
                }

                t.inRange++;
                t.saneStreak++;
                t.lastSane = value;
                t.lastSaneFrame = g_scan.frames;
                t.latest = value;

                // Movement is only meaningful after the candidate has proved it can supply a run of
                // sane readings. This prevents an early pair of unrelated values becoming the source.
                if (t.inRange >= kMinDriveReads && t.saneStreak >= kMinDriveStreak &&
                    t.highest > t.lowest * 1.20f)
                    t.moves = true;
'@
Replace-LiteralOne -Path $scanReadback -Old $oldSampleQuality.TrimEnd("`r", "`n") -New $newSampleQuality.TrimEnd("`r", "`n") -Label 'junk-resistant exposure sample gate'

# Reset quality/lock state whenever scanning is switched off, so re-enabling starts a new validation
# session instead of trusting an old lock or old ranges.
Replace-LiteralOne -Path $scanReadback `
    -Old @'
            std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
            std::fill(std::begin(g_scan.readbackValid), std::end(g_scan.readbackValid), 0);
            std::fill(std::begin(g_scan.readbackWriter), std::end(g_scan.readbackWriter), nullptr);
        }
        return;
'@.TrimEnd("`r", "`n") `
    -New @'
            std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
            std::fill(std::begin(g_scan.readbackValid), std::end(g_scan.readbackValid), 0);
            std::fill(std::begin(g_scan.readbackWriter), std::end(g_scan.readbackWriter), nullptr);
            g_scan.activeCandidate = -1;
            g_scan.selectionReadyFrame = 0;
            for (Tracked& t : g_scan.tracked)
            {
                t.latest = t.lowest = t.highest = t.lastSane = 0.0f;
                t.reads = t.inRange = t.saneStreak = t.invalidReads = t.spikeReads = 0;
                t.lastSaneFrame = 0;
                t.rejected = false;
                t.moves = false;
            }
        }
        return;
'@.TrimEnd("`r", "`n") `
    -Label 'exposure quality reset on scan disable'

# Candidate selection: validated confidence + lock instead of widest-travel-wins.
$scanLogic = Join-Path $SourceRoot 'OptiScaler/dlssnr/DlssNr_ExposureScan.cpp'

# Spider-Man's useful exposure source has previously appeared as R11G11B10_FLOAT. The upstream
# plausible-format list currently excludes it (and RGBA16F) even though its own comment says engines
# may keep the value in a four-channel texture. Restore those small floating-point texture formats.
Replace-LiteralOne -Path $scanLogic `
    -Old @'
    case DXGI_FORMAT_R16G16_FLOAT:
        *outBytes = 2;
        *outName = "R16G16_FLOAT";
        return true;
    default:
'@.TrimEnd("`r", "`n") `
    -New @'
    case DXGI_FORMAT_R16G16_FLOAT:
        *outBytes = 2;
        *outName = "R16G16_FLOAT";
        return true;
    case DXGI_FORMAT_R11G11B10_FLOAT:
        *outBytes = 4; // packed 11/11/10 float; readback decodes the R channel explicitly
        *outName = "R11G11B10_FLOAT";
        return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        *outBytes = 2; // first (R) half-float channel
        *outName = "R16G16B16A16_FLOAT";
        return true;
    default:
'@.TrimEnd("`r", "`n") `
    -Label 'Spider-Man packed/four-channel exposure formats'

$oldCandidateCap = @'
    if (g_scan.tracked.size() >= kMaxCandidates)
    {
        if (!g_scan.complained)
        {
            g_scan.complained = true;
            LOG_WARN("DLSS-NR exposure scan: more than {} candidates, so the filter is too loose here "
                     "rather than the game having {} exposures",
                     kMaxCandidates, kMaxCandidates);
        }

        return;
    }
'@
$newCandidateCap = @'
    const size_t bufferCandidates = std::count_if(g_scan.tracked.begin(), g_scan.tracked.end(),
                                                   [](const Tracked& t) { return t.isBuffer; });
    if (isBuffer && bufferCandidates >= kMaxBufferCandidates)
    {
        if (!g_scan.complained)
        {
            g_scan.complained = true;
            LOG_WARN("DLSS-NR exposure scan: generic buffer reserve reached at {}; keeping {} slots available for float textures",
                     kMaxBufferCandidates, kMaxCandidates - kMaxBufferCandidates);
        }
        return;
    }

    if (g_scan.tracked.size() >= kMaxCandidates)
        return;
'@
Replace-LiteralOne -Path $scanLogic -Old $oldCandidateCap.TrimEnd("`r", "`n") -New $newCandidateCap.TrimEnd("`r", "`n") -Label 'priority exposure candidate admission'
$oldSelection = @'
// How many frames of watching without movement before saying so. At sixty frames a second this is
// about half a minute, which is long enough to have walked somewhere with different light in it and
// short enough that nobody waits on it wondering.
constexpr unsigned int kPatience = 1800;

Verdict Where()
{
    if (!Wanted())
        return Verdict::Off;

    std::lock_guard<std::mutex> lock(g_scanMutex);

    if (g_scan.tracked.empty())
        return Verdict::Waiting;

    unsigned int mostReads = 0;

    for (const Tracked& t : g_scan.tracked)
    {
        if (t.moves)
            return Verdict::Found;

        mostReads = std::max(mostReads, t.reads);
    }

    return mostReads >= kPatience ? Verdict::Barren : Verdict::Watching;
}

const char* Headline()
{
    static std::string line;

    switch (Where())
    {
    case Verdict::Off:
        line = "";
        break;

    case Verdict::Waiting:
    {
        // The examined count is the whole diagnosis. Zero means the hook is not running and no
        // amount of playing will change that; a large number means the game genuinely has nothing
        // shaped like an exposure, which is an answer rather than a failure.
        const unsigned int seen = Examined();
        line = seen == 0 ? "DLSS-NR exposure scan: NOT RUNNING -- no resources seen at all"
                         : "DLSS-NR exposure scan: examined " + std::to_string(seen) +
                               " resources, none shaped like an exposure";
        break;
    }

    case Verdict::Watching:
    {
        std::lock_guard<std::mutex> lock(g_scanMutex);
        unsigned int mostReads = 0;

        for (const Tracked& t : g_scan.tracked)
            mostReads = std::max(mostReads, t.reads);

        line = "DLSS-NR exposure scan: watching " + std::to_string(g_scan.tracked.size()) +
               ", none moving yet -- walk between light and shade  (" +
               std::to_string(mostReads * 100 / kPatience) + "%)";
        break;
    }

    case Verdict::Found:
    {
        std::lock_guard<std::mutex> lock(g_scanMutex);

        // The widest travel wins where several move. An exposure swings by orders of magnitude
        // between a dark interior and open daylight; anything that merely wobbles is something else.
        size_t best = 0;
        float bestRatio = 0.0f;

        for (size_t i = 0; i < g_scan.tracked.size(); ++i)
        {
            const Tracked& t = g_scan.tracked[i];

            if (!t.moves || t.lowest <= kFloor)
                continue;

            const float ratio = t.highest / t.lowest;

            if (ratio > bestRatio)
            {
                bestRatio = ratio;
                best = i;
            }
        }

        char buf[192];
        // The live value is in here so the line visibly ticks. Without it the indicator looks stuck
        // the moment the range settles, which is exactly when it has succeeded.
        snprintf(buf, sizeof(buf),
                 "DLSS-NR exposure scan: FOUND -- candidate %zu = %.5f  (%.5f..%.5f, x%.0f)  done",
                 best + 1, g_scan.tracked[best].latest, g_scan.tracked[best].lowest,
                 g_scan.tracked[best].highest, bestRatio);
        line = buf;
        break;
    }

    case Verdict::Barren:
        line = "DLSS-NR exposure scan: nothing moved. No exposure to find here.";
        break;
    }

    return line.c_str();
}

float BestValue(int* outIndex, float* outLowest, float* outHighest)
{
    std::lock_guard<std::mutex> lock(g_scanMutex);

    int best = -1;
    float bestRatio = 0.0f;

    for (size_t i = 0; i < g_scan.tracked.size(); ++i)
    {
        const Tracked& t = g_scan.tracked[i];

        if (!t.moves || t.lowest <= kFloor)
            continue;

        const float ratio = t.highest / t.lowest;

        if (ratio > bestRatio)
        {
            bestRatio = ratio;
            best = (int) i;
        }
    }

    if (best < 0)
        return 0.0f;

    if (outIndex != nullptr)
        *outIndex = best + 1;

    if (outLowest != nullptr)
        *outLowest = g_scan.tracked[best].lowest;

    if (outHighest != nullptr)
        *outHighest = g_scan.tracked[best].highest;

    return g_scan.tracked[best].latest;
}
'@
$newSelection = @'
// How many frames of watching without a validated source before saying so.
constexpr unsigned int kPatience = 1800;

static float CandidateDriveCeiling(const Tracked& t)
{
    return t.isBuffer ? kBufferDriveCeiling : kTextureDriveCeiling;
}

static bool CandidateUsable(const Tracked& t)
{
    const float ceiling = CandidateDriveCeiling(t);
    if (t.rejected || !t.moves || t.inRange < kMinDriveReads || t.saneStreak < kMinDriveStreak ||
        t.latest < kDriveFloor || t.latest > ceiling || t.lowest < kDriveFloor || t.highest > ceiling)
        return false;

    if (g_scan.frames > t.lastSaneFrame + kCandidateStaleFrames)
        return false;

    return true;
}

static float CandidateConfidence(const Tracked& t)
{
    if (!CandidateUsable(t))
        return -10000.0f;

    const float ratio = std::max(1.0f, t.highest / std::max(t.lowest, kDriveFloor));
    float score = std::min(std::log2(ratio), 4.0f) * 1.25f;
    score += std::min((float) t.inRange, 120.0f) / 30.0f;

    // Eye adaptation is most commonly a tiny floating-point texture. Anonymous buffers remain valid
    // for engines that use them, but should not win merely because they were created first.
    if (!t.isBuffer)
    {
        score += 6.0f;
        if (t.shape.rfind("1x1 ", 0) == 0)
            score += 6.0f;
        if (t.shape.find("R11G11B10_FLOAT") != std::string::npos)
            score += 5.0f;
        else if (t.shape == "1x1 R32_FLOAT")
            score += 2.0f;
    }
    else
    {
        score -= 1.0f;
        if (t.shape == "buffer, 4 bytes")
            score -= 1.0f;
    }

    score -= std::min((float) t.invalidReads, 3.0f);
    score -= std::min((float) t.spikeReads, 3.0f) * 2.0f;
    return score;
}

static int BestObservedCandidateLocked()
{
    int best = -1;
    float bestScore = -10000.0f;
    for (size_t i = 0; i < g_scan.tracked.size(); ++i)
    {
        const float score = CandidateConfidence(g_scan.tracked[i]);
        if (score > bestScore)
        {
            bestScore = score;
            best = (int) i;
        }
    }
    return bestScore > -9999.0f ? best : -1;
}

// Manual calibration is intentionally less strict than automatic control. Requiring `moves` here
// made Anchor here impossible to press before auto-lock, defeating the purpose of manual calibration.
// A texture only needs a couple of recent sane reads. Buffers still need movement because they are
// much more likely to be counters or unrelated constants.
static bool CandidateAnchorUsable(const Tracked& t)
{
    const float ceiling = CandidateDriveCeiling(t);
    if (t.rejected || t.inRange < 2 || t.latest < kDriveFloor || t.latest > ceiling)
        return false;
    if (g_scan.frames > t.lastSaneFrame + kCandidateStaleFrames)
        return false;
    if (t.isBuffer && !t.moves)
        return false;
    return true;
}

static float CandidateAnchorConfidence(const Tracked& t)
{
    if (!CandidateAnchorUsable(t))
        return -10000.0f;

    float score = std::min((float) t.inRange, 120.0f) / 20.0f;
    if (t.moves)
        score += 4.0f;

    if (!t.isBuffer)
    {
        score += 8.0f;
        if (t.shape.rfind("1x1 ", 0) == 0)
            score += 8.0f;
        if (t.shape.find("R11G11B10_FLOAT") != std::string::npos)
            score += 8.0f; // known-good Spider-Man exposure format
        else if (t.shape.find("R32_FLOAT") != std::string::npos)
            score += 4.0f;
        else if (t.shape.find("R16_FLOAT") != std::string::npos)
            score += 2.0f;
    }
    else
    {
        score -= 4.0f;
    }

    score -= std::min((float) t.invalidReads, 3.0f);
    score -= std::min((float) t.spikeReads, 3.0f);
    return score;
}

static int BestManualAnchorCandidateLocked()
{
    int best = -1;
    float bestScore = -10000.0f;
    for (size_t i = 0; i < g_scan.tracked.size(); ++i)
    {
        const float score = CandidateAnchorConfidence(g_scan.tracked[i]);
        if (score > bestScore)
        {
            bestScore = score;
            best = (int) i;
        }
    }
    return bestScore > -9999.0f ? best : -1;
}

static int SelectCandidateLocked()
{
    if (g_scan.activeCandidate >= 0 && g_scan.activeCandidate < (int) g_scan.tracked.size() &&
        CandidateUsable(g_scan.tracked[g_scan.activeCandidate]))
        return g_scan.activeCandidate;

    g_scan.activeCandidate = -1;
    const int best = BestObservedCandidateLocked();
    if (best < 0)
    {
        g_scan.selectionReadyFrame = 0;
        return -1;
    }

    // Do not lock the first merely-usable buffer immediately. HITMAN exposes its real 1x1 R32_FLOAT
    // just after a small generic buffer starts moving. Two seconds of observation lets the texture
    // accumulate enough evidence to outrank it without allowing source hopping after lock.
    if (g_scan.selectionReadyFrame == 0)
        g_scan.selectionReadyFrame = g_scan.frames + kSelectionGraceFrames;
    if (g_scan.frames < g_scan.selectionReadyFrame)
        return -1;

    g_scan.activeCandidate = best;
    const Tracked& t = g_scan.tracked[best];
    LOG_INFO("DLSS-NR exposure scan: locked validated candidate {} ({}) score {:.2f}, range {:.5f}..{:.5f}",
             best + 1, t.shape, CandidateConfidence(t), t.lowest, t.highest);
    return best;
}

Verdict Where()
{
    if (!Wanted())
        return Verdict::Off;

    std::lock_guard<std::mutex> lock(g_scanMutex);
    if (g_scan.tracked.empty())
        return Verdict::Waiting;
    if (SelectCandidateLocked() >= 0)
        return Verdict::Found;

    unsigned int mostReads = 0;
    for (const Tracked& t : g_scan.tracked)
        mostReads = std::max(mostReads, t.reads);
    return mostReads >= kPatience ? Verdict::Barren : Verdict::Watching;
}

const char* Headline()
{
    static std::string line;
    switch (Where())
    {
    case Verdict::Off: line = ""; break;
    case Verdict::Waiting:
    {
        const unsigned int seen = Examined();
        line = seen == 0 ? "DLSS-NR exposure scan: NOT RUNNING -- no resources seen at all"
                         : "DLSS-NR exposure scan: examined " + std::to_string(seen) +
                               " resources, none shaped like an exposure";
        break;
    }
    case Verdict::Watching:
    {
        std::lock_guard<std::mutex> lock(g_scanMutex);
        unsigned int mostReads = 0;
        for (const Tracked& t : g_scan.tracked)
            mostReads = std::max(mostReads, t.reads);
        line = "DLSS-NR exposure scan: validating " + std::to_string(g_scan.tracked.size()) +
               " candidates; manual anchor remains available before auto-lock  (" +
               std::to_string(std::min(100u, mostReads * 100 / kPatience)) + "%)";
        break;
    }
    case Verdict::Found:
    {
        std::lock_guard<std::mutex> lock(g_scanMutex);
        const int best = SelectCandidateLocked();
        if (best < 0) { line = "DLSS-NR exposure scan: validating candidates"; break; }
        const Tracked& t = g_scan.tracked[best];
        const float ratio = t.highest / std::max(t.lowest, kDriveFloor);
        char buf[208];
        snprintf(buf, sizeof(buf),
                 "DLSS-NR exposure scan: VALIDATED -- candidate %d = %.5f  (%.5f..%.5f, x%.1f)  locked",
                 best + 1, t.latest, t.lowest, t.highest, ratio);
        line = buf;
        break;
    }
    case Verdict::Barren: line = "DLSS-NR exposure scan: no validated exposure source found."; break;
    }
    return line.c_str();
}

float BestValue(int* outIndex, float* outLowest, float* outHighest)
{
    std::lock_guard<std::mutex> lock(g_scanMutex);
    const int best = SelectCandidateLocked();
    if (best < 0)
        return 0.0f;
    if (outIndex != nullptr) *outIndex = best + 1;
    if (outLowest != nullptr) *outLowest = g_scan.tracked[best].lowest;
    if (outHighest != nullptr) *outHighest = g_scan.tracked[best].highest;
    return g_scan.tracked[best].latest;
}

float BestAnchorValue(int* outIndex, float* outLowest, float* outHighest)
{
    std::lock_guard<std::mutex> lock(g_scanMutex);
    const int best = BestManualAnchorCandidateLocked();

    static int lastLoggedCandidate = -2;
    if (best != lastLoggedCandidate)
    {
        lastLoggedCandidate = best;
        if (best >= 0)
        {
            const Tracked& t = g_scan.tracked[best];
            LOG_INFO("DLSS-NR exposure scan: manual anchor candidate {} ({}) = {:.5f}, sane reads {}, moves {}",
                     best + 1, t.shape, t.latest, t.inRange, t.moves);
        }
    }

    if (best < 0)
        return 0.0f;
    if (outIndex != nullptr) *outIndex = best + 1;
    if (outLowest != nullptr) *outLowest = g_scan.tracked[best].lowest;
    if (outHighest != nullptr) *outHighest = g_scan.tracked[best].highest;
    return g_scan.tracked[best].latest;
}
'@
Replace-LiteralOne -Path $scanLogic -Old $oldSelection.TrimEnd("`r", "`n") -New $newSelection.TrimEnd("`r", "`n") -Label 'validated exposure candidate selection'

# Anchors are user calibration data, not automatic candidate-safety decisions. Keep the original
# broad finite range so valid game-scaled points such as Spider-Man's ~91.5 survive upgrades.
$scanAnchors = Join-Path $SourceRoot 'OptiScaler/dlssnr/DlssNr_ExposureAnchors.cpp'
$oldAnchorAdd = @'
bool AnchorAdd(float scan, float white)
{
    if (!(scan > kFloor && scan < kCeiling) || !(white > 1e-6f))
        return false;

    std::lock_guard<std::mutex> lock(g_anchorMutex);

    // A near-duplicate scan value would make log(v_{k+1}) - log(v_k) ~ 0 and divide the interpolation
    // by zero. Replace the existing point's white instead of adding a second at the same place.
    for (auto& p : g_anchors)
    {
        if (scan > p.scan * 0.98f && scan < p.scan * 1.02f)
        {
            p.white = white;
            return true;
        }
    }

    if (g_anchors.size() >= 8)
        return false;

    g_anchors.push_back({ scan, white });
    SortAnchorsLocked();
    return true;
}
'@
$newAnchorAdd = @'
bool AnchorAdd(float scan, float white)
{
    if (!(scan > kFloor && scan < kCeiling) || !(white > 1e-6f))
    {
        LOG_WARN("DLSS-NR exposure manual anchor rejected: scan {:.5f}, white {:.5f}", scan, white);
        return false;
    }

    std::lock_guard<std::mutex> lock(g_anchorMutex);
    for (auto& p : g_anchors)
    {
        if (scan > p.scan * 0.98f && scan < p.scan * 1.02f)
        {
            p.white = white;
            LOG_INFO("DLSS-NR exposure manual anchor replaced: scan {:.5f}, white {:.5f}", scan, white);
            return true;
        }
    }

    if (g_anchors.size() >= 8)
    {
        LOG_WARN("DLSS-NR exposure manual anchor rejected: table already has 8 points");
        return false;
    }

    g_anchors.push_back({ scan, white });
    SortAnchorsLocked();
    LOG_INFO("DLSS-NR exposure manual anchor accepted: scan {:.5f}, white {:.5f}", scan, white);
    return true;
}
'@
Replace-LiteralOne -Path $scanAnchors -Old $oldAnchorAdd.TrimEnd("`r", "`n") -New $newAnchorAdd.TrimEnd("`r", "`n") -Label 'manual exposure anchor logging and broad validity'

# Improve the menu wording so it is clear that scanning is now filtered and locked, not widest-range.
$menuInput = Join-Path $SourceRoot 'OptiScaler/dlssnr/DlssNr_MenuInput.cpp'
Replace-LiteralOne -Path $menuInput `
    -Old '            HelpMarker("Game exposure uses supplied data. Scanned exposure requires calibration and may select the wrong buffer.");' `
    -New '            HelpMarker("Game exposure uses supplied data. Scanned exposure validates sane readings, rejects junk/outliers and locks one source before it can drive NR.");' `
    -Label 'filtered scan menu help'
Replace-LiteralOne -Path $menuInput `
    -Old '                const float live = DlssNr::ExposureScan::BestValue(&which, &low, &high);' `
    -New '                const float live = DlssNr::ExposureScan::BestAnchorValue(&which, &low, &high);' `
    -Label 'manual anchor bypasses automatic lock'



$d3d12Hooks = Join-Path $SourceRoot 'OptiScaler/hooks/D3D12_Hooks.cpp'
Replace-LiteralOne -Path $d3d12Hooks `
    -Old @'
// Common
using PFN_SetDescriptorHeaps = rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetDescriptorHeaps)>::type;
using PFN_SetPipelineState = rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetPipelineState)>::type;
'@.TrimEnd("`r", "`n") `
    -New @'
// Common
using PFN_ResourceBarrier = rewrite_signature<decltype(&ID3D12GraphicsCommandList::ResourceBarrier)>::type;
using PFN_SetDescriptorHeaps = rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetDescriptorHeaps)>::type;
using PFN_SetPipelineState = rewrite_signature<decltype(&ID3D12GraphicsCommandList::SetPipelineState)>::type;
'@.TrimEnd("`r", "`n") `
    -Label 'ResourceBarrier hook typedef'
Replace-LiteralOne -Path $d3d12Hooks `
    -Old @'
static bool isUpscalerActive = false;

// Intel Atomic Extension
'@.TrimEnd("`r", "`n") `
    -New @'
static bool isUpscalerActive = false;
static PFN_ResourceBarrier o_ResourceBarrier = nullptr;

// Intel Atomic Extension
'@.TrimEnd("`r", "`n") `
    -Label 'ResourceBarrier trampoline storage'

Replace-LiteralOne -Path $d3d12Hooks `
    -Old @'
VALIDATE_HOOK(hkSetDescriptorHeaps, PFN_SetDescriptorHeaps)
'@.TrimEnd("`r", "`n") `
    -New @'
VALIDATE_HOOK(hkResourceBarrier, PFN_ResourceBarrier)
static void hkResourceBarrier(ID3D12GraphicsCommandList* commandList, UINT NumBarriers,
                              const D3D12_RESOURCE_BARRIER* pBarriers)
{
    // Capture BEFORE the game's barrier: StateBefore is the exact state the game declares at this
    // point on this command list. NoteBarriers internally ignores split/unknown/non-UAV transitions.
    if (commandList != nullptr && pBarriers != nullptr && NumBarriers > 0)
        DlssNr::ExposureScan::NoteBarriers(commandList, NumBarriers, pBarriers);

    o_ResourceBarrier(commandList, NumBarriers, pBarriers);
}

VALIDATE_HOOK(hkSetDescriptorHeaps, PFN_SetDescriptorHeaps)
'@.TrimEnd("`r", "`n") `
    -Label 'ResourceBarrier hook implementation'

Replace-LiteralOne -Path $d3d12Hooks `
    -Old @'
            s_SetPipelineState.o_earlyHook = (PFN_SetPipelineState) pVTable[25];
            s_SetDescriptorHeaps.o_earlyHook = (PFN_SetDescriptorHeaps) pVTable[28];
'@.TrimEnd("`r", "`n") `
    -New @'
            s_SetPipelineState.o_earlyHook = (PFN_SetPipelineState) pVTable[25];
            if (Config::Instance()->DlssNrEnabled.value_or_default())
                o_ResourceBarrier = (PFN_ResourceBarrier) pVTable[26];
            s_SetDescriptorHeaps.o_earlyHook = (PFN_SetDescriptorHeaps) pVTable[28];
'@.TrimEnd("`r", "`n") `
    -Label 'ResourceBarrier vtable capture'
Replace-LiteralOne -Path $d3d12Hooks `
    -Old @'
            if (s_SetPipelineState.o_earlyHook || s_SetDescriptorHeaps.o_earlyHook ||
                s_SetComputeRootSignature.o_earlyHook || s_SetGraphicsRootSignature.o_earlyHook ||
'@.TrimEnd("`r", "`n") `
    -New @'
            if (o_ResourceBarrier || s_SetPipelineState.o_earlyHook || s_SetDescriptorHeaps.o_earlyHook ||
                s_SetComputeRootSignature.o_earlyHook || s_SetGraphicsRootSignature.o_earlyHook ||
'@.TrimEnd("`r", "`n") `
    -Label 'ResourceBarrier early hook condition'
Replace-LiteralOne -Path $d3d12Hooks `
    -Old @'
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (s_SetPipelineState.o_earlyHook != nullptr && extendedRestoreSignature)
'@.TrimEnd("`r", "`n") `
    -New @'
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                if (o_ResourceBarrier != nullptr)
                    DetourAttach(&(PVOID&) o_ResourceBarrier, hkResourceBarrier);

                if (s_SetPipelineState.o_earlyHook != nullptr && extendedRestoreSignature)
'@.TrimEnd("`r", "`n") `
    -Label 'ResourceBarrier detour attach'
Replace-LiteralOne -Path $d3d12Hooks `
    -Old @'
                    s_SetPipelineState.o_earlyHook = nullptr;
                    s_SetDescriptorHeaps.o_earlyHook = nullptr;
'@.TrimEnd("`r", "`n") `
    -New @'
                    o_ResourceBarrier = nullptr;
                    s_SetPipelineState.o_earlyHook = nullptr;
                    s_SetDescriptorHeaps.o_earlyHook = nullptr;
'@.TrimEnd("`r", "`n") `
    -Label 'ResourceBarrier failure reset'
Replace-LiteralOne -Path $d3d12Hooks `
    -Old @'
static void UnhookAll()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (s_SetComputeRootSignature.o_earlyHook != nullptr)
'@.TrimEnd("`r", "`n") `
    -New @'
static void UnhookAll()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_ResourceBarrier != nullptr)
    {
        DetourDetach(&(PVOID&) o_ResourceBarrier, hkResourceBarrier);
        o_ResourceBarrier = nullptr;
    }

    if (s_SetComputeRootSignature.o_earlyHook != nullptr)
'@.TrimEnd("`r", "`n") `
    -Label 'ResourceBarrier unhook'





$checks = @(
    @{ path=$scanHeader; text='NoteBarriers'; label='safe scan barrier declaration' },
    @{ path=$scanHeader; text='BestAnchorValue'; label='manual anchor API' },
    @{ path=$scanInternal; text='readbackValid'; label='safe scan validity mask' },
    @{ path=$scanInternal; text='kMaxBufferCandidates = 48'; label='texture slot reserve' },
    @{ path=$scanInternal; text='kTextureDriveCeiling = 512.0f'; label='game-scaled exposure ceiling' },
    @{ path=$scanInternal; text='kMaxBufferSingleStep = 8.0f'; label='buffer-only spike threshold' },
    @{ path=$scanReadback; text='safe UAV-barrier capture active'; label='safe scan runtime marker' },
    @{ path=$scanReadback; text='rejected as junk'; label='junk rejection path' },
    @{ path=$scanReadback; text='R11ToFloat'; label='packed exposure decode' },
    @{ path=$scanLogic; text='locked validated candidate'; label='validated candidate lock' },
    @{ path=$scanLogic; text='BestAnchorValue'; label='manual anchor unlocked candidate path' },
    @{ path=$scanLogic; text='R11G11B10_FLOAT'; label='R11 exposure format admission' },
    @{ path=$scanLogic; text='BestManualAnchorCandidateLocked'; label='manual pre-lock selection' },
    @{ path=$scanAnchors; text='manual anchor accepted'; label='anchor acceptance logging' },
    @{ path=$menuInput; text='BestAnchorValue'; label='menu anchor lock bypass' },
    @{ path=$d3d12Hooks; text='hkResourceBarrier'; label='safe scan ResourceBarrier hook' }
)
foreach ($c in $checks) {
    if (-not ([IO.File]::ReadAllText($c.path).Contains($c.text))) { throw "Exposure scanner patch verification failed: $($c.label)" }
}
Write-Host 'Exposure ScannerFix2 variant applied.'
