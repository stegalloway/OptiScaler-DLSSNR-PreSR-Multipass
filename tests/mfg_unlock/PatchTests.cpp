// Production patcher/scanner against controlled PE images; no NVIDIA code executes.
#include "Mocks.h"
static HMODULE g_syntheticModule = nullptr;
static HMODULE g_secondarySyntheticModule = nullptr;
static std::vector<HMODULE> g_extraSyntheticModules;
static bool IsSynthetic(HMODULE module)
{
    return module == g_syntheticModule || module == g_secondarySyntheticModule ||
           std::find(g_extraSyntheticModules.begin(), g_extraSyntheticModules.end(), module) !=
               g_extraSyntheticModules.end();
}
static bool g_hasDlssgExport = true;
static bool g_hasDirectSrExport = false;
static bool g_referenceAcquisitionFails = false;
static int g_referencesAcquired = 0;
static int g_referencesReleased = 0;
static int g_protectCalls = 0;
static int g_failProtectAt = 0;
static int g_failProtectAt2 = 0;
static BOOL TestVirtualProtect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous)
{
    ++g_protectCalls;
    if (g_protectCalls == g_failProtectAt || g_protectCalls == g_failProtectAt2)
        return FALSE;
    return VirtualProtect(address, size, protection, previous);
}
static BOOL TestGetModuleHandleExW(DWORD flags, LPCWSTR address, HMODULE* acquired)
{
    const auto raw = reinterpret_cast<HMODULE>(const_cast<LPWSTR>(address));
    if (!IsSynthetic(raw))
        return GetModuleHandleExW(flags, address, acquired);
    if ((flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) == 0 || g_referenceAcquisitionFails)
    {
        *acquired = nullptr;
        return FALSE;
    }
    *acquired = raw;
    ++g_referencesAcquired;
    return TRUE;
}
static BOOL TestFreeLibrary(HMODULE module)
{
    if (!IsSynthetic(module))
        return FreeLibrary(module);
    ++g_referencesReleased;
    return TRUE;
}
static FARPROC TestGetProcAddress(HMODULE module, LPCSTR name)
{
    if (!IsSynthetic(module))
        return GetProcAddress(module, name);
    if (std::strcmp(name, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl") == 0)
        return g_hasDlssgExport ? reinterpret_cast<FARPROC>(&TestGetProcAddress) : nullptr;
    if (std::strcmp(name, "NVSDK_NGX_DirectSR_Create") == 0)
        return g_hasDirectSrExport ? reinterpret_cast<FARPROC>(&TestGetProcAddress) : nullptr;
    return nullptr;
}
#define GetProcAddress TestGetProcAddress
#define GetModuleHandleExW TestGetModuleHandleExW
#define FreeLibrary TestFreeLibrary
#define VirtualProtect TestVirtualProtect
#include "../../OptiScaler/framegen/dlssg/MfgUnlock.cpp"
#undef GetProcAddress
#undef GetModuleHandleExW
#undef FreeLibrary
#undef VirtualProtect
#include "../../OptiScaler/scanner/scanner.cpp"
#include <stdexcept>

void Expect(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
template<class T> void Put(uint8_t* at, T value) { std::memcpy(at, &value, sizeof(value)); }
template<class T> T Read(uint8_t* at) { T value; std::memcpy(&value, at, sizeof(value)); return value; }
void Pattern(uint8_t* to, std::string_view pattern)
{
    unsigned offset = 0;
    for (size_t i = 0; i < pattern.size();)
    {
        if (pattern[i] == ' ') { ++i; continue; }
        if (pattern[i] == '?') { to[offset++] = 0; ++i; continue; }
        to[offset++] = static_cast<uint8_t>(std::stoul(std::string(pattern.substr(i, 2)), nullptr, 16));
        i += 2;
    }
}
int main(int argc, char** argv) try
{
    Expect(argc == 2 || argc == 3, "Pass a test case or runtime and DLL path");
    std::string mode = argv[1];
    Config::Instance()->FGDLSSGAdaMfgUnlock.enabled = mode != "disabled";
    const bool runtimeQuality = mode.starts_with("runtime-quality-");
    if (mode == "runtime" || mode == "runtime-ptx" || runtimeQuality)
    {
        Expect(argc == 3, "Pass an installed DLSSG DLL path");
        // Map its image without imports/DllMain; never initialize FG or change the disk file.
        auto module = LoadLibraryExA(argv[2], nullptr, DONT_RESOLVE_DLL_REFERENCES);
        Expect(module != nullptr, "Map installed runtime");
        if (mode == "runtime-ptx")
            Config::Instance()->FGDLSSGAdaTemporalFix.value = "Ptx";
        if (runtimeQuality)
        {
            const auto qualityPart = mode.substr(std::strlen("runtime-quality-"));
            Config::Instance()->FGDLSSGAdaQualityMode.value = std::stoi(qualityPart);
            Config::Instance()->FGDLSSGAdaWarpBlend.enabled = mode.ends_with("-warp");
        }
        const bool failClean = mode.ends_with("-fail-clean");
        const bool failUnsafe = mode.ends_with("-fail-unsafe");
        const bool badFingerprint = mode.ends_with("-bad-fingerprint");
        if (badFingerprint)
        {
            std::vector<mfgunlock::blackwell::internal::Candidate> candidates;
            std::string detail;
            Expect(mfgunlock::blackwell::internal::CollectCandidates(module, candidates, detail) &&
                       candidates.size() == 3, "Find three unmodified kernel roles for hash rejection test");
            auto* byte = candidates.front().payload + candidates.front().slot_size - 1;
            DWORD protection = 0;
            Expect(VirtualProtect(byte, 1, PAGE_EXECUTE_READWRITE, &protection) != FALSE,
                   "Make mapped test image writable");
            *byte ^= 1;
            DWORD ignored = 0;
            Expect(VirtualProtect(byte, 1, protection, &ignored) != FALSE,
                   "Restore mapped test image protection");
        }
        const auto imageSize = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            reinterpret_cast<const uint8_t*>(module) + reinterpret_cast<const IMAGE_DOS_HEADER*>(module)->e_lfanew)
                                   ->OptionalHeader.SizeOfImage;
        Expect(imageSize > 0 && imageSize < 0x10000000, "Bound mapped-provider comparison");
        std::vector<uint8_t> original;
        if (failClean || badFingerprint)
            original.assign(reinterpret_cast<const uint8_t*>(module),
                            reinterpret_cast<const uint8_t*>(module) + imageSize);
        if (failClean || failUnsafe)
        {
            g_failProtectAt = 3; // Fail after earlier transaction writes, never during planning.
            if (failUnsafe) g_failProtectAt2 = 4; // Refuse the first rollback protection change.
        }
        MfgUnlock::TryApply(module);
        const auto status = MfgUnlock::LastStatus();
        std::cout << "Runtime gates " << status.AdvertiseMatched << '/' << status.ValidateMatched
                  << ", kernel groups " << status.KernelsRewritten << ", detail " << status.TemporalDetail
                  << ", quality " << status.QualityMode << ", warp " << status.QualityWarp
                  << ", quality detail " << status.QualityDetail << '\n';
        if (failClean || failUnsafe || badFingerprint)
        {
            Expect(MfgUnlock::UnlockedMax() == 0 && MfgUnlock::EffectiveMax(1) == 1,
                   "Rejected quality transaction must not advertise unlocked FG");
            if (badFingerprint)
                Expect(!status.PatchFailed && !status.RollbackFailed,
                       "Unrecognized provider payload must refuse before writes");
            else
                Expect(status.PatchFailed && status.RollbackFailed == failUnsafe,
                       "Quality transaction failure must distinguish clean and incomplete rollback");
            if (!original.empty())
                Expect(std::memcmp(module, original.data(), original.size()) == 0,
                       "Rejected quality transaction changed provider image");
            if (failUnsafe)
                Expect(g_retainedProvider == module, "Unsafe rollback must retain the patched provider");
            FreeLibrary(module);
            std::cout << "PASS " << mode << " (mapped provider; no GPU execution)\n";
            return 0;
        }
        Expect(MfgUnlock::UnlockedMax() == 5, "Installed runtime is not supported by this patch");
        if (runtimeQuality)
        {
            Expect(status.QualityMode == Config::Instance()->FGDLSSGAdaQualityMode.value,
                   "Requested quality mode was not recorded");
            Expect(status.KernelsRewritten == 3, "Quality path did not select all three kernel roles");
            Expect(status.QualityWarp == (Config::Instance()->FGDLSSGAdaWarpBlend.enabled || status.QualityMode == 4),
                   "Requested warp path did not commit");
        }
        FreeLibrary(module);
        std::cout << "PASS runtime image patch (simulated Ada; no GPU execution)\n";
        return 0;
    }
    if (mode == "blackwell") IdentifyGpu::gpu.nvidiaArchInfo.architecture_id = 0x1b0;
    if (mode == "ampere") IdentifyGpu::gpu.nvidiaArchInfo.architecture_id = 0x170;
    if (mode == "other-vendor") IdentifyGpu::gpu.vendorId = VendorId::Other;
    if (mode == "restart")
    {
        Config::Instance()->FGDLSSGAdaMfgUnlock.enabled = false;
        Expect(!MfgUnlock::EnabledForSession(), "Default must be inactive");
        Config::Instance()->FGDLSSGAdaMfgUnlock.enabled = true;
        Expect(!MfgUnlock::EnabledForSession() && !MfgUnlock::Pending(), "UI enabled a live patch without restart");
        std::cout << "PASS " << mode << '\n'; return 0;
    }
    if (mode == "plan-invalid-pointer")
    {
        std::vector<MfgUnlock::Transaction::Patch> plan;
        const uint8_t replacement = 5;
        Expect(!MfgUnlock::Transaction::AddPatch(plan, reinterpret_cast<uint8_t*>(1), &replacement, 1) &&
                   plan.empty(), "Unreadable patch address must fail during planning without mutation");
        std::cout << "PASS " << mode << '\n'; return 0;
    }
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x5000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Expect(memory != nullptr, "VirtualAlloc");
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(memory);
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x100;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(memory + 0x100);
    nt->Signature = IMAGE_NT_SIGNATURE; nt->FileHeader.NumberOfSections = 2;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SizeOfImage = 0x5000;
    auto* sections = IMAGE_FIRST_SECTION(nt);
    sections[0].VirtualAddress = 0x1000; sections[0].Misc.VirtualSize = 0x1000;
    sections[0].Characteristics = IMAGE_SCN_MEM_EXECUTE;
    sections[1].VirtualAddress = 0x3000; sections[1].Misc.VirtualSize = 0x1000;
    const bool newer = mode == "3109" || mode == "mixed-families";
    Pattern(memory + 0x1100, newer ? kAdvertisePattern309 : kAdvertisePattern);
    if (mode != "missing-gate") Pattern(memory + 0x1200, newer ? kValidatePattern309 : kValidatePattern);
    if (mode == "duplicate-gate") Pattern(memory + 0x1300, kAdvertisePattern);
    if (mode == "mixed-families") Pattern(memory + 0x1300, kAdvertisePattern);
    if (mode == "unknown") memory[0x1100] = 0;
    if (mode == "bad-image") nt->OptionalHeader.Magic = 0;
    if (mode == "bad-section") sections[1].Misc.VirtualSize = 0x3000;

    // One complete fatbin: an Ada image and Blackwell PTX with an equal-length target directive.
    auto* container = memory + 0x3000;
    Put<uint32_t>(container, 0xba55ed50); Put<uint16_t>(container + 6, 16);
    Put<uint64_t>(container + 8, 128);
    auto* ada = container + 16;
    auto* blackwell = ada + 64;
    Put<uint16_t>(ada, 2); Put<uint32_t>(ada + 4, 32);
    Put<uint64_t>(ada + 8, 32); Put<uint32_t>(ada + 28, 89);
    Put<uint16_t>(blackwell, 1); Put<uint32_t>(blackwell + 4, 32);
    Put<uint64_t>(blackwell + 8, 32); Put<uint32_t>(blackwell + 28, 120);
    std::memcpy(blackwell + 32, ".target sm_120", 14);
    if (mode == "no-kernel") std::memset(blackwell + 32, 0, 14);
    if (mode == "malformed") Put<uint64_t>(blackwell + 8, UINT64_MAX);
    const std::vector<uint8_t> before(memory, memory + 0x5000);
    auto module = reinterpret_cast<HMODULE>(memory);
    g_syntheticModule = module;
    g_hasDlssgExport = mode != "sr-only" && mode != "neither-export";
    g_hasDirectSrExport = mode == "sr-only" || mode == "mixed-exports";
    g_referenceAcquisitionFails = mode == "retain-failure";
    if (mode == "second-provider-capacity")
    {
        MfgUnlock::TryApply(module);
        Expect(MfgUnlock::UnlockedMax() == 5, "First provider failed before capacity test");
        for (size_t i = 1; i < kMaxPatchedProviders + 1; ++i)
        {
            auto* next = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x5000, MEM_RESERVE | MEM_COMMIT,
                                                            PAGE_READWRITE));
            Expect(next != nullptr, "Allocate distinct provider at capacity");
            std::memcpy(next, before.data(), before.size());
            g_extraSyntheticModules.push_back(reinterpret_cast<HMODULE>(next));
            MfgUnlock::TryApply(reinterpret_cast<HMODULE>(next));
            if (i < kMaxPatchedProviders)
                Expect(MfgUnlock::UnlockedMax() == 5, "Provider under capacity failed");
            else
            {
                Expect(MfgUnlock::UnlockedMax() == 0 && MfgUnlock::EffectiveMax(5) == 1,
                       "Capacity exhaustion did not fail closed");
                Expect(std::memcmp(next, before.data(), before.size()) == 0,
                       "Capacity-exhausted provider was modified");
            }
        }
        Expect(g_referencesAcquired == kMaxPatchedProviders && g_referencesReleased == 0,
               "Patched modules lost references or exhausted module was retained");
        for (auto held : g_extraSyntheticModules) VirtualFree(held, 0, MEM_RELEASE);
        VirtualFree(memory, 0, MEM_RELEASE);
        std::cout << "PASS " << mode << '\n';
        return 0;
    }
    if (mode == "second-provider" || mode == "second-provider-unsupported" ||
        mode == "second-provider-fail-clean" || mode == "second-provider-fail-unsafe" ||
        mode == "second-provider-retain-failure")
    {
        auto* second = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x5000, MEM_RESERVE | MEM_COMMIT,
                                                         PAGE_READWRITE));
        Expect(second != nullptr, "Allocate second provider image");
        std::memcpy(second, before.data(), before.size());
        g_secondarySyntheticModule = reinterpret_cast<HMODULE>(second);
        if (mode == "second-provider-unsupported") second[0x1200] = 0;
        const std::vector<uint8_t> secondBefore(second, second + 0x5000);
        MfgUnlock::TryApply(module);
        Expect(MfgUnlock::UnlockedMax() == 5 && g_retainedProvider == module,
               "First provider did not patch and retain successfully");
        const std::vector<uint8_t> firstPatched(memory, memory + 0x5000);
        const bool failClean = mode == "second-provider-fail-clean";
        const bool failUnsafe = mode == "second-provider-fail-unsafe";
        if (failClean || failUnsafe)
        {
            g_failProtectAt = g_protectCalls + 3;
            if (failUnsafe) g_failProtectAt2 = g_failProtectAt + 1;
        }
        if (mode == "second-provider-retain-failure") g_referenceAcquisitionFails = true;
        MfgUnlock::TryApply(g_secondarySyntheticModule);
        if (mode == "second-provider")
        {
            Expect(MfgUnlock::UnlockedMax() == 5 && g_retainedProvider == g_secondarySyntheticModule,
                   "Second provider was not patched and retained");
            Expect(std::memcmp(second, secondBefore.data(), secondBefore.size()) != 0,
                   "Second provider remained unpatched");
            Expect(g_referencesAcquired == 2 && g_referencesReleased == 0,
                   "Both patched providers must retain loader references");
            MfgUnlock::TryApply(module);
            Expect(g_referencesAcquired == 2, "Duplicate provider admission reacquired a reference");
        }
        else if (mode == "second-provider-unsupported")
        {
            Expect(MfgUnlock::UnlockedMax() == 0 && MfgUnlock::EffectiveMax(5) == 1,
                   "Unsupported second provider must refuse higher FG globally");
            Expect(std::memcmp(second, secondBefore.data(), secondBefore.size()) == 0,
                   "Unsupported second provider changed bytes");
            Expect(g_referencesReleased == 1 && g_referencesAcquired == 2,
                   "Unsupported provider's new reference must be released");
        }
        else if (mode == "second-provider-retain-failure")
        {
            Expect(MfgUnlock::UnlockedMax() == 0 && MfgUnlock::EffectiveMax(5) == 1,
                   "Unretained second provider still advertised higher FG");
            Expect(g_referencesAcquired == 1 && g_referencesReleased == 0 &&
                       std::memcmp(second, secondBefore.data(), secondBefore.size()) == 0,
                   "Unretained second provider was mutated or prior reference lost");
        }
        else
        {
            const auto status = MfgUnlock::LastStatus();
            Expect(MfgUnlock::UnlockedMax() == 0 && MfgUnlock::EffectiveMax(5) == 1 &&
                       status.PatchFailed && status.RollbackFailed == failUnsafe,
                   "Second-provider transaction failure did not refuse higher FG");
            if (failClean)
                Expect(std::memcmp(second, secondBefore.data(), secondBefore.size()) == 0 &&
                           g_referencesAcquired == 2 && g_referencesReleased == 1,
                       "Clean second-provider rollback changed bytes or retained its reference");
            else
                Expect(g_referencesAcquired == 2 && g_referencesReleased == 0 &&
                           g_retainedProvider == g_secondarySyntheticModule,
                       "Unsafe second-provider rollback lost an affected module reference");
        }
        Expect(std::memcmp(memory, firstPatched.data(), firstPatched.size()) == 0,
               "First provider was altered during second-provider handover");
        VirtualFree(second, 0, MEM_RELEASE);
        VirtualFree(memory, 0, MEM_RELEASE);
        std::cout << "PASS " << mode << '\n';
        return 0;
    }
    if (mode == "protect-fail-late" || mode == "rollback-incomplete")
        g_failProtectAt = 3;
    if (mode == "rollback-incomplete")
        g_failProtectAt2 = 4;
    MfgUnlock::TryApply(module);
    const bool shouldPatch = mode == "legacy" || mode == "3109";
    if (shouldPatch)
    {
        Expect(MfgUnlock::UnlockedMax() == 5, "Maximum must require successful kernels and both gates");
        Expect(Read<uint32_t>(ada + 28) == 122 && Read<uint32_t>(blackwell + 28) == 89, "Wrong kernel routing");
        Expect(std::memcmp(blackwell + 32, ".target sm_89 ", 14) == 0, "PTX target was not retargeted");
        Expect(MfgUnlock::LastStatus().KernelsRewritten == 1, "Wrong kernel count");
        if (newer)
            Expect(memory[0x1106] == 0x0f && memory[0x1107] == 0x1f && memory[0x1205] == 0xb0, "310.9 gates");
        else
            Expect(memory[0x1107] == 5 && memory[0x1111] == 0x0f && memory[0x1205] == 0x90 && memory[0x1209] == 5, "Legacy gates");
        const std::vector<uint8_t> patched(memory, memory + 0x5000);
        MfgUnlock::TryApply(module);
        Expect(std::memcmp(memory, patched.data(), patched.size()) == 0, "Repeated patch changed the image");
        Config::Instance()->FGDLSSGAdaMfgUnlock.enabled = false;
        Expect(MfgUnlock::EnabledForSession(), "Disabling must wait for restart");
    }
    else
    {
        Expect(MfgUnlock::UnlockedMax() == 0, "Unsupported case advertised MFG");
        if (mode != "rollback-incomplete")
            Expect(std::memcmp(memory, before.data(), before.size()) == 0, "Unsupported case modified memory");
    }
    if (mode == "protect-fail-late" || mode == "rollback-incomplete")
    {
        const auto status = MfgUnlock::LastStatus();
        Expect(status.PatchFailed, "Injected transaction failure must be reported");
        Expect(status.RollbackFailed == (mode == "rollback-incomplete"), "Rollback safety state mismatch");
        Expect(MfgUnlock::EffectiveMax(5) == 1, "Failed patch must cap effective generated frames at one");
        Expect(MfgUnlock::LastFailure() == (mode == "rollback-incomplete" ? MfgUnlock::Failure::RollbackFailed
                                                                     : MfgUnlock::Failure::PatchFailed),
               "Failure state mismatch");
    }
    if (mode == "retain-failure")
        Expect(g_referencesAcquired == 0 && g_referencesReleased == 0,
               "Failed reference acquisition must not enter mutation path");
    else if (g_referencesAcquired != 0)
        Expect(g_referencesAcquired == 1 &&
                   (g_referencesReleased == (g_retainedProvider == module ? 0 : 1)),
               "Provider reference must be released before writes or held thereafter");
    VirtualFree(memory, 0, MEM_RELEASE);
    std::cout << "PASS " << mode << '\n';
    return 0;
}
catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
