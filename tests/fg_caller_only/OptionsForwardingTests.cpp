#include "../../OptiScaler/hooks/DlssgOptionsForwarding.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main()
{
    const sl::DLSSGOptions defaults {};
    assert(defaults.structVersion == 5);

    for (std::size_t version = 1; version <= 5; ++version)
    {
        sl::DLSSGOptions caller {};
        sl::DLSSGOptions extension {};
        caller.next = &extension;
        caller.structVersion = version;
        caller.mode = sl::DLSSGMode::eOn;
        caller.numFramesToGenerate = 3;
        caller.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
        caller.numBackBuffers = 4;
        caller.bReserved15 = sl::eTrue;
        caller.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockNoClientQueues;
        caller.enableUserInterfaceRecomposition = sl::eTrue;
        caller.dynamicTargetFrameRate = 144.0f;

        sl::DLSSGOptions prepared {};
        assert(PrepareDlssgOptionsForForwarding(caller, prepared) == DlssgOptionsForwarding::Prepared);
        assert(caller.structVersion == version);
        assert(caller.dynamicTargetFrameRate == 144.0f);
        assert(prepared.structVersion == 5);
        assert(prepared.next == &extension);
        assert(prepared.mode == sl::DLSSGMode::eOn);
        assert(prepared.numFramesToGenerate == 3);
        assert(prepared.flags == sl::DLSSGFlags::eRetainResourcesWhenOff);
        assert(prepared.numBackBuffers == 4);
        assert(prepared.bReserved15 == (version >= 2 ? sl::eTrue : defaults.bReserved15));
        assert(prepared.queueParallelismMode ==
               (version >= 3 ? sl::DLSSGQueueParallelismMode::eBlockNoClientQueues
                             : defaults.queueParallelismMode));
        assert(prepared.enableUserInterfaceRecomposition ==
               (version >= 4 ? sl::eTrue : defaults.enableUserInterfaceRecomposition));
        assert(prepared.dynamicTargetFrameRate == (version >= 5 ? 144.0f : defaults.dynamicTargetFrameRate));
        std::printf("PASS known DLSSGOptions v%zu -> v5\n", version);
    }

    // Future layouts may include fields our headers do not know. The hook must
    // forward the original object, not a truncated copy of this common prefix.
    struct FutureOptions
    {
        sl::DLSSGOptions known;
        uint64_t extensionMarker = 0x8c719b462d50e3a4ULL;
    } future;
    future.known.structVersion = 6;
    future.known.numFramesToGenerate = 5;
    sl::DLSSGOptions untouched {};
    assert(PrepareDlssgOptionsForForwarding(future.known, untouched) == DlssgOptionsForwarding::Passthrough);
    assert(untouched.structVersion == defaults.structVersion);
    assert(untouched.numFramesToGenerate == defaults.numFramesToGenerate);
    assert(future.known.structVersion == 6);
    assert(future.extensionMarker == 0x8c719b462d50e3a4ULL);
    std::puts("PASS unknown v6 passthrough classification");

    sl::DLSSGOptions invalid {};
    invalid.structVersion = 0;
    assert(PrepareDlssgOptionsForForwarding(invalid, untouched) == DlssgOptionsForwarding::Invalid);
    assert(untouched.structVersion == defaults.structVersion);
    std::puts("PASS invalid v0 refusal");
    return 0;
}
