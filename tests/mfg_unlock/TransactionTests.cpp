// Production transaction helper against three synthetic gate/descriptor pages.
// No NVIDIA module is loaded or modified.
#include <windows.h>
#include <cstdint>
#include <stdexcept>
#include <vector>

static int g_protectCalls = 0;
static int g_failProtectAt = 0;
static int g_failProtectAt2 = 0;
static int g_flushCalls = 0;
static int g_failFlushAt = 0;

static BOOL TestVirtualProtect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous)
{
    ++g_protectCalls;
    if (g_protectCalls == g_failProtectAt || g_protectCalls == g_failProtectAt2)
        return FALSE;
    return VirtualProtect(address, size, protection, previous);
}

static BOOL TestFlushInstructionCache(HANDLE process, LPCVOID address, SIZE_T size)
{
    ++g_flushCalls;
    if (g_flushCalls == g_failFlushAt)
        return FALSE;
    return FlushInstructionCache(process, address, size);
}

#define LOG_WARN(...) ((void)0)
#define VirtualProtect TestVirtualProtect
#define FlushInstructionCache TestFlushInstructionCache
#include "../../OptiScaler/framegen/dlssg/MfgUnlockTransaction.h"
#undef FlushInstructionCache
#undef VirtualProtect
#undef LOG_WARN

using MfgUnlock::Transaction::AddPatch;
using MfgUnlock::Transaction::ApplyTransaction;
using MfgUnlock::Transaction::Patch;
using MfgUnlock::Transaction::TransactionResult;

static void Check(bool value, const char* reason)
{
    if (!value)
        throw std::runtime_error(reason);
}

static DWORD ProtectionAt(const void* address)
{
    MEMORY_BASIC_INFORMATION info {};
    Check(VirtualQuery(address, &info, sizeof(info)) == sizeof(info), "VirtualQuery failed");
    return info.Protect;
}

static void RunCase(int failProtectAt, int failProtectAt2, int failFlushAt,
                    TransactionResult expected, bool expectOriginal)
{
    SYSTEM_INFO systemInfo {};
    GetSystemInfo(&systemInfo);
    const size_t page = systemInfo.dwPageSize;
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 3, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(memory != nullptr, "allocation failed");

    const uint8_t original[] = { 1, 2, 3 };
    const uint8_t replacement[] = { 5, 6, 7 };
    std::vector<Patch> plan;
    for (size_t i = 0; i < 3; ++i)
    {
        uint8_t* address = memory + i * page;
        *address = original[i];
        DWORD previous = 0;
        Check(VirtualProtect(address, page, PAGE_READONLY, &previous), "readonly setup failed");
        Check(AddPatch(plan, address, &replacement[i], 1), "planning failed");
    }
    Check(!AddPatch(plan, memory, &replacement[0], 1), "overlapping patch accepted");

    g_protectCalls = 0;
    g_flushCalls = 0;
    g_failProtectAt = failProtectAt;
    g_failProtectAt2 = failProtectAt2;
    g_failFlushAt = failFlushAt;
    const auto result = ApplyTransaction(plan);
    Check(result == expected, "wrong transaction result");
    for (size_t i = 0; i < 3; ++i)
    {
        const uint8_t value = memory[i * page];
        if (expectOriginal)
            Check(value == original[i], "rollback did not restore every byte");
        else if (expected == TransactionResult::Succeeded)
            Check(value == replacement[i], "commit missed a patch");
        Check(ProtectionAt(memory + i * page) == PAGE_READONLY, "page protection not restored");
    }
    if (expected == TransactionResult::Succeeded)
        Check(g_flushCalls == 3, "committed patches were not flushed");
    VirtualFree(memory, 0, MEM_RELEASE);
}

int main() try
{
    RunCase(0, 0, 0, TransactionResult::Succeeded, false);
    RunCase(5, 0, 0, TransactionResult::FailedRolledBack, true);
    RunCase(0, 0, 1, TransactionResult::FailedRolledBack, true);
    // The first rollback protection call fails after two patches were applied.
    // This is unsafe even if the other patches can be restored.
    RunCase(5, 6, 0, TransactionResult::FailedRollbackIncomplete, false);
    return 0;
}
catch (const std::exception&)
{
    return 1;
}
