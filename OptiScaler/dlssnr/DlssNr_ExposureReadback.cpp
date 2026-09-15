#include "pch.h"

#include "DlssNr_ExposureScan_Internal.h"

#include <Config.h>
#include <Util.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

namespace DlssNr::ExposureScan
{
namespace Detail
{
ScanState g_scan;
std::mutex g_scanMutex;
std::mutex g_tickMutex;
// Prevent our injected UAV->COPY_SOURCE->UAV barriers recursively re-entering the capture hook.
thread_local bool g_scanBarrierInjection = false;
float HalfToFloat(uint16_t h)
{
    const uint32_t sign = (uint32_t) (h & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1Fu;
    uint32_t mantissa = h & 0x3FFu;

    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            const uint32_t bits = sign;
            float out;
            std::memcpy(&out, &bits, sizeof(out));
            return out;
        }

        // Subnormal: normalise it by hand.
        exponent = 1;

        while ((mantissa & 0x400u) == 0)
        {
            mantissa <<= 1;
            --exponent;
        }

        mantissa &= 0x3FFu;
    }
    else if (exponent == 31)
    {
        const uint32_t bits = sign | 0x7F800000u | (mantissa << 13);
        float out;
        std::memcpy(&out, &bits, sizeof(out));
        return out;
    }

    const uint32_t bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    float out;
    std::memcpy(&out, &bits, sizeof(out));
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
             D3D12_RESOURCE_STATES to)
{
    if (res == nullptr || from == to)
        return;

    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = res;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = from;
    barrier.Transition.StateAfter = to;
    cmdList->ResourceBarrier(1, &barrier);
}

bool EnsureReadback(ID3D12Device* device)
{
    for (unsigned int i = 0; i < kSlots; ++i)
    {
        if (g_scan.readback[i] != nullptr)
            continue;

        D3D12_HEAP_PROPERTIES heap {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kStride * kMaxCandidates;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&g_scan.readback[i]))))
        {
            std::lock_guard<std::mutex> lock(g_scanMutex);
            g_scan.status = "could not allocate the readback buffers";
            return false;
        }
    }

    return true;
}

// Whether the scan should be running at all.
//
// Choosing it as the white point's source is the whole of the answer for anybody using this.
// The separate setting survives as a developer override, for the one case a user has no reason
// to want: watching the scan in a game that supplies a REAL exposure, so the two can be
// compared in the log. That is validation work, not a control, and it does not belong in a
// panel.
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
{
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
    }

    if (device == nullptr || cmdList == nullptr)
        return;

    std::lock_guard<std::mutex> tickLock(g_tickMutex);
    {
        std::lock_guard<std::mutex> lock(g_scanMutex);
        if (g_scan.device != nullptr && g_scan.device != device)
            return;
        if (submissionEpoch != UINT64_MAX && submissionEpoch == g_scan.lastEpoch)
            return;
        if (g_scan.device == nullptr)
        {
            g_scan.device = device;
            device->AddRef();
            // Discovery starts before a rendering device is known. Never record a copy of a
            // resource discovered on another device into this device's command list.
            std::erase_if(g_scan.tracked, [device](const Tracked& candidate)
            {
                if (candidate.device == device)
                    return false;
                candidate.resource->Release();
                return true;
            });
        }
    }

    // Outside g_scanMutex, deliberately. EnsureReadback creates a committed resource, and that call is
    // detoured to hkCreateCommittedResource -> NoteResource, which takes g_scanMutex. Holding it
    // here would be a self-deadlock. g_tickMutex serializes different NR owners and shutdown.
    if (!EnsureReadback(device))
        return;

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
    {
        g_scan.status = "no buffer in this game is shaped like an exposure";
        return;
    }
    g_scan.lastEpoch = submissionEpoch;

    // Barrier capture writes earlier in the frame. Read the slot which will be reused next frame;
    // with five slots it is four complete frames old and therefore not the slot the GPU is writing now.
    if (g_scan.frames >= kSlots)
    {
        const unsigned int readSlot = (g_scan.writeSlot + 1) % kSlots;
        ID3D12Resource* old = g_scan.readback[readSlot];
        const uint64_t validMask = g_scan.readbackValid[readSlot];
        void* mapped = nullptr;
        D3D12_RANGE range { 0, kStride * kMaxCandidates };

        if (old != nullptr && SUCCEEDED(old->Map(0, &range, &mapped)) && mapped != nullptr)
        {
            const unsigned char* base = (const unsigned char*) mapped;

            const auto readableCount = std::min(g_scan.tracked.size(), g_scan.readbackCounts[readSlot]);
            for (size_t i = 0; i < readableCount; ++i)
            {
                if ((validMask & (uint64_t(1) << i)) == 0)
                    continue; // never interpret an uncopied/skipped candidate as stale exposure data

                Tracked& t = g_scan.tracked[i];
                const unsigned char* at = base + i * kStride;

                float value = 0.0f;

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
            }

            D3D12_RANGE nothingWritten { 0, 0 };
            old->Unmap(0, &nothingWritten);
        }
    }

    // Periodic movement readout. The menu's Advanced panel shows which candidate tracks the light, but
    // the log did not -- so a game the scan is being taught (Cyberpunk) could not be cracked from a log
    // alone. Every ~300 frames, name the candidates that MOVE and their travel: the exposure is the one
    // that swings widely between bright and dark. Throttled, and only while the scan is wanted.
    if (g_scan.frames > 0 && g_scan.frames % 300 == 0)
    {
        unsigned int movers = 0;

        for (size_t i = 0; i < g_scan.tracked.size(); ++i)
        {
            const Tracked& t = g_scan.tracked[i];

            if (!t.moves)
                continue;

            movers++;
            LOG_INFO("DLSS-NR scan mover: candidate {} ({}) range {:.5f}..{:.5f} (x{:.1f}), latest {:.5f}",
                     (unsigned int) (i + 1), t.shape, t.lowest, t.highest,
                     t.lowest > kFloor ? t.highest / t.lowest : 0.0f, t.latest);
        }

        if (movers == 0)
            LOG_INFO("DLSS-NR scan: {} candidates tracked, none moving yet -- go between bright and dark",
                     (unsigned int) g_scan.tracked.size());
    }

    // Safe copies were recorded by NoteBarriers earlier in the frame. Retire the oldest slot and
    // make it the destination for the NEXT frame. No game-owned resource is touched from Tick().
    const unsigned int nextSlot = (g_scan.writeSlot + 1) % kSlots;
    g_scan.readbackCounts[nextSlot] = 0;
    g_scan.readbackValid[nextSlot] = 0;
    g_scan.readbackWriter[nextSlot] = nullptr;
    g_scan.writeSlot = nextSlot;
    g_scan.frames++;
    g_scan.status = "";
}

