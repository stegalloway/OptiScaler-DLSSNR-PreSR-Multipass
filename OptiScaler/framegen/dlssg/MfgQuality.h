// OptiScaler host adapter for the pinned Reno 1.0 legacy and 1.1.5 adaptive
// quality profiles. Discovery and PTX construction only: MfgUnlock owns the
// one rollback-safe gate/kernel/descriptor transaction and provider lifetime.
#pragma once
#include "mfgquality/blackwell.hpp"
#include "mfgquality/thin_geometry.hpp"
#include "MfgMagnitudeProfile.h"

namespace MfgQuality
{
struct Options
{
    int mode = 0; // 0 existing, 1 framework, 2 Balanced, 3 Aggressive, 4 Adaptive 1.1.5, 5 magnitude-only
    bool warp = false;
    int magnitudeThresholdPx = MfgMagnitude::kDefaultDisplayThresholdPx;
    bool operator==(const Options&) const = default;
};

struct Write
{
    uint8_t* address = nullptr;
    std::vector<uint8_t> replacement;
};

inline Write MakeWrite(void* address, std::vector<uint8_t> replacement)
{
    return {static_cast<uint8_t*>(address), std::move(replacement)};
}

struct Result
{
    std::string detail;
    // Only MfgUnlock may commit writes and retain the provider. This allocation
    // remains live after success or incomplete rollback, and is freed only when
    // no descriptor could still reference it.
    void* warpAllocation = nullptr;
    std::vector<Write> writes;
};

// Preserve the upstream suffix/metadata, but make new warp PTX the ONLY Ada
// choice. Otherwise CUDA prefers the preserved original sm_89 cubin.
inline bool SelectWarpPtxForAda(std::vector<uint8_t>& fatbin, std::string& detail)
{
    namespace t = mfgunlock::thingeometry::internal;
    size_t replacement = 0;
    std::vector<size_t> superseded;
    size_t cursor = 16;
    while (cursor + 64 <= fatbin.size())
    {
        const auto header = t::ReadU32(fatbin.data() + cursor + 4);
        const auto bytes = t::ReadU64(fatbin.data() + cursor + 8);
        if (header < 64 || header > fatbin.size() - cursor || bytes > fatbin.size() - cursor - header)
        { detail = "invalid rebuilt warp entry"; return false; }
        if (t::ReadU32(fatbin.data() + cursor + 28) == 89)
        {
            if (t::ReadU16(fatbin.data() + cursor) == 1 && t::ReadU32(fatbin.data() + cursor + 16) == 0)
            {
                const auto* payload = reinterpret_cast<const char*>(fatbin.data() + cursor + header);
                const std::string text(payload, static_cast<size_t>(bytes));
                if (replacement || text.find("MFGUNLOCK_VALIDATED_WARP_BLEND_V1") == std::string::npos)
                { detail = "ambiguous rebuilt warp PTX"; return false; }
                replacement = cursor;
            }
            else superseded.push_back(cursor);
        }
        cursor += header + static_cast<size_t>(bytes);
    }
    if (cursor != fatbin.size() || !replacement || superseded.size() != 2)
    { detail = "unexpected warp architecture layout; no changes"; return false; }
    const uint32_t parked = 122; // Non-Ada parking, as in the existing MFG backend.
    for (auto entry : superseded) std::memcpy(fatbin.data() + entry + 28, &parked, sizeof(parked));
    return true;
}

inline bool Prepare(HMODULE module, Options options, std::vector<Write> gates, Result& result)
{
    namespace bw = mfgunlock::blackwell;
    namespace tg = mfgunlock::thingeometry;
    if (options.mode < 1 || options.mode > 5)
    { result.detail = "experimental backend not selected"; return false; }
    if (options.mode == 5 && !MfgMagnitude::IsValidDisplayThresholdPx(options.magnitudeThresholdPx))
    { result.detail = "unsupported calibrated magnitude threshold"; return false; }
    std::string version;
    if (!tg::IsSupportedProvider(module, version, result.detail) || version != "310.9.1")
    { result.detail = "only exact 310.9.1 provider supported: " + result.detail; return false; }
#if !MFGUNLOCK_HAS_GENERATED_BLACKWELL_CUBINS || !MFGUNLOCK_HAS_GENERATED_THIN_GEOMETRY_CUBINS
    (void)gates;
    result.detail = "required locally generated kernel tables absent; experimental backend disabled";
    return false;
#else
    std::vector<bw::internal::Candidate> candidates;
    if (!bw::internal::CollectCandidates(module, candidates, result.detail)) return false;
    unsigned roles[3] {};
    for (const auto& candidate : candidates)
    {
        const auto role = static_cast<unsigned>(candidate.role);
        if (role < 1 || role > 3) { result.detail = "unknown kernel role"; return false; }
        ++roles[role - 1];
    }
    if (candidates.size() != 3 || roles[0] != 1 || roles[1] != 1 || roles[2] != 1)
    { result.detail = "expected one unmodified kernel per framework role"; return false; }
    auto plan = std::move(gates);
    for (const auto& candidate : candidates)
    {
        if (bw::internal::Fnv1a64(candidate.payload, candidate.slot_size) != candidate.replacement->source_fnv1a64)
        { result.detail = "source cubin hash mismatch (modified/unsupported provider); no changes"; return false; }

        // Magnitude-only is intentionally a one-kernel experiment. The frozen
        // specification leaves native inpaint and inpaint-decision roles
        // untouched; retargeting them would turn this into a framework profile.
        if (options.mode == 5 && candidate.role != bw::KernelRole::MotionVector)
            continue;

        const uint8_t* data = candidate.replacement->data;
        size_t size = candidate.replacement->size;
        if (candidate.role == bw::KernelRole::MotionVector && options.mode == 5)
        {
#if MFGUNLOCK_HAS_GENERATED_MAGNITUDE_CUBINS
            const bw::internal::ElfFingerprint fp {candidate.replacement->text, candidate.replacement->shared,
                                                   candidate.replacement->regs};
            const auto* variant = bw::internal::MatchMagnitudeVariant(
                fp, candidate.payload, candidate.slot_size,
                static_cast<unsigned int>(options.magnitudeThresholdPx));
            if (!variant)
            { result.detail = "requested calibrated magnitude variant missing; no fallback substitution"; return false; }
            data = variant->data;
            size = variant->size;
#else
            result.detail = "local calibrated magnitude table absent; magnitude mode disabled";
            return false;
#endif
        }
        else if (candidate.role == bw::KernelRole::MotionVector && options.mode >= 2)
        {
            const bw::internal::ElfFingerprint fp {candidate.replacement->text, candidate.replacement->shared,
                                                   candidate.replacement->regs};
            const auto mechanism = options.mode == 4 ? "adaptive_quality_geometry_v1"
                                 : options.mode == 2 ? "geometry_motion_depth"
                                                     : "geometry_motion_depth_aggressive";
            const auto* variant = bw::internal::MatchScatterVariant(fp, candidate.payload, candidate.slot_size, mechanism);
            if (!variant) { result.detail = "requested boundary variant missing; no fallback substitution"; return false; }
            data = variant->data;
            size = variant->size;
        }
        if (candidate.role == bw::KernelRole::InpaintDecision && options.mode == 4)
        {
            const bw::internal::ElfFingerprint fp {candidate.replacement->text, candidate.replacement->shared,
                                                   candidate.replacement->regs};
            const auto* variant = bw::internal::MatchScatterVariant(
                fp, candidate.payload, candidate.slot_size, "adaptive_inpaint_decision_v1");
            if (!variant) { result.detail = "requested adaptive inpaint variant missing; no fallback substitution"; return false; }
            data = variant->data;
            size = variant->size;
        }
        if (size > candidate.slot_size)
        { result.detail = "requested quality kernel exceeds verified slot"; return false; }
        std::vector<uint8_t> replacement(candidate.slot_size, 0);
        std::memcpy(replacement.data(), data, size);
        plan.push_back(MakeWrite(candidate.payload, std::move(replacement)));
    }
    const bool legacyWarp = options.warp && options.mode >= 1 && options.mode <= 3;
    if (legacyWarp || options.mode == 4)
    {
        const auto* profile = tg::internal::ProfileFor(tg::Mechanism::ValidatedWarpBlend);
        tg::internal::LocatedFatbin located;
        std::vector<uint8_t> rebuilt;
        if (!profile || !tg::internal::LocateUniqueFatbin(module, *profile, located, result.detail)) return false;
        // The 1.1.5 profile coordinates smooth border confidence and
        // forward/inverse candidate arbitration. Do not stack the legacy warp
        // PTX over it or leave its process-global selection set after planning.
        const bool previousAdaptive = tg::g_adaptive_quality_enabled;
        tg::g_adaptive_quality_enabled = options.mode == 4;
        const bool rebuiltOk = tg::internal::BuildRedirectedFatbin(
            located.address, located.size, *profile, rebuilt, result.detail);
        tg::g_adaptive_quality_enabled = previousAdaptive;
        if (!rebuiltOk) return false;
        if (!SelectWarpPtxForAda(rebuilt, result.detail)) return false;
        auto* base = reinterpret_cast<uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        const uint64_t expected = reinterpret_cast<uint64_t>(located.address);
        std::vector<uint8_t*> slots;
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const auto& section = sections[i];
            if (!(section.Characteristics & IMAGE_SCN_MEM_READ) || (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) ||
                section.VirtualAddress >= nt->OptionalHeader.SizeOfImage) continue;
            const size_t bytes = std::min<size_t>(section.Misc.VirtualSize, nt->OptionalHeader.SizeOfImage - section.VirtualAddress);
            for (size_t offset = 0; offset + sizeof(uint64_t) <= bytes; offset += alignof(uint64_t))
            {
                auto* address = base + section.VirtualAddress + offset;
                if (tg::internal::ReadU64(address) == expected) slots.push_back(address);
            }
        }
        if (slots.empty() || slots.size() > 32)
        { result.detail = "warp descriptor count outside 1..32; no changes"; return false; }
        result.warpAllocation = VirtualAlloc(nullptr, rebuilt.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!result.warpAllocation) { result.detail = "warp allocation failed"; return false; }
        std::memcpy(result.warpAllocation, rebuilt.data(), rebuilt.size());
        const auto pointer = reinterpret_cast<uint64_t>(result.warpAllocation);
        std::vector<uint8_t> value(sizeof(pointer));
        std::memcpy(value.data(), &pointer, sizeof(pointer));
        for (auto* slot : slots) plan.push_back(MakeWrite(slot, value));
    }
    result.writes = std::move(plan);
    return true;
#endif
}

inline void DiscardUncommitted(Result& result)
{
    if (result.warpAllocation) VirtualFree(result.warpAllocation, 0, MEM_RELEASE);
    result.warpAllocation = nullptr;
    result.writes.clear();
}
} // namespace MfgQuality
