// Adapted from y4my4my4m/OptiScaler_DLSSNR_Multipass_MFG, tag v4 (7b7220bb), GPL-3.0.
#include "pch.h"

#if defined(OPTISCALER_RTX40_MFG)

#include "MfgUnlock.h"

#include <Config.h>
#include <Util.h>
#include <scanner/scanner.h>
#include <misc/IdentifyGpu.h>

#include "MfgUnlockFlip.h"
#include "MfgUnlockPlugin.h"
#include "MfgUnlockPtx.h"
#include "MfgUnlockTransaction.h"
#include "MfgQuality.h"

#include <array>
#include <mutex>
#include <tlhelp32.h>

namespace
{
// mov ebx,1 / mov r8d,3 / cmp edi,0x1b0 / cmovl r8d,ebx. The two counts and the architecture
// constant together identify the legacy capability gate.
constexpr std::string_view kAdvertisePattern = "BB 01 00 00 00 41 B8 03 00 00 00 81 FF B0 01 00 00 44 0F 4C C3";

// cmp eax,0x1b0 / jl / cmp ebx,3 / jbe. The only comparison against the architecture constant that
// is followed by a signed branch and a count test.
constexpr std::string_view kValidatePattern = "3D B0 01 00 00 7C ? 83 FB 03 76";

// Five generated frames, the count both patched sites carry.
constexpr uint8_t kMaxGeneratedFrames = 5;

// 310.9 restructured both gates. The count is no longer an immediate next to the comparison: the
// Blackwell branch starts at five and reads a configured value, and anything below Blackwell is sent
// to a branch that publishes one.
//     cmp ebp, 0x1b0
//     jl  ada          <- neutralised, so every card takes the Blackwell branch
//     mov edi, 0x5
constexpr std::string_view kAdvertisePattern309 = "81 FD B0 01 00 00 0F 8C ? ? ? ? BF 05 00 00 00";

// The capability flag in the same build is a setae rather than a branch.
//     cmp   eax, 0x1b0
//     setae al
constexpr std::string_view kValidatePattern309 = "3D B0 01 00 00 0F 93 C0";



MfgUnlock::Status g_status {};
std::recursive_mutex g_mutex;
// Every patched provider keeps a real loader reference for process lifetime.
// In particular, an OTA handover must not release the base DLL while its
// modified code may still execute on another thread.
HMODULE g_retainedProvider = nullptr;
constexpr size_t kMaxPatchedProviders = 8;
std::array<HMODULE, kMaxPatchedProviders - 1> g_priorPatchedProviders {};
size_t g_priorPatchedProviderCount = 0;
// A successful quality descriptor redirect must remain live with the retained
// provider. After incomplete rollback it also cannot be freed safely.
void* g_qualityAllocation = nullptr;
std::array<void*, kMaxPatchedProviders - 1> g_priorQualityAllocations {};
size_t g_priorQualityAllocationCount = 0;

bool IsPatchedProvider(HMODULE module)
{
    return module == g_retainedProvider ||
           std::find(g_priorPatchedProviders.begin(),
                     g_priorPatchedProviders.begin() + g_priorPatchedProviderCount, module) !=
               g_priorPatchedProviders.begin() + g_priorPatchedProviderCount;
}

void RetainPatchedProvider(HMODULE acquired)
{
    if (g_retainedProvider)
        g_priorPatchedProviders[g_priorPatchedProviderCount++] = g_retainedProvider;
    g_retainedProvider = acquired;
}

void RetainQualityAllocation(void* allocation)
{
    if (!allocation) return;
    if (g_qualityAllocation)
        g_priorQualityAllocations[g_priorQualityAllocationCount++] = g_qualityAllocation;
    g_qualityAllocation = allocation;
}

// A filename or NGX version string alone can also identify a DirectSR module.
// Require the DLSS-G-specific export and refuse mixed-export images before any
// gate or kernel mutation. The transaction port will additionally retain the
// module while inspecting and patching it.
bool HasExclusiveDlssgExports(HMODULE module)
{
    if (!module)
        return false;
    __try
    {
        return GetProcAddress(module, "NVSDK_NGX_D3D12_PopulateDeviceParameters_Impl") != nullptr &&
               GetProcAddress(module, "NVSDK_NGX_DirectSR_Create") == nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool RetainModule(HMODULE raw, HMODULE& held);
bool ValidProviderImage(HMODULE module);
HMODULE RetainedCandidate(HMODULE raw);

uintptr_t UniqueAddress(HMODULE module, std::string_view pattern)
{
    const auto first = scanner::GetAddress(module, pattern);
    return first && !scanner::GetAddress(module, pattern, 0, first + 1) ? first : 0;
}

// The module's own file version, for the report. A signature that does not match is expected on a
// version nobody has looked at, and the version is the one thing that makes such a report actionable.
std::string ModuleVersion(HMODULE module)
{
    wchar_t path[MAX_PATH] {};

    if (GetModuleFileNameW(module, path, MAX_PATH) == 0)
        return {};

    version_t file {};
    version_t product {};

    if (!Util::GetFileVersion(path, &file, &product))
        return {};

    return std::format("{}.{}.{}", file.major, file.minor, file.patch);
}

// The DLSS-G provider, if it is mapped. The file name finds the game's own copy. The driver's OTA copy
// (models\dlssg\versions\<n>\files\<hash>.bin) and a renamed snippet are found by walking the loaded
// modules, which is only safe from an ordinary thread: a snapshot taken under the loader lock
// deadlocks. The load hook hands TryApply its module directly and never comes here.
//
// TryApply runs on every Streamline call until the snippet is found, so the walk is rate limited. A
// provider that has not appeared after these few walks is not going to appear through this route.
HMODULE FindRetainedProvider()
{
    if (auto held = RetainedCandidate(GetModuleHandleW(L"nvngx_dlssg.dll")))
        return held;

    static uint64_t nextWalk = 0;
    static unsigned walks = 0;
    constexpr unsigned kMaxWalks = 8;
    constexpr uint64_t kWalkIntervalMs = 2000;

    const uint64_t now = GetTickCount64();

    if (walks >= kMaxWalks || now < nextWalk)
        return nullptr;

    nextWalk = now + kWalkIntervalMs;
    ++walks;

    // The marker string is a literal in this DLL, so it has to be excluded from the walk.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&FindRetainedProvider), &self);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());

    if (snapshot == INVALID_HANDLE_VALUE)
        return nullptr;

    HMODULE found = nullptr;
    bool ambiguous = false;
    unsigned int inspected = 0;
    MODULEENTRY32W entry {};
    entry.dwSize = sizeof(entry);

    bool more = Module32FirstW(snapshot, &entry);
    for (; more && inspected < 1024; more = Module32NextW(snapshot, &entry))
    {
        ++inspected;
        if (entry.hModule == self)
            continue;

        HMODULE held = RetainedCandidate(entry.hModule);
        if (!held)
            continue;

        const bool matches = MfgUnlock::Provider::IsProviderPath(entry.szExePath) ||
                             MfgUnlock::Provider::ImageContains(held, MfgUnlock::Provider::kMarker);
        if (!matches)
        {
            FreeLibrary(held);
            continue;
        }

        if (found)
        {
            FreeLibrary(held);
            ambiguous = true;
            break;
        }
        found = held;
    }

    CloseHandle(snapshot);
    // A truncated walk cannot establish uniqueness either.
    if (ambiguous || more)
    {
        if (found)
            FreeLibrary(found);
        return nullptr;
    }

    return found;
}

// The Streamline DLSS-G plugins seen so far, and the ones already tried. A plugin is tried once: the
// answer does not change, and a repeat would only repeat the log line.
// Status, plugin discovery and patch application share g_mutex.
std::vector<HMODULE> g_plugins;
std::vector<HMODULE> g_pluginsTried;

// Same guards as TryApply: the option on for this session, an Ada GPU.
bool AdaUnlockWanted()
{
    if (!MfgUnlock::EnabledForSession())
        return false;

    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    return gpu.vendorId == VendorId::Nvidia && gpu.nvidiaArchInfo.architecture_id == NV_GPU_ARCHITECTURE_AD100;
}

// Software frame pacing: pin the plugin's flip-metering state to its own software fallback. Applied when
// the plugin is loaded, before Streamline uses it, because rewriting a register store is a seven byte
// write into code that no other thread should be executing yet. Caller holds g_mutex.
void PatchFlipMetering(HMODULE plugin)
{
    g_status.FlipRequested = Config::Instance()->FGDLSSGAdaFlipMeteringPatch.value_or_default();

    if (std::string_view(g_status.FlipMetering) == "patched")
        return;

    // A plugin has been seen; "off" tells the overlay that, and that nothing was asked of it.
    if (!g_status.FlipRequested)
    {
        g_status.FlipMetering = "off";
        return;
    }

    wchar_t path[MAX_PATH] {};
    GetModuleFileNameW(plugin, path, MAX_PATH);
    const auto pluginPath = wstring_to_string(path);

    MfgUnlock::Flip::Plan plan;
    const auto found = MfgUnlock::Flip::FindPlan(plugin, plan);

    if (found != MfgUnlock::Flip::FindResult::Found)
    {
        g_status.FlipMetering = MfgUnlock::Flip::Describe(found);
        LOG_WARN("MFG unlock: {}: software frame pacing not applied: {}", pluginPath, g_status.FlipMetering);
        return;
    }

    switch (MfgUnlock::Flip::Apply(plan))
    {
    case MfgUnlock::Flip::ApplyResult::Patched:
        g_status.FlipMetering = "patched";
        g_status.FlipSites = static_cast<unsigned int>(plan.sites.size());
        LOG_INFO("MFG unlock: {}: flip-metering state +0x{:X} pinned to {} at {} site(s); multi-frame should pace in "
                 "software",
                 pluginPath, plan.field, plan.value, plan.sites.size());
        break;
    case MfgUnlock::Flip::ApplyResult::Mismatch:
        g_status.FlipMetering = "the plugin changed while it was being patched";
        LOG_WARN("MFG unlock: {}: software frame pacing not applied: {}", pluginPath, g_status.FlipMetering);
        break;
    case MfgUnlock::Flip::ApplyResult::ProtectFailed:
        g_status.FlipMetering = "its memory could not be made writable";
        LOG_WARN("MFG unlock: {}: software frame pacing not applied: {}", pluginPath, g_status.FlipMetering);
        break;
    }
}

// Only once the snippet unlock has landed. Raising the plugin's ceiling while the snippet still answers
// Ada's 1 would only move the rejection from the plugin to the snippet.
void PatchPluginCeilings()
{
    std::lock_guard lock(g_mutex);
    if (MfgUnlock::UnlockedMax() == 0)
        return;

    for (HMODULE plugin : g_plugins)
    {
        if (std::find(g_pluginsTried.begin(), g_pluginsTried.end(), plugin) != g_pluginsTried.end())
            continue;

        g_pluginsTried.push_back(plugin);

        wchar_t path[MAX_PATH] {};
        GetModuleFileNameW(plugin, path, MAX_PATH);
        const auto pluginPath = wstring_to_string(path);

        // A plugin that was patched stays patched; a second one that cannot be does not change that.
        const bool alreadyPatched = std::string_view(g_status.PluginCeiling) == "patched";

        MfgUnlock::Plugin::CeilingSite site;
        const char* result = "not matched";

        switch (MfgUnlock::Plugin::FindCeilingSite(plugin, site))
        {
        case MfgUnlock::Plugin::FindResult::Found:
            switch (MfgUnlock::Plugin::ApplyCeilingPatch(site))
            {
            case MfgUnlock::Plugin::ApplyResult::Patched:
                result = "patched";
                LOG_INFO("MFG unlock: {}: frame-count clamp neutralised, compiled maximum {} generated frame(s)",
                         pluginPath, site.compiled);
                break;
            case MfgUnlock::Plugin::ApplyResult::ProtectFailed:
                result = "not writable";
                LOG_WARN("MFG unlock: {}: frame-count clamp found but its page could not be made writable", pluginPath);
                break;
            case MfgUnlock::Plugin::ApplyResult::Mismatch:
                LOG_WARN("MFG unlock: {}: frame-count clamp changed under us; left unchanged", pluginPath);
                break;
            }
            break;
        case MfgUnlock::Plugin::FindResult::Ambiguous:
            result = "ambiguous";
            LOG_WARN("MFG unlock: {}: more than one frame-count clamp; left unchanged", pluginPath);
            break;
        case MfgUnlock::Plugin::FindResult::None:
        case MfgUnlock::Plugin::FindResult::BadImage:
            LOG_WARN("MFG unlock: {}: no frame-count clamp of the known shape; left unchanged", pluginPath);
            break;
        }

        if (!alreadyPatched)
            g_status.PluginCeiling = result;
    }
}

bool WriteBytes(uintptr_t address, const uint8_t* bytes, size_t count)
{
    DWORD oldProtect = 0;

    if (!VirtualProtect((LPVOID) address, count, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        LOG_WARN("VirtualProtect failed at {:X}", address);
        return false;
    }

    std::memcpy((void*) address, bytes, count);

    DWORD ignored = 0;
    VirtualProtect((LPVOID) address, count, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID) address, count);

    return true;
}

std::string Hex(const uint8_t* bytes, size_t count)
{
    std::string out;

    for (size_t i = 0; i < count; ++i)
        out += std::format("{}{:02X}", i == 0 ? "" : " ", bytes[i]);

    return out;
}

// Rewrites count and neutralises the architecture clamp, so MultiFrameCountMax is published as five.
bool PatchAdvertise(HMODULE module)
{
    if (const auto at309 = UniqueAddress(module, kAdvertisePattern309); at309 != 0)
    {
        // The jl is a rel32, six bytes.
        const auto branchAt = at309 + 6;
        const uint8_t nop[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x90 };

        LOG_INFO("MFG unlock: advertise (310.9) at {:X}, jl {} -> {}", at309,
                 Hex((const uint8_t*) branchAt, sizeof(nop)), Hex(nop, sizeof(nop)));

        return WriteBytes(branchAt, nop, sizeof(nop));
    }

    const auto address = UniqueAddress(module, kAdvertisePattern);

    if (address == 0)
    {
        LOG_WARN("MFG unlock: the advertise signature did not match, nvngx_dlssg.dll left alone");
        return false;
    }

    // Offsets within the matched sequence: the r8d immediate, and the cmovl.
    const auto countAt = address + 7;
    const auto cmovAt = address + 17;

    const uint8_t count[] = { kMaxGeneratedFrames };
    const uint8_t nop[] = { 0x0F, 0x1F, 0x40, 0x00 };

    LOG_INFO("MFG unlock: advertise at {:X}, count {} -> {}, cmovl {} -> {}", address,
             *(const uint8_t*) countAt, kMaxGeneratedFrames, Hex((const uint8_t*) cmovAt, sizeof(nop)),
             Hex(nop, sizeof(nop)));

    return WriteBytes(countAt, count, sizeof(count)) && WriteBytes(cmovAt, nop, sizeof(nop));
}

// Drops the Ada branch and raises the accepted count, so a request for five is not rejected.
bool PatchValidate(HMODULE module)
{
    if (const auto at309 = UniqueAddress(module, kValidatePattern309); at309 != 0)
    {
        // setae al -> mov al, 1, so the flag is set whatever the architecture reports.
        const auto setAt = at309 + 5;
        const uint8_t always[] = { 0xB0, 0x01, 0x90 };

        LOG_INFO("MFG unlock: validate (310.9) at {:X}, setae {} -> {}", at309,
                 Hex((const uint8_t*) setAt, sizeof(always)), Hex(always, sizeof(always)));

        return WriteBytes(setAt, always, sizeof(always));
    }

    const auto address = UniqueAddress(module, kValidatePattern);

    if (address == 0)
    {
        LOG_WARN("MFG unlock: the validate signature did not match, nvngx_dlssg.dll left alone");
        return false;
    }

    // Offsets within the matched sequence: the jl, and the immediate of the count test behind it.
    const auto branchAt = address + 5;
    const auto countAt = address + 9;

    const uint8_t nop[] = { 0x90, 0x90 };
    const uint8_t count[] = { kMaxGeneratedFrames };

    LOG_INFO("MFG unlock: validate at {:X}, jl {} -> {}, count {} -> {}", address,
             Hex((const uint8_t*) branchAt, sizeof(nop)), Hex(nop, sizeof(nop)), *(const uint8_t*) countAt,
             kMaxGeneratedFrames);

    return WriteBytes(branchAt, nop, sizeof(nop)) && WriteBytes(countAt, count, sizeof(count));
}


// Gives Ada the Blackwell kernels the module already carries.
//
// nvngx_dlssg.dll ships two builds of the interpolation kernels. Kernel_EstimateIntermMvecsScatter
// reads three f32 fields of its parameter block on sm_120 and one on sm_89, so on Ada every generated
// frame is placed at the same point between the two real ones: the world does not advance between
// them while the interface, composited once per present, does. At 2X there is one frame and nothing
// to distinguish; above it that is the whole symptom.
//
// The sm_120 module uses no instruction Ada lacks. So per container: the Blackwell PTX image is
// relabelled sm_89, its .target directive is rewritten in place (".target sm_120" and
// ".target sm_89 " are both fourteen bytes, and the directive sits in the literal run at the head of
// the LZ4 stream), and the images that were sm_89 -- the Ada PTX and its SASS -- are relabelled to an
// architecture that does not exist so the driver cannot select them. The driver then JITs Blackwell's
// kernel when it asks for Ada's.
//
// Nothing is copied in and no payload changes length. A container without both images is left alone.
constexpr uint32_t kArchAda = 89;
constexpr uint32_t kArchBlackwell = 120;

// No such shader model. Parks an image where nothing will ask for it.
constexpr uint32_t kArchParked = 122;

// Offsets inside a fatbin image header: payload length, and the architecture the image answers for.
constexpr size_t kImagePayloadSize = 8;
constexpr size_t kImageArch = 28;

unsigned int RewriteBlackwellKernels(HMODULE module)
{
    auto base = reinterpret_cast<uint8_t*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);

    const uint8_t magic[] = { 0x50, 0xED, 0x55, 0xBA };
    unsigned int rewritten = 0;

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto& s = section[i];

        if (s.Characteristics & IMAGE_SCN_MEM_EXECUTE)
            continue;

        uint8_t* start = base + s.VirtualAddress;
        uint8_t* end = start + s.Misc.VirtualSize;

        for (uint8_t* c = std::search(start, end, magic, magic + sizeof(magic)); c < end;
             c = std::search(c + 1, end, magic, magic + sizeof(magic)))
        {
            if (end - c < 16)
                break;

            const auto headerSize = *reinterpret_cast<const uint16_t*>(c + 6);
            const auto fatSize = *reinterpret_cast<const uint64_t*>(c + 8);

            if (headerSize != 0x10 || fatSize == 0 || fatSize > (uint64_t) (end - c - 16))
                continue;

            uint8_t* blackwell = nullptr;
            size_t blackwellHeader = 0;
            size_t blackwellPayload = 0;
            std::vector<uint8_t*> ada;
            bool valid = true;

            for (uint8_t* image = c + 16; image < c + 16 + fatSize;)
            {
                const auto remaining = (uint64_t) (c + 16 + fatSize - image);
                if (remaining < kImageArch + sizeof(uint32_t))
                {
                    valid = false;
                    break;
                }
                const auto kind = *reinterpret_cast<const uint16_t*>(image);
                const auto imageHeader = *reinterpret_cast<const uint32_t*>(image + 4);
                const auto payload = *reinterpret_cast<const uint64_t*>(image + kImagePayloadSize);
                const auto arch = *reinterpret_cast<const uint32_t*>(image + kImageArch);

                if (imageHeader < kImageArch + sizeof(uint32_t) || imageHeader > remaining ||
                    payload == 0 || payload > remaining - imageHeader)
                {
                    valid = false;
                    break;
                }

                // kind 1 is PTX, 2 is a cubin. Only the PTX can be retargeted; the cubin is parked.
                if (kind == 1 && arch == kArchBlackwell)
                {
                    blackwell = image;
                    blackwellHeader = imageHeader;
                    blackwellPayload = payload;
                }
                else if (arch == kArchAda)
                {
                    ada.push_back(image);
                }

                image += imageHeader + payload;
            }

            if (!valid || blackwell == nullptr || ada.empty())
                continue;

            const char from[] = ".target sm_120";
            const char to[] = ".target sm_89 ";
            static_assert(sizeof(from) == sizeof(to), "the directive rewrite must not change length");

            uint8_t* body = blackwell + blackwellHeader;
            uint8_t* bodyEnd = body + blackwellPayload;
            auto at = std::search(body, bodyEnd, from, from + sizeof(from) - 1);

            if (at == bodyEnd)
                continue;

            const uint32_t ada89 = kArchAda;
            const uint32_t parked = kArchParked;
            // Prepare one complete container first. A failed protection change must not leave
            // its PTX target and architecture headers disagreeing, or count a partial rewrite.
            std::vector<uint8_t> patched(c, c + 16 + fatSize);
            std::memcpy(patched.data() + (at - c), to, sizeof(to) - 1);
            std::memcpy(patched.data() + (blackwell + kImageArch - c), &ada89, sizeof(ada89));
            for (uint8_t* image : ada)
                std::memcpy(patched.data() + (image + kImageArch - c), &parked, sizeof(parked));
            if (WriteBytes(reinterpret_cast<uintptr_t>(c), patched.data(), patched.size()))
                ++rewritten;
        }
    }

    LOG_INFO("MFG unlock: {} kernel containers answer Ada with the Blackwell image", rewritten);

    return rewritten;
}
struct PatternMatches
{
    uintptr_t address = 0;
    unsigned int count = 0;
};

PatternMatches FindMatches(HMODULE module, std::string_view pattern)
{
    PatternMatches matches;
    uintptr_t start = 0;

    while (const auto address = scanner::GetAddress(module, pattern, 0, start))
    {
        if (matches.count == 0)
            matches.address = address;
        ++matches.count;
        if (matches.count > 1)
            break;
        start = address + 1;
    }

    return matches;
}

// A raw module handle is only a borrowed address. Keep a real loader reference
// before examining exports, PE headers or any signature bytes.
bool RetainModule(HMODULE raw, HMODULE& held)
{
    held = nullptr;
    if (!raw || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(raw), &held))
        return false;
    if (held == raw)
        return true;
    if (held)
        FreeLibrary(held);
    held = nullptr;
    return false;
}

