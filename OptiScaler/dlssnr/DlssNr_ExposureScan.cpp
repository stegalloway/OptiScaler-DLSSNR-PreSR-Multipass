#include "pch.h"

#include "DlssNr_ExposureScan_Internal.h"

#include <Config.h>
#include <Util.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

namespace DlssNr
{
namespace ExposureScan
{
namespace Detail
{

// Formats an exposure could plausibly be in: floating point, one or two channels.
//
// Two channels because eye adaptation commonly carries the value and something alongside it -- the
// previous frame's value, or a target it is easing toward. Anything wider is a picture rather than a
// number. Integer formats are excluded because an exposure is a scale and a normalised integer
// cannot hold one.
// outBytes is the size of the FIRST channel only -- that is the one texel this reads back, and a
// two-channel format that stored 4 or 8 there would be decoded as the wrong type. The copy uses the
// real format (outFormat) so it matches the source texture; the read uses outBytes.
bool PlausibleFormat(DXGI_FORMAT f, unsigned int* outBytes, const char** outName, DXGI_FORMAT* outFormat)
{
    *outFormat = f;
    switch (f)
    {
    case DXGI_FORMAT_R32_FLOAT:
        *outBytes = 4;
        *outName = "R32_FLOAT";
        return true;
    case DXGI_FORMAT_R16_FLOAT:
        *outBytes = 2;
        *outName = "R16_FLOAT";
        return true;
    case DXGI_FORMAT_R32G32_FLOAT:
        *outBytes = 4;
        *outName = "R32G32_FLOAT";
        return true;
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
        return false;
    }
}

bool Wanted()
{
    return Config::Instance()->DlssNrWhitePointSource.value_or_default() == 2 ||
           Config::Instance()->DlssNrScanExposure.value_or_default();
}

} // namespace Detail
using namespace Detail;

// Everything the two entry points share: does this description look like a number rather than a
// picture, and if so what is it.
//
// Widened from the first attempt, which asked for at most 4x4 and one or two channels and found
// nothing anywhere. That was tuned on what an exposure buffer ought to look like rather than on what
// engines actually allocate: some keep a small histogram beside the value, some keep a few frames of
// history, and some put the whole thing in a four-channel texture and use one channel. The filter
// only has to be tight enough that the list stays readable.
bool LooksLikeANumber(const D3D12_RESOURCE_DESC& rd, std::string* outShape, unsigned int* outBytes, bool* outIsBuffer,
                      DXGI_FORMAT* outFormat)
{
    // An exposure is computed, so it is written by a shader. This is the one condition worth being
    // strict about: it removes almost everything without removing anything that could be the answer.
    if ((rd.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
        return false;

    if (rd.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        // 256 texels rather than 16. A 16x16 texture is still a number by any reasonable measure and
        // a 256-bin histogram is exactly how a lot of engines compute one.
        if (rd.Width * rd.Height > 256 || rd.Width == 0 || rd.Height == 0)
            return false;

        const char* name = nullptr;

        if (!PlausibleFormat(rd.Format, outBytes, &name, outFormat))
            return false;

        *outIsBuffer = false;
        *outShape = std::to_string((unsigned int) rd.Width) + "x" + std::to_string(rd.Height) + " " + name;
        return true;
    }

    if (rd.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        *outFormat = DXGI_FORMAT_UNKNOWN;

        // Unreal moved eye adaptation off a texture and onto a buffer, so buffers have to be in
        // scope or a whole engine's worth of games is invisible.
        //
        // 128 bytes, down from 4kB. The wider bound filled all twelve slots in Nioh 3 with 256, 512
        // and 768 byte buffers that never held anything but zero, and the real answer -- eight bytes
        // -- only made the list because it happened to be created early. A cap that can be filled by
        // junk is a cap that can hide the answer.
        if (rd.Width == 0 || rd.Width > 128)
            return false;

        *outIsBuffer = true;
        *outBytes = 4;
        *outShape = "buffer, " + std::to_string((unsigned int) rd.Width) + " bytes";
        return true;
    }

    return false;
}

void Adopt(ID3D12Resource* resource, const std::string& shape, unsigned int bytes, bool isBuffer, DXGI_FORMAT texFormat)
{
    ID3D12Device* resourceDevice = nullptr;
    if (FAILED(resource->GetDevice(IID_PPV_ARGS(&resourceDevice))) || resourceDevice == nullptr)
        return;
    const bool differentDevice = g_scan.device != nullptr && g_scan.device != resourceDevice;
    resourceDevice->Release();
    if (differentDevice)
        return;

    for (const Tracked& t : g_scan.tracked)
    {
        if (t.resource == resource)
            return;
    }

    const size_t bufferCandidates =
        std::count_if(g_scan.tracked.begin(), g_scan.tracked.end(), [](const Tracked& t) { return t.isBuffer; });
    if (isBuffer && bufferCandidates >= kMaxBufferCandidates)
    {
        if (!g_scan.complained)
        {
            g_scan.complained = true;
            LOG_WARN("DLSS-NR exposure scan: generic buffer reserve reached at {}; keeping {} slots available for "
                     "float textures",
                     kMaxBufferCandidates, kMaxCandidates - kMaxBufferCandidates);
        }
        return;
    }

    if (g_scan.tracked.size() >= kMaxCandidates)
        return;

    Tracked t;
    t.resource = resource;
    t.device = resourceDevice;
    t.shape = shape;
    t.isBuffer = isBuffer;
    t.bytes = bytes;
    t.texFormat = texFormat;
    resource->AddRef();

    g_scan.tracked.push_back(t);

    LOG_INFO("DLSS-NR exposure scan: candidate {} -- {}", g_scan.tracked.size(), shape);
}

void NoteResource(const D3D12_RESOURCE_DESC* desc, ID3D12Resource* resource)
{
    if (!Config::Instance()->DlssNrEnabled.value_or_default())
        return;

    if (desc == nullptr || resource == nullptr)
        return;

    std::string shape;
    unsigned int bytes = 4;
    bool isBuffer = false;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;

    std::lock_guard<std::mutex> lock(g_scanMutex);
    g_scan.examined++;

    if (!LooksLikeANumber(*desc, &shape, &bytes, &isBuffer, &fmt))
    {
        // Near-miss diagnostic. A resource a shader writes (UAV) that the filter rejected: logging its
        // shape -- bounded to the first 40 so it cannot flood -- reveals whether a game the scan finds
        // nothing in (Cyberpunk 2077) has an exposure the filter narrowly misses (a small UAV texture
        // in an unlisted format, or a UAV buffer just over 128 bytes -> widen precisely to match) or
        // nothing scannable at all (only large buffers/textures -> the exposure is baked in a bigger
        // buffer and no filter change can help). Read these against Examined() in the log.
        if ((desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 && g_scan.nearMissLogged < 40)
        {
            g_scan.nearMissLogged++;
            LOG_INFO("DLSS-NR scan near-miss #{}: UAV dim {} {}x{}x{} fmt {} (filter rejected)", g_scan.nearMissLogged,
                     (int) desc->Dimension, (unsigned int) desc->Width, desc->Height, desc->DepthOrArraySize,
                     (int) desc->Format);
        }

        return;
    }

    Adopt(resource, shape, bytes, isBuffer, fmt);
}

unsigned int Examined()
{
    std::lock_guard<std::mutex> lock(g_scanMutex);
    return g_scan.examined;
}

void NoteUav(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc)
{
    // Deliberately NOT gated on the scan setting, and that was a real bug rather than a nicety.
    //
    // An engine creates its eye adaptation view once, when it builds its render targets, which is
    // long before anybody opens a menu and ticks a box. Gating the recording meant every candidate
    // was thrown away before the scan could want it, and the readout then said "nothing matched
    // yet -- play for a few seconds", which is advice that could never come true no matter how long
    // anyone played.
    //
    // Recording is a resource description and a pointer. What is genuinely risky -- reading a buffer
    // the game owns, on an assumption about its state -- lives in Tick, and that is still gated.
    if (!Config::Instance()->DlssNrEnabled.value_or_default())
        return;

    if (resource == nullptr)
        return;

    const D3D12_RESOURCE_DESC rd = resource->GetDesc();

    std::string shape;
    unsigned int bytes = 4;
    bool isBuffer = false;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;

    std::lock_guard<std::mutex> lock(g_scanMutex);
    g_scan.examined++;

    if (!LooksLikeANumber(rd, &shape, &bytes, &isBuffer, &fmt))
        return;

    Adopt(resource, shape, bytes, isBuffer, fmt);
}

// How many frames of watching without a validated source before saying so.
constexpr unsigned int kPatience = 1800;

static float CandidateDriveCeiling(const Tracked& t) { return t.isBuffer ? kBufferDriveCeiling : kTextureDriveCeiling; }

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
    LOG_INFO("DLSS-NR exposure scan: locked validated candidate {} ({}) score {:.2f}, range {:.5f}..{:.5f}", best + 1,
             t.shape, CandidateConfidence(t), t.lowest, t.highest);
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
    case Verdict::Off:
        line = "";
        break;
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
        if (best < 0)
        {
            line = "DLSS-NR exposure scan: validating candidates";
            break;
        }
        const Tracked& t = g_scan.tracked[best];
        const float ratio = t.highest / std::max(t.lowest, kDriveFloor);
        char buf[208];
        snprintf(buf, sizeof(buf),
                 "DLSS-NR exposure scan: VALIDATED -- candidate %d = %.5f  (%.5f..%.5f, x%.1f)  locked", best + 1,
                 t.latest, t.lowest, t.highest, ratio);
        line = buf;
        break;
    }
    case Verdict::Barren:
        line = "DLSS-NR exposure scan: no validated exposure source found.";
        break;
    }
    return line.c_str();
}

float BestValue(int* outIndex, float* outLowest, float* outHighest)
{
    std::lock_guard<std::mutex> lock(g_scanMutex);
    const int best = SelectCandidateLocked();
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
    if (outIndex != nullptr)
        *outIndex = best + 1;
    if (outLowest != nullptr)
        *outLowest = g_scan.tracked[best].lowest;
    if (outHighest != nullptr)
        *outHighest = g_scan.tracked[best].highest;
    return g_scan.tracked[best].latest;
}

std::vector<Candidate> Report()
{
    std::lock_guard<std::mutex> lock(g_scanMutex);

    std::vector<Candidate> out;
    out.reserve(g_scan.tracked.size());

    for (const Tracked& t : g_scan.tracked)
    {
        Candidate c;
        c.shape = t.shape;
        c.latest = t.latest;
        c.lowest = t.lowest;
        c.highest = t.highest;
        c.reads = t.reads;
        c.moves = t.moves;
        out.push_back(c);
    }

    return out;
}

const char* Status()
{
    std::lock_guard<std::mutex> lock(g_scanMutex);
    return g_scan.status;
}

bool Scanning() { return Wanted(); }

} // namespace ExposureScan
} // namespace DlssNr
