#pragma once

#include <Util.h>

// Single source of truth for the separate RDR2 + PureDark coexistence build.
// PureDark owns DXGI/Streamline/Reflex/presentation/FG; OptiScaler keeps NGX
// SR/NR, device-level D3D12 hooks and the narrow DLSS-G count/MFG bridge.
inline bool IsRdr2PureDarkCoexistence()
{
    static const bool isRdr2 = _wcsicmp(Util::ExePath().filename().c_str(), L"RDR2.exe") == 0;
    return isRdr2;
}
