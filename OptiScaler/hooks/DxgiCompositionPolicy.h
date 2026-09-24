#pragma once

#include <dxgi1_2.h>
#include <cwchar>

// Akane v1.5's known 720x1000 DirectComposition helper must retain the original DXGI
// swapchain. Do not exempt the game's normal chain or other composition overlays.
inline bool IsAkaneHelperComposition(const wchar_t* executableName, const DXGI_SWAP_CHAIN_DESC1* desc)
{
    return executableName != nullptr && desc != nullptr && desc->Width == 720 && desc->Height == 1000 &&
           _wcsicmp(executableName, L"tlou-ii.exe") == 0;
}
