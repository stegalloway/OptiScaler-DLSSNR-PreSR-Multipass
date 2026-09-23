#pragma once

#include <sl_dlss_g.h>
#include <cstddef>
#include <cstring>

enum class DlssgOptionsForwarding
{
    Invalid,
    Prepared,
    Passthrough
};

// The destination must be distinct and freshly value-initialised. Copy only the bytes
// defined by the caller's ABI; v2/v4 trailing padding must not become a newer
// field. Unknown versions retain the caller's original storage in the hook.
inline DlssgOptionsForwarding PrepareDlssgOptionsForForwarding(const sl::DLSSGOptions& caller,
                                                               sl::DLSSGOptions& prepared)
{
    static_assert(sizeof(sl::DLSSGOptions) >= 120);
    std::size_t callerBytes = 0;
    switch (caller.structVersion)
    {
    case 1: callerBytes = 104; break;
    case 2: callerBytes = 108; break;
    case 3: callerBytes = 112; break;
    case 4: callerBytes = 116; break;
    case 5: callerBytes = 120; break;
    case 0: return DlssgOptionsForwarding::Invalid;
    default: return DlssgOptionsForwarding::Passthrough;
    }

    std::memcpy(&prepared, &caller, callerBytes);
    // The known-clean wrapper uses the current version for known layouts.
    // Value-initialisation supplied the defaults for fields absent in caller.
    prepared.structVersion = sl::DLSSGOptions {}.structVersion;
    return DlssgOptionsForwarding::Prepared;
}
