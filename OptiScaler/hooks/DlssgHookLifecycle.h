#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace DlssgHookLifecycle
{
inline constexpr size_t kPatchBytes = 16;

struct ModuleIdentity
{
    uintptr_t module = 0;
    uint64_t generation = 0;
    uint32_t slot = UINT32_MAX;
    size_t imageSize = 0;
    uint32_t timestamp = 0;

    bool operator==(const ModuleIdentity&) const = default;
};

struct HookRecord
{
    ModuleIdentity identity {};
    uintptr_t target = 0;
    std::array<uint8_t, kPatchBytes> patchedBytes {};
    uint8_t patchSize = 0;
    bool installed = false;

    void Clear() { *this = {}; }
};

enum class DetachDecision : uint8_t
{
    Detach,
    DiscardGenerationMismatch,
    DiscardStaleGeneration,
    DiscardPinFailed,
    DiscardIdentityMismatch,
    DiscardUnreadableTarget,
    DiscardPatchMismatch,
};

inline constexpr const char* DecisionName(DetachDecision decision)
{
    switch (decision)
    {
    case DetachDecision::Detach:
        return "detach";
    case DetachDecision::DiscardGenerationMismatch:
        return "generation-mismatch";
    case DetachDecision::DiscardStaleGeneration:
        return "stale-generation";
    case DetachDecision::DiscardPinFailed:
        return "pin-failed";
    case DetachDecision::DiscardIdentityMismatch:
        return "identity-mismatch";
    case DetachDecision::DiscardUnreadableTarget:
        return "unreadable-target";
    case DetachDecision::DiscardPatchMismatch:
        return "patch-mismatch";
    }
    return "unknown";
}

// Probe contract:
//   bool GenerationStale(const ModuleIdentity&)
//   bool Pin(uintptr_t target)
//   ModuleIdentity PinnedIdentity()
//   bool ReadableExecutable(uintptr_t target, size_t bytes)
//   bool Read(uintptr_t target, void* bytes, size_t count)
//   void ReleasePin()
//
// A Detach result deliberately leaves the pin held. The caller must keep that
// reference until DetourDetach has committed, then call ReleasePin(). Every
// discard path releases any pin before returning.
template <typename Probe>
DetachDecision ValidateForDetach(const HookRecord& record, const ModuleIdentity* replacement, Probe& probe)
{
    // Same-base replacement with a different generation is stale by definition.
    // This check is intentionally before pinning, VirtualQuery or any byte read.
    if (replacement != nullptr && replacement->module == record.identity.module &&
        replacement->generation != record.identity.generation)
        return DetachDecision::DiscardGenerationMismatch;

    if (probe.GenerationStale(record.identity))
        return DetachDecision::DiscardStaleGeneration;

    if (!probe.Pin(record.target))
        return DetachDecision::DiscardPinFailed;

    const auto releaseAnd = [&](DetachDecision decision)
    {
        probe.ReleasePin();
        return decision;
    };

    if (!(probe.PinnedIdentity() == record.identity))
        return releaseAnd(DetachDecision::DiscardIdentityMismatch);

    if (record.patchSize == 0 || record.patchSize > record.patchedBytes.size() ||
        !probe.ReadableExecutable(record.target, record.patchSize))
        return releaseAnd(DetachDecision::DiscardUnreadableTarget);

    std::array<uint8_t, kPatchBytes> observed {};
    if (!probe.Read(record.target, observed.data(), record.patchSize))
        return releaseAnd(DetachDecision::DiscardUnreadableTarget);

    if (std::memcmp(observed.data(), record.patchedBytes.data(), record.patchSize) != 0)
        return releaseAnd(DetachDecision::DiscardPatchMismatch);

    return DetachDecision::Detach;
}

inline bool ApplyDetachResult(HookRecord& record, bool detachSucceeded)
{
    if (detachSucceeded)
        record.Clear();
    return detachSucceeded;
}
} // namespace DlssgHookLifecycle
