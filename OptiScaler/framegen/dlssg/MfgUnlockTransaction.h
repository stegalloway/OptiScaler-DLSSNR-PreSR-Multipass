#pragma once

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace MfgUnlock::Transaction
{
struct Patch
{
    uint8_t* address = nullptr;
    std::vector<uint8_t> original;
    std::vector<uint8_t> replacement;
    DWORD originalProtection = 0;
    bool changed = false;
};

enum class TransactionResult
{
    Succeeded,
    FailedRolledBack,
    FailedRollbackIncomplete
};

bool EqualBytes(const void* a, const void* b, size_t count)
{
    __try { return std::memcmp(a, b, count) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CopyBytes(void* to, const void* from, size_t count)
{
    __try { std::memcpy(to, from, count); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool AddPatch(std::vector<Patch>& plan, uint8_t* address, const uint8_t* replacement, size_t size)
{
    if (address == nullptr || replacement == nullptr || size == 0)
        return false;

    const auto begin = reinterpret_cast<uintptr_t>(address);
    const auto end = begin + size;
    if (end < begin)
        return false;

    for (const auto& patch : plan)
    {
        const auto otherBegin = reinterpret_cast<uintptr_t>(patch.address);
        const auto otherEnd = otherBegin + patch.original.size();
        if (begin < otherEnd && otherBegin < end)
            return false;
    }

    Patch patch;
    patch.address = address;
    patch.original.assign(address, address + size);
    patch.replacement.assign(replacement, replacement + size);
    plan.push_back(std::move(patch));
    return true;
}

template <size_t N> bool AddPatch(std::vector<Patch>& plan, uintptr_t address, const uint8_t (&replacement)[N])
{
    return AddPatch(plan, reinterpret_cast<uint8_t*>(address), replacement, N);
}

bool Rollback(std::vector<Patch>& plan)
{
    bool complete = true;

    for (auto it = plan.rbegin(); it != plan.rend(); ++it)
    {
        auto& patch = *it;
        if (!patch.changed)
            continue;

        DWORD currentProtection = 0;
        if (!VirtualProtect(patch.address, patch.original.size(), PAGE_EXECUTE_READWRITE, &currentProtection))
        {
            LOG_WARN("MFG unlock: rollback VirtualProtect failed at {:X}", reinterpret_cast<uintptr_t>(patch.address));
            complete = false;
            continue;
        }

        if (!CopyBytes(patch.address, patch.original.data(), patch.original.size()) ||
            !EqualBytes(patch.address, patch.original.data(), patch.original.size())) complete = false;

        DWORD ignored = 0;
        if (!VirtualProtect(patch.address, patch.original.size(), patch.originalProtection, &ignored))
        {
            LOG_WARN("MFG unlock: rollback protection restore failed at {:X}",
                     reinterpret_cast<uintptr_t>(patch.address));
            complete = false;
        }

        if (!FlushInstructionCache(GetCurrentProcess(), patch.address, patch.original.size()))
        {
            LOG_WARN("MFG unlock: rollback cache flush failed at {:X}", reinterpret_cast<uintptr_t>(patch.address));
            complete = false;
        }
    }

    return complete;
}

TransactionResult ApplyTransaction(std::vector<Patch>& plan)
{
    // Revalidate every captured byte immediately before the first write. This makes the transaction
    // reject a concurrently changed or incorrectly planned module without touching it.
    for (const auto& patch : plan)
    {
        if (!EqualBytes(patch.address, patch.original.data(), patch.original.size()))
        {
            LOG_WARN("MFG unlock: planned bytes changed before apply at {:X}",
                     reinterpret_cast<uintptr_t>(patch.address));
            return TransactionResult::FailedRolledBack;
        }
    }

    for (auto& patch : plan)
    {
        if (!VirtualProtect(patch.address, patch.replacement.size(), PAGE_EXECUTE_READWRITE, &patch.originalProtection))
        {
            LOG_WARN("MFG unlock: VirtualProtect failed at {:X}", reinterpret_cast<uintptr_t>(patch.address));
            return Rollback(plan) ? TransactionResult::FailedRolledBack : TransactionResult::FailedRollbackIncomplete;
        }

        patch.changed = true; // A failed copy may have written a prefix; rollback must include it.
        if (!CopyBytes(patch.address, patch.replacement.data(), patch.replacement.size()) ||
            !EqualBytes(patch.address, patch.replacement.data(), patch.replacement.size()))
            return Rollback(plan) ? TransactionResult::FailedRolledBack : TransactionResult::FailedRollbackIncomplete;

        DWORD ignored = 0;
        if (!VirtualProtect(patch.address, patch.replacement.size(), patch.originalProtection, &ignored))
        {
            LOG_WARN("MFG unlock: protection restore failed at {:X}", reinterpret_cast<uintptr_t>(patch.address));
            return Rollback(plan) ? TransactionResult::FailedRolledBack : TransactionResult::FailedRollbackIncomplete;
        }

        if (!FlushInstructionCache(GetCurrentProcess(), patch.address, patch.replacement.size()))
        {
            LOG_WARN("MFG unlock: cache flush failed at {:X}", reinterpret_cast<uintptr_t>(patch.address));
            return Rollback(plan) ? TransactionResult::FailedRolledBack : TransactionResult::FailedRollbackIncomplete;
        }
    }

    return TransactionResult::Succeeded;
}
} // namespace MfgUnlock::Transaction

