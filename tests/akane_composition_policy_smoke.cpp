#include <windows.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <stdexcept>
#include "../OptiScaler/hooks/DxgiCompositionPolicy.h"

static void expect(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main() try
{
    DXGI_SWAP_CHAIN_DESC1 desc {};
    desc.Width = 720;
    desc.Height = 1000;
    expect(IsAkaneHelperComposition(L"tlou-ii.exe", &desc), "Akane's measured helper was rejected");
    expect(IsAkaneHelperComposition(L"TLOU-II.EXE", &desc), "executable match is not case-insensitive");
    expect(!IsAkaneHelperComposition(L"tlou-ii-l.exe", &desc), "unvalidated alternate executable was exempted");
    expect(!IsAkaneHelperComposition(L"MilesMorales.exe", &desc), "another game was exempted");
    expect(!IsAkaneHelperComposition(nullptr, &desc), "null executable was exempted");
    expect(!IsAkaneHelperComposition(L"tlou-ii.exe", nullptr), "null descriptor was exempted");
    desc.Width = 721;
    expect(!IsAkaneHelperComposition(L"tlou-ii.exe", &desc), "other width was exempted");
    desc.Width = 720;
    desc.Height = 999;
    expect(!IsAkaneHelperComposition(L"tlou-ii.exe", &desc), "other height was exempted");
    desc.Width = 3440;
    desc.Height = 1440;
    expect(!IsAkaneHelperComposition(L"tlou-ii.exe", &desc), "game-sized chain was exempted");
    std::puts("PASS: exact TLOU2 Akane composition exception; all other compositions retain normal routing");
    return 0;
}
catch (const std::exception& e)
{
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