bool ValidProviderImage(HMODULE module)
{
    if (!HasExclusiveDlssgExports(module))
        return false;
    __try
    {
        const auto* base = reinterpret_cast<const uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) ||
            dos->e_lfanew > 0x100000)
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 96 ||
            nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64))
            return false;
        const size_t size = nt->OptionalHeader.SizeOfImage;
        const size_t sectionOffset = reinterpret_cast<const uint8_t*>(IMAGE_FIRST_SECTION(nt)) - base;
        if (size < sizeof(IMAGE_DOS_HEADER) || size > 1024ull * 1024 * 1024 || sectionOffset > size ||
            nt->FileHeader.NumberOfSections > (size - sectionOffset) / sizeof(IMAGE_SECTION_HEADER))
            return false;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (sections[i].VirtualAddress > size ||
                sections[i].Misc.VirtualSize > size - sections[i].VirtualAddress)
                return false;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

HMODULE RetainedCandidate(HMODULE raw)
{
    HMODULE held = nullptr;
    if (!RetainModule(raw, held))
        return nullptr;
    if (ValidProviderImage(held))
        return held;
    FreeLibrary(held);
    return nullptr;
}

bool BuildGatePlan(HMODULE module, std::vector<MfgUnlock::Transaction::Patch>& plan)
{
    const auto advertise309 = FindMatches(module, kAdvertisePattern309);
    const auto validate309 = FindMatches(module, kValidatePattern309);
    const auto advertiseLegacy = FindMatches(module, kAdvertisePattern);
    const auto validateLegacy = FindMatches(module, kValidatePattern);

    const bool exact309 =
        advertise309.count == 1 && validate309.count == 1 && advertiseLegacy.count == 0 && validateLegacy.count == 0;
    const bool exactLegacy =
        advertiseLegacy.count == 1 && validateLegacy.count == 1 && advertise309.count == 0 && validate309.count == 0;

    if (exact309)
    {
        const uint8_t advertiseNop[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x90 };
        const uint8_t validateAlways[] = { 0xB0, 0x01, 0x90 };
        return MfgUnlock::Transaction::AddPatch(plan, advertise309.address + 6, advertiseNop) &&
               MfgUnlock::Transaction::AddPatch(plan, validate309.address + 5, validateAlways);
    }

    if (exactLegacy)
    {
        const uint8_t count[] = { kMaxGeneratedFrames };
        const uint8_t advertiseNop[] = { 0x0F, 0x1F, 0x40, 0x00 };
        const uint8_t validateNop[] = { 0x90, 0x90 };
        return MfgUnlock::Transaction::AddPatch(plan, advertiseLegacy.address + 7, count) &&
               MfgUnlock::Transaction::AddPatch(plan, advertiseLegacy.address + 17, advertiseNop) &&
               MfgUnlock::Transaction::AddPatch(plan, validateLegacy.address + 5, validateNop) &&
               MfgUnlock::Transaction::AddPatch(plan, validateLegacy.address + 9, count);
    }

    return false;
}

