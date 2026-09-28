#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

// Testable storage for cffccbca's RDR2/PureDark submission and scan identities.
// A recording/scan identity is NOT proof of queue submission or GPU completion.
// Successful Reset expires a dropped recording; failed Reset does not. As in
// cffccbca, object destruction/address reuse without Reset is not observed.
class DlssNrRdr2Clock
{
    struct Pending
    {
        uint64_t generation;
        uint64_t epoch;
    };
    std::mutex mutex;
    std::unordered_map<const void*, uint64_t> generations;
    std::unordered_map<const void*, Pending> pending;
    uint64_t submittedEpoch = 0, scanEpoch = 0, pendingOverflows = 0;
    const void* lastScan = nullptr;
    uint64_t lastScanGeneration = 0;
    uint64_t Generation(const void* list) const
    {
        const auto it = generations.find(list);
        return it == generations.end() ? 0 : it->second;
    }

  public:
    uint64_t Register(const void* list)
    {
        std::lock_guard lock(mutex);
        if (list)
        {
            if (pending.size() >= 256)
            {
                pending.clear(); // Emergency refusal, never invent a submission.
                ++pendingOverflows;
            }
            pending.insert_or_assign(list, Pending { Generation(list), submittedEpoch });
        }
        return submittedEpoch;
    }
    void ResetRecording(const void* list, bool succeeded)
    {
        if (!list || !succeeded)
            return;
        std::lock_guard lock(mutex);
        ++generations[list];
        pending.erase(list);
    }
    template <class T> uint64_t Submitted(size_t count, T* const* lists)
    {
        if (!lists || !count)
            return 0;
        std::lock_guard lock(mutex);
        bool matched = false;
        for (size_t i = 0; i < count; ++i)
        {
            const auto it = pending.find(lists[i]);
            if (it == pending.end())
                continue;
            matched |= it->second.generation == Generation(lists[i]);
            pending.erase(it);
        }
        return matched ? ++submittedEpoch : 0; // Once per matching queue batch, not per list.
    }
    uint64_t ScanTick(const void* list)
    {
        if (!list)
            return 0;
        std::lock_guard lock(mutex);
        const auto generation = Generation(list);
        if (list != lastScan || generation != lastScanGeneration)
        {
            lastScan = list;
            lastScanGeneration = generation;
            ++scanEpoch;
        }
        return scanEpoch;
    }
    uint64_t Overflows()
    {
        std::lock_guard lock(mutex);
        return pendingOverflows;
    }
    void Clear()
    {
        std::lock_guard lock(mutex);
        generations.clear();
        pending.clear();
        submittedEpoch = scanEpoch = pendingOverflows = lastScanGeneration = 0;
        lastScan = nullptr;
    }
};
