#pragma once

#include <Util.h>

// Single source of truth for "is this the RDR2 + PureDark coexistence patch".
//
// Every hook / tracking site that needs to special-case RDR2 under PureDark should
// call this instead of re-deriving the executable-name comparison locally. Before
// this helper existed, the same `_wcsicmp(..., L"RDR2.exe")` check was duplicated
// across dllmain.cpp, D3D11_Hooks.cpp, D3D12_Hooks.cpp, Dxgi_Hooks.cpp,
// Streamline_Hooks.cpp, ResTrack_dx12.cpp and DlssNr_Dx12.cpp -- easy for one copy
// to drift from the others (e.g. if a store build ever ships under a different exe
// name) without anything catching it at build time.
//
// The executable path is fixed for the lifetime of the process, so this is safe to
// cache in a function-local static.
inline bool IsRdr2PureDarkCoexistence()
{
    static const bool isRdr2 = _wcsicmp(Util::ExePath().filename().c_str(), L"RDR2.exe") == 0;
    return isRdr2;
}