// Plans every compatible fatbin rewrite without changing the module. A malformed candidate makes the
// optional kernel mode unsupported; mixing a partial rewrite with unlocked frame-count gates is unsafe.
bool BuildKernelPlan(HMODULE module, std::vector<MfgUnlock::Transaction::Patch>& plan, unsigned int& containers)
{
    auto* base = reinterpret_cast<uint8_t*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto* sections = IMAGE_FIRST_SECTION(nt);
    const uint8_t magic[] = { 0x50, 0xED, 0x55, 0xBA };
    containers = 0;

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto& section = sections[i];
        if (section.Characteristics & IMAGE_SCN_MEM_EXECUTE)
            continue;

        uint8_t* start = base + section.VirtualAddress;
        uint8_t* end = start + section.Misc.VirtualSize;
        for (uint8_t* cursor = start; cursor < end;)
        {
            uint8_t* container = std::search(cursor, end, magic, magic + sizeof(magic));
            if (container == end)
                break;
            cursor = container + 1;

            if (end - container < 16)
                return false;

            const auto headerSize = *reinterpret_cast<const uint16_t*>(container + 6);
            const auto fatSize = *reinterpret_cast<const uint64_t*>(container + 8);
            if (headerSize != 0x10 || fatSize == 0 || fatSize > static_cast<uint64_t>(end - container - 16))
                return false;

            uint8_t* const containerEnd = container + 16 + fatSize;
            uint8_t* blackwell = nullptr;
            size_t blackwellHeader = 0;
            size_t blackwellPayload = 0;
            std::vector<uint8_t*> adaImages;

            for (uint8_t* image = container + 16; image < containerEnd;)
            {
                const auto remaining = static_cast<uint64_t>(containerEnd - image);
                if (remaining < kImageArch + sizeof(uint32_t))
                    return false;

                const auto kind = *reinterpret_cast<const uint16_t*>(image);
                const auto imageHeader = *reinterpret_cast<const uint32_t*>(image + 4);
                const auto payload = *reinterpret_cast<const uint64_t*>(image + kImagePayloadSize);
                const auto arch = *reinterpret_cast<const uint32_t*>(image + kImageArch);
                if (imageHeader < kImageArch + sizeof(uint32_t) || imageHeader > remaining || payload == 0 ||
                    payload > remaining - imageHeader)
                    return false;

                if (kind == 1 && arch == kArchBlackwell)
                {
                    if (blackwell != nullptr)
                        return false;
                    blackwell = image;
                    blackwellHeader = imageHeader;
                    blackwellPayload = static_cast<size_t>(payload);
                }
                else if (arch == kArchAda)
                    adaImages.push_back(image);

                image += imageHeader + payload;
            }

            if (blackwell == nullptr || adaImages.empty())
                continue;

            const char from[] = ".target sm_120";
            const char to[] = ".target sm_89 ";
            static_assert(sizeof(from) == sizeof(to), "the directive rewrite must not change length");

            uint8_t* body = blackwell + blackwellHeader;
            uint8_t* bodyEnd = body + blackwellPayload;
            auto target = std::search(body, bodyEnd, from, from + sizeof(from) - 1);
            if (target == bodyEnd || std::search(target + 1, bodyEnd, from, from + sizeof(from) - 1) != bodyEnd)
                return false;

            std::vector<uint8_t> patched(container, containerEnd);
            std::memcpy(patched.data() + (target - container), to, sizeof(to) - 1);
            std::memcpy(patched.data() + (blackwell + kImageArch - container), &kArchAda, sizeof(kArchAda));
            for (auto* image : adaImages)
                std::memcpy(patched.data() + (image + kImageArch - container), &kArchParked, sizeof(kArchParked));

            if (!MfgUnlock::Transaction::AddPatch(plan, container, patched.data(), patched.size()))
                return false;
            ++containers;
            cursor = containerEnd;
        }
    }

    return containers > 0;
}