// Drop the scan's references to the resources it captured, WITHOUT touching our own readback buffers.
//
// The scan AddRef's every candidate it adopts (Adopt) but nothing ever released them -- Shutdown() has
// no callers -- so a Streamline/DLSS-D-owned resource that passes the filter is pinned by our stray
// AddRef, and when the driver frees its (placed) heap at feature teardown the surviving wrapper points
// at freed memory: the use-after-free that removed the device in Cyberpunk (a lock on a freed object in
// nvwgf2umx). Calling this at feature release drops our references first, so nothing we hold outlives
// the heap. Only the candidates (foreign resources) are released here -- NOT the readback ring, which
// is ours and may have GPU copies in flight; freeing that here would be a new hazard. Capture is gated
// on NR being ENABLED (not on the scan source), so this releases whatever was captured whenever NR is
// on -- scan selected or not; it is a no-op only when NR is off (nothing captured), so FSR/XeSS users
// with NR off pay nothing. The scan re-adopts candidates next frame.
void ReleaseTrackedResources()
{
    std::lock_guard<std::mutex> lock(g_scanMutex);

    for (Tracked& t : g_scan.tracked)
    {
        if (t.resource != nullptr)
            t.resource->Release();
    }

    g_scan.tracked.clear();
    g_scan.complained = false;
    // The ring may still contain copies of the old candidates. Do not interpret those values
    // as newly adopted resources that happen to occupy the same list positions.
    std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
    std::fill(std::begin(g_scan.readbackValid), std::end(g_scan.readbackValid), 0);
    std::fill(std::begin(g_scan.readbackWriter), std::end(g_scan.readbackWriter), nullptr);
}

void Shutdown()
{
    std::lock_guard<std::mutex> tickLock(g_tickMutex);
    std::lock_guard<std::mutex> lock(g_scanMutex);

    for (Tracked& t : g_scan.tracked)
    {
        if (t.resource != nullptr)
            t.resource->Release();
    }

    g_scan.tracked.clear();

    for (unsigned int i = 0; i < kSlots; ++i)
    {
        if (g_scan.readback[i] != nullptr)
        {
            g_scan.readback[i]->Release();
            g_scan.readback[i] = nullptr;
        }
    }

    g_scan.frames = 0;
    g_scan.writeSlot = 0;
    std::fill(std::begin(g_scan.readbackCounts), std::end(g_scan.readbackCounts), 0);
    std::fill(std::begin(g_scan.readbackValid), std::end(g_scan.readbackValid), 0);
    std::fill(std::begin(g_scan.readbackWriter), std::end(g_scan.readbackWriter), nullptr);
    g_scan.captureActive = false;
    g_scan.safeCaptureLogged = false;
    g_scan.lastEpoch = UINT64_MAX;
    if (g_scan.device != nullptr)
    {
        g_scan.device->Release();
        g_scan.device = nullptr;
    }
    g_scan.status = "not started";
}

}
