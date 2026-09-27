#include "../OptiScaler/hooks/DlssgHookLifecycle.h"

#include <cassert>
#include <cstdio>

using namespace DlssgHookLifecycle;

struct Probe
{
    bool stale = false;
    bool pinOk = true;
    bool regionOk = true;
    bool readOk = true;
    ModuleIdentity live {};
    std::array<uint8_t, kPatchBytes> bytes {};
    unsigned staleChecks = 0;
    unsigned pins = 0;
    unsigned identityReads = 0;
    unsigned regionChecks = 0;
    unsigned reads = 0;
    unsigned releases = 0;

    bool GenerationStale(const ModuleIdentity&)
    {
        ++staleChecks;
        return stale;
    }

    bool Pin(uintptr_t)
    {
        ++pins;
        return pinOk;
    }

    ModuleIdentity PinnedIdentity()
    {
        ++identityReads;
        return live;
    }

    bool ReadableExecutable(uintptr_t, size_t)
    {
        ++regionChecks;
        return regionOk;
    }

    bool Read(uintptr_t, void* out, size_t count)
    {
        ++reads;
        if (!readOk)
            return false;
        std::memcpy(out, bytes.data(), count);
        return true;
    }

    void ReleasePin() { ++releases; }
};

static HookRecord Record()
{
    HookRecord record {};
    record.identity = { 0x100000, 7, 3, 0x65000, 0x12345678 };
    record.target = 0x101000;
    record.patchSize = 8;
    record.installed = true;
    for (size_t i = 0; i < record.patchSize; ++i)
        record.patchedBytes[i] = static_cast<uint8_t>(0xA0 + i);
    return record;
}

int main()
{
    // Same base, different generation: stale before pin/query/read; detach must never be called.
    {
        auto record = Record();
        ModuleIdentity replacement = record.identity;
        replacement.generation = 8;
        Probe probe {};
        probe.live = record.identity;
        probe.bytes = record.patchedBytes;

        const auto decision = ValidateForDetach(record, &replacement, probe);
        assert(decision == DetachDecision::DiscardGenerationMismatch);
        assert(probe.staleChecks == 0 && probe.pins == 0 && probe.regionChecks == 0 && probe.reads == 0);
        unsigned detachCalls = 0;
        if (decision == DetachDecision::Detach)
            ++detachCalls;
        assert(detachCalls == 0);
    }

    // Same generation, same identity, intact captured Detours bytes: normal detach.
    {
        auto record = Record();
        ModuleIdentity replacement = record.identity;
        Probe probe {};
        probe.live = record.identity;
        probe.bytes = record.patchedBytes;

        const auto decision = ValidateForDetach(record, &replacement, probe);
        assert(decision == DetachDecision::Detach);
        assert(probe.staleChecks == 1 && probe.pins == 1 && probe.identityReads == 1);
        assert(probe.regionChecks == 1 && probe.reads == 1 && probe.releases == 0);
        probe.ReleasePin();
        assert(probe.releases == 1);
        assert(ApplyDetachResult(record, true));
        assert(!record.installed);
    }

    // Same generation/base but a different PE identity: discard after pin/identity,
    // before VirtualQuery or reading the target patch bytes.
    {
        auto record = Record();
        Probe probe {};
        probe.live = record.identity;
        ++probe.live.timestamp;
        probe.bytes = record.patchedBytes;

        const auto decision = ValidateForDetach(record, nullptr, probe);
        assert(decision == DetachDecision::DiscardIdentityMismatch);
        assert(probe.pins == 1 && probe.identityReads == 1);
        assert(probe.regionChecks == 0 && probe.reads == 0 && probe.releases == 1);
    }

    // Unmapped target: pin fails, so there is no VirtualQuery/read attempt.
    {
        auto record = Record();
        Probe probe {};
        probe.pinOk = false;
        const auto decision = ValidateForDetach(record, nullptr, probe);
        assert(decision == DetachDecision::DiscardPinFailed);
        assert(probe.pins == 1 && probe.identityReads == 0 && probe.regionChecks == 0 && probe.reads == 0);
    }

    // Same base/metadata but generation was atomically retired by the unload notification.
    {
        auto record = Record();
        Probe probe {};
        probe.stale = true;
        const auto decision = ValidateForDetach(record, nullptr, probe);
        assert(decision == DetachDecision::DiscardStaleGeneration);
        assert(probe.pins == 0 && probe.reads == 0);
    }

    // Genuine detach failure keeps the old hook record. A later retry can detach it;
    // the replacement stays unhooked until that succeeds.
    {
        auto record = Record();
        Probe first {};
        first.live = record.identity;
        first.bytes = record.patchedBytes;
        assert(ValidateForDetach(record, nullptr, first) == DetachDecision::Detach);

        unsigned newHookCalls = 0;
        const bool firstDetachSucceeded = false;
        first.ReleasePin();
        assert(!ApplyDetachResult(record, firstDetachSucceeded));
        if (!record.installed)
            ++newHookCalls;
        assert(record.installed && newHookCalls == 0);

        Probe retry {};
        retry.live = record.identity;
        retry.bytes = record.patchedBytes;
        assert(ValidateForDetach(record, nullptr, retry) == DetachDecision::Detach);
        retry.ReleasePin();
        assert(ApplyDetachResult(record, true));
        if (!record.installed)
            ++newHookCalls;
        assert(newHookCalls == 1);
    }

    std::puts("PASS: DLSS-G hook generation validation, stale discard, safe read ordering and detach retry");
    return 0;
}