bool GuardedBuildRetargetPlan(HMODULE module, std::vector<MfgUnlock::Transaction::Patch>& plan,
                              unsigned int& containers)
{
    __try
    {
        return BuildGatePlan(module, plan) && BuildKernelPlan(module, plan, containers);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool BuildQualityPlan(HMODULE module, MfgQuality::Options quality,
                      std::vector<MfgUnlock::Transaction::Patch>& plan, MfgQuality::Result& prepared)
{
    if (!BuildGatePlan(module, plan) || !MfgQuality::Prepare(module, quality, {}, prepared) ||
        prepared.writes.size() < 3)
        return false;

    for (const auto& write : prepared.writes)
        if (!MfgUnlock::Transaction::AddPatch(plan, write.address, write.replacement.data(),
                                               write.replacement.size()))
            return false;
    return true;
}

bool GuardedBuildQualityPlan(HMODULE module, MfgQuality::Options quality,
                             std::vector<MfgUnlock::Transaction::Patch>& plan, MfgQuality::Result& prepared)
{
    __try
    {
        return BuildQualityPlan(module, quality, plan, prepared);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        prepared.detail = "provider access fault during quality planning; no writes";
        return false;
    }
}

} // namespace

void MfgUnlock::TryApply(HMODULE requestedModule)
{
    if (!AdaUnlockWanted())
        return;

    std::lock_guard lock(g_mutex);

    // A direct provider call is allowed to introduce a second, distinct OTA
    // image after success. Null-module periodic calls never re-patch the base.
    static bool snippetDone = false;
    if (g_status.PatchFailed)
        return;
    if (requestedModule && IsPatchedProvider(requestedModule))
        return;
    const bool secondProvider = snippetDone && g_retainedProvider && requestedModule;
    if (snippetDone && !secondProvider)
        return;
    if (secondProvider && g_priorPatchedProviderCount + 1 >= kMaxPatchedProviders)
    {
        g_status.PatchFailed = true;
        g_status.TemporalDetail = "patched provider capacity exhausted; higher FG refused";
        LOG_WARN("MFG unlock: {}", g_status.TemporalDetail);
        return;
    }

    if (!snippetDone || secondProvider)
    {
        HMODULE acquired = nullptr;
        if (requestedModule)
        {
            if (!RetainModule(requestedModule, acquired))
            {
                if (secondProvider)
                {
                    // The new provider may now be selected by NGX. The old
                    // successful patch cannot justify a claim about it.
                    g_status.PatchFailed = true;
                    g_status.TemporalDetail = "second provider could not be retained; higher FG refused";
                }
                LOG_WARN("MFG unlock: could not retain provider; left unchanged");
                return;
            }
        }
        else
            acquired = FindRetainedProvider();

        if (acquired != nullptr)
        {
            HMODULE module = acquired;
            if (!ValidProviderImage(module))
            {
                FreeLibrary(acquired);
                if (secondProvider)
                {
                    g_status.PatchFailed = true;
                    g_status.TemporalDetail = "second provider identity invalid; higher FG refused";
                }
                LOG_WARN("MFG unlock: provider identity or image layout invalid; left unchanged");
                return;
            }

            // Validate both gates before touching either. Ambiguous/unknown versions remain unmodified.
            const bool knownGates =
                (UniqueAddress(module, kAdvertisePattern309) && UniqueAddress(module, kValidatePattern309)) ||
                (UniqueAddress(module, kAdvertisePattern) && UniqueAddress(module, kValidatePattern));
            if (!knownGates)
            {
                FreeLibrary(acquired);
                if (secondProvider)
                {
                    g_status.PatchFailed = true;
                    g_status.TemporalDetail = "second provider gates unsupported; higher FG refused";
                }
                LOG_WARN("MFG unlock: unsupported or ambiguous DLSSG {} signatures; left unchanged",
                         ModuleVersion(module));
                return;
            }

            snippetDone = true;
            g_status.ModuleFound = true;
            g_status.SnippetVersion = ModuleVersion(module);

            wchar_t modulePath[MAX_PATH] {};
            GetModuleFileNameW(module, modulePath, MAX_PATH);
            LOG_INFO("MFG unlock: DLSS-G provider {} at {}", g_status.SnippetVersion, wstring_to_string(modulePath));

            // Gates and the selected temporal method form one transaction. A
            // count-only unlock repeats frames, and a partly redirected PTX
            // descriptor table cannot be reported as a working unlock.
            const auto method = ConfiguredTemporalMethod();
            g_status.TemporalAttempted = method;
            const MfgQuality::Options quality {
                Config::Instance()->FGDLSSGAdaQualityMode.value_or_default(),
                Config::Instance()->FGDLSSGAdaWarpBlend.value_or_default()
            };
            g_status.QualityMode = quality.mode;

            std::vector<MfgUnlock::Transaction::Patch> plan;
            unsigned int kernelCount = 0;
            Ptx::Plan ptxPlan;
            Ptx::Result ptxResult;
            MfgQuality::Result qualityResult;
            bool planned = false;
            if (quality.mode != 0)
            {
                // One exclusive backend per process: no broad Retarget or PTX
                // descriptor writes are combined with the quality profile.
                planned = GuardedBuildQualityPlan(module, quality, plan, qualityResult);
                if (planned) kernelCount = 3;
            }
            else if (method == TemporalMethod::Retarget)
                planned = GuardedBuildRetargetPlan(module, plan, kernelCount);
            else if (BuildGatePlan(module, plan) && Ptx::Prepare(module, ptxPlan, ptxResult))
            {
                const auto rebuiltPointer = reinterpret_cast<uint64_t>(ptxPlan.rebuilt);
                planned = !ptxPlan.slots.empty();
                for (auto* slot : ptxPlan.slots)
                    planned = MfgUnlock::Transaction::AddPatch(plan, reinterpret_cast<uint8_t*>(slot),
                                                                reinterpret_cast<const uint8_t*>(&rebuiltPointer),
                                                                sizeof(rebuiltPointer)) && planned;
                kernelCount = static_cast<unsigned int>(ptxPlan.slots.size());
            }
            if (!planned)
            {
                if (ptxPlan.rebuilt)
                    VirtualFree(ptxPlan.rebuilt, 0, MEM_RELEASE);
                MfgQuality::DiscardUncommitted(qualityResult);
                g_status.TemporalDetail = quality.mode != 0 && !qualityResult.detail.empty()
                                              ? qualityResult.detail
                                              : method == TemporalMethod::Ptx && !ptxResult.detail.empty()
                                                  ? ptxResult.detail
                                                  : "unsupported, malformed or mixed gate/kernel layout; no writes";
                g_status.QualityDetail = quality.mode != 0 ? g_status.TemporalDetail : "";
                if (secondProvider) g_status.PatchFailed = true;
                FreeLibrary(acquired);
                LOG_WARN("MFG unlock: {}", g_status.TemporalDetail);
                return;
            }

            const auto result = MfgUnlock::Transaction::ApplyTransaction(plan);
            if (result != MfgUnlock::Transaction::TransactionResult::Succeeded)
            {
                g_status.PatchFailed = true;
                g_status.RollbackFailed =
                    result == MfgUnlock::Transaction::TransactionResult::FailedRollbackIncomplete;
                g_status.TemporalDetail = g_status.RollbackFailed ? "unsafe incomplete rollback; FG refused"
                                                                  : "patch failed and rolled back";
                if (!g_status.RollbackFailed)
                {
                    if (ptxPlan.rebuilt)
                        VirtualFree(ptxPlan.rebuilt, 0, MEM_RELEASE);
                    MfgQuality::DiscardUncommitted(qualityResult);
                    FreeLibrary(acquired);
                }
                else
                {
                    RetainPatchedProvider(acquired);
                    RetainQualityAllocation(qualityResult.warpAllocation);
                }
                LOG_WARN("MFG unlock: {}", g_status.TemporalDetail);
                return;
            }

            RetainPatchedProvider(acquired);
            g_status.KernelsRewritten = kernelCount;
            g_status.AdvertiseMatched = true;
            g_status.ValidateMatched = true;
            if (quality.mode != 0)
            {
                RetainQualityAllocation(qualityResult.warpAllocation);
                g_status.QualityWarp = quality.warp || quality.mode == 4;
                g_status.QualityDetail = std::format("quality profile {} with {}", quality.mode,
                                                     g_status.QualityWarp ? "warp path" : "native warp");
            }
            g_status.TemporalDetail = quality.mode != 0
                                          ? "complete quality profile and gate transaction"
                                          : method == TemporalMethod::Retarget
                                              ? "complete Blackwell retarget and gate transaction"
                                              : "complete PTX descriptor and gate transaction";
            LOG_INFO("MFG unlock: nvngx_dlssg.dll patched for {} generated frames ({})", kMaxGeneratedFrames,
                     g_status.TemporalDetail);
            if (quality.mode != 0)
                LOG_INFO("[MFGQUALITY] mode={} warp={} applied=true kernels={} provider={} detail={}",
                         quality.mode, g_status.QualityWarp, kernelCount, g_status.SnippetVersion,
                         g_status.QualityDetail);

            // A plugin that was loaded first has been waiting for this.
            PatchPluginCeilings();
        }
    }
}

namespace
{
MfgUnlock::Telemetry g_telemetry;
}

const MfgUnlock::Telemetry& MfgUnlock::GetTelemetry() { return g_telemetry; }

bool MfgUnlock::SoftwarePacing() { return std::string_view(LastStatus().FlipMetering) == "patched"; }

void MfgUnlock::RecordSetOptions(unsigned int requested, unsigned int sent, bool active, unsigned int result)
{
    // Above 2X with hardware flip metering still on can freeze presentation. Say so once, and change
    // nothing: whether it freezes depends on the game and the plugin build.
    static std::atomic_bool warned { false };

    if (active && sent > 1 && UnlockedMax() > 0 && !SoftwarePacing() &&
        !Config::Instance()->DisableFlipMetering.value_or(false) && !warned.exchange(true))
        LOG_WARN("MFG unlock: {}X requested with hardware flip metering still on. If presentation freezes, try "
                 "[NvApi] DisableFlipMetering=true, then [DLSSG] AdaFlipMeteringPatch=true.",
                 sent + 1);

    g_telemetry.requested.store(requested, std::memory_order_relaxed);
    g_telemetry.sent.store(sent, std::memory_order_relaxed);
    g_telemetry.result.store(result, std::memory_order_relaxed);
    g_telemetry.active.store(active, std::memory_order_relaxed);
    g_telemetry.optionsSeen.store(true, std::memory_order_release);
}

void MfgUnlock::RecordState(unsigned int presented)
{
    g_telemetry.presented.store(presented, std::memory_order_relaxed);

    auto seenMax = g_telemetry.maxPresented.load(std::memory_order_relaxed);
    while (presented > seenMax &&
           !g_telemetry.maxPresented.compare_exchange_weak(seenMax, presented, std::memory_order_relaxed))
    {
    }

    g_telemetry.stateSeen.store(true, std::memory_order_release);
}

void MfgUnlock::OnStreamlinePluginLoaded(HMODULE plugin)
{
    if (plugin == nullptr || !AdaUnlockWanted())
        return;

    {
        std::lock_guard lock(g_mutex);

        if (std::find(g_plugins.begin(), g_plugins.end(), plugin) == g_plugins.end())
        {
            g_plugins.push_back(plugin);

            // At load, ahead of any use of the plugin.
            PatchFlipMetering(plugin);
        }
    }

    // A no-op until the snippet unlock has landed; TryApply calls it again then.
    PatchPluginCeilings();
}

unsigned int MfgUnlock::UnlockedMax()
{
    std::lock_guard lock(g_mutex);
    return g_retainedProvider && !g_status.PatchFailed && g_status.AdvertiseMatched &&
                   g_status.ValidateMatched && g_status.KernelsRewritten > 0
               ? kMaxGeneratedFrames : 0;
}

unsigned int MfgUnlock::EffectiveMax(unsigned int nativeMaximum)
{
    std::lock_guard lock(g_mutex);
    if (g_status.PatchFailed)
        return 1;
    const unsigned int verified = g_retainedProvider && g_status.AdvertiseMatched && g_status.ValidateMatched &&
                                          g_status.KernelsRewritten > 0
                                      ? kMaxGeneratedFrames : 0;
    return std::max(nativeMaximum, verified);
}

MfgUnlock::Failure MfgUnlock::LastFailure()
{
    std::lock_guard lock(g_mutex);
    return g_status.RollbackFailed ? Failure::RollbackFailed
                                   : g_status.PatchFailed ? Failure::PatchFailed : Failure::None;
}

bool MfgUnlock::Pending()
{
    if (!EnabledForSession() || LastStatus().ModuleFound)
        return false;
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    return gpu.vendorId == VendorId::Nvidia && gpu.nvidiaArchInfo.architecture_id == NV_GPU_ARCHITECTURE_AD100;
}

MfgUnlock::Status MfgUnlock::LastStatus()
{
    std::lock_guard lock(g_mutex);
    return g_status;
}

bool MfgUnlock::EnabledForSession()
{
    // Latch before the first FG load/query. UI changes take effect on the next launch.
    static const bool enabled = Config::Instance()->FGDLSSGAdaMfgUnlock.value_or_default();
    return enabled;
}

MfgUnlock::TemporalMethod MfgUnlock::ConfiguredTemporalMethod()
{
    const auto* config = Config::Instance();

    std::optional<std::string> fix;
    if (config->FGDLSSGAdaTemporalFix.has_value())
        fix = config->FGDLSSGAdaTemporalFix.value_or("Auto");

    return ResolveTemporalMethod(fix);
}

#endif
