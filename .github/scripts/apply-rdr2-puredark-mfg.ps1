param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path $SourceRoot).Path

function Write-Utf8NoBom {
    param([string]$Path, [string]$Text)
    [IO.File]::WriteAllText($Path, $Text.Replace("`r`n","`n"), [Text.UTF8Encoding]::new($false))
}

function Replace-LiteralOne {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Old,
        [Parameter(Mandatory=$true)][string]$New,
        [Parameter(Mandatory=$true)][string]$Label
    )
    $text = [IO.File]::ReadAllText($Path).Replace("`r`n","`n")
    $oldN = $Old.Replace("`r`n","`n")
    $newN = $New.Replace("`r`n","`n")
    $first = $text.IndexOf($oldN, [StringComparison]::Ordinal)
    if ($first -lt 0) { throw "Could not locate $Label in $Path. Upstream changed; refusing to guess." }
    $second = $text.IndexOf($oldN, $first + $oldN.Length, [StringComparison]::Ordinal)
    if ($second -ge 0) { throw "Found more than one $Label in $Path. Refusing an ambiguous patch." }
    Write-Utf8NoBom -Path $Path -Text ($text.Substring(0,$first) + $newN + $text.Substring($first + $oldN.Length))
}

function Add-Rdr2PureDarkGuard {
    param(
        [Parameter(Mandatory=$true)][string]$RelativePath,
        [Parameter(Mandatory=$true)][string]$SignaturePattern,
        [Parameter(Mandatory=$true)][string]$Label
    )
    $path = Join-Path $root $RelativePath
    $text = [IO.File]::ReadAllText($path).Replace("`r`n","`n")
    $marker = "RDR2 PureDark external FG: skipping global $Label hook"
    if ($text.Contains($marker)) {
        Write-Host "$Label guard already present."
        return
    }
    $rx = [regex]::new($SignaturePattern + '\s*\{\s*', [Text.RegularExpressions.RegexOptions]::Multiline)
    $matches = $rx.Matches($text)
    if ($matches.Count -ne 1) {
        throw "Expected exactly one $Label hook entry point in $RelativePath; found $($matches.Count)."
    }
    $m = $matches[0]
    $guard = @"
    if (State::Instance().externalFrameGeneration &&
        _wcsicmp(Util::ExePath().filename().c_str(), L"RDR2.exe") == 0)
    {
        LOG_INFO("$marker");
        return;
    }

"@
    $patched = $text.Substring(0, $m.Index) + $m.Value + $guard + $text.Substring($m.Index + $m.Length)
    Write-Utf8NoBom -Path $path -Text $patched
}

Add-Rdr2PureDarkGuard -RelativePath 'OptiScaler/hooks/Dxgi_Hooks.cpp' -SignaturePattern 'void\s+DxgiHooks::Hook\(\)' -Label 'DXGI'
Add-Rdr2PureDarkGuard -RelativePath 'OptiScaler/hooks/D3D11_Hooks.cpp' -SignaturePattern 'void\s+D3D11Hooks::Hook\(HMODULE\s+dx11Module\)' -Label 'D3D11'
Add-Rdr2PureDarkGuard -RelativePath 'OptiScaler/hooks/D3D12_Hooks.cpp' -SignaturePattern 'void\s+D3D12Hooks::Hook\(\)' -Label 'D3D12 export'
Add-Rdr2PureDarkGuard -RelativePath 'OptiScaler/hooks/D3D12_Hooks.cpp' -SignaturePattern 'void\s+D3D12Hooks::HookAgility\(HMODULE\s+module\)' -Label 'D3D12 Agility'

# Header-only private submission clock. It observes only command lists that actually reached
# the NR seam, then advances only after the real ExecuteCommandLists call returns.
$epochHeader = Join-Path $root 'OptiScaler/shaders/dlssnr/DlssNr_Rdr2Epoch.h'
$epochMarker = 'RDR2 PureDark coexistence: installed NR submission epoch hook'
if (-not (Test-Path $epochHeader)) {
    $header = @'
#pragma once

#include <State.h>
#include <Util.h>
#include <detours/detours.h>

#include <atomic>
#include <mutex>
#include <set>

namespace DlssNrRdr2Epoch
{
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

inline ExecuteCommandListsFn gExecuteCommandLists = nullptr;
inline std::mutex gMutex;
inline std::set<ID3D12CommandList*> gPending;
inline std::atomic<unsigned long long> gEpoch { 0 };

inline bool Enabled()
{
    return State::Instance().externalFrameGeneration &&
           _wcsicmp(Util::ExePath().filename().c_str(), L"RDR2.exe") == 0;
}

inline void STDMETHODCALLTYPE ExecuteCommandLists(ID3D12CommandQueue* queue, UINT count,
                                                  ID3D12CommandList* const* lists)
{
    bool submitted = false;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        for (UINT i = 0; i < count; ++i)
        {
            const auto it = gPending.find(lists[i]);
            if (it != gPending.end())
            {
                gPending.erase(it);
                submitted = true;
            }
        }
    }

    gExecuteCommandLists(queue, count, lists);

    if (submitted)
    {
        const auto epoch = gEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (epoch <= 3 || (epoch % 600) == 0)
            LOG_INFO("RDR2 PureDark coexistence: submitted DLSS command list, NR epoch {}", epoch);
    }
}

inline bool Ensure(ID3D12Device* device)
{
    if (!Enabled() || device == nullptr)
        return false;

    std::lock_guard<std::mutex> lock(gMutex);
    if (gExecuteCommandLists != nullptr)
        return true;

    D3D12_COMMAND_QUEUE_DESC desc {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;

    ID3D12CommandQueue* probe = nullptr;
    if (FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&probe))) || probe == nullptr)
    {
        LOG_ERROR("RDR2 PureDark coexistence: could not create probe queue for NR submission tracking");
        return false;
    }

    auto** vtable = *reinterpret_cast<void***>(probe);
    gExecuteCommandLists = reinterpret_cast<ExecuteCommandListsFn>(vtable[10]);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)gExecuteCommandLists, ExecuteCommandLists);
    const LONG result = DetourTransactionCommit();
    probe->Release();

    if (result != NO_ERROR)
    {
        LOG_ERROR("RDR2 PureDark coexistence: NR submission epoch hook failed: 0x{:X}",
                  static_cast<unsigned long>(result));
        gExecuteCommandLists = nullptr;
        return false;
    }

    LOG_INFO("RDR2 PureDark coexistence: installed NR submission epoch hook");
    return true;
}

inline unsigned long long Track(ID3D12GraphicsCommandList* commandList)
{
    if (!Enabled() || commandList == nullptr)
        return State::Instance().frameCount;

    ID3D12Device* device = nullptr;
    if (FAILED(commandList->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr)
        return State::Instance().frameCount;

    const bool hooked = Ensure(device);
    device->Release();
    if (!hooked)
        return State::Instance().frameCount;

    {
        std::lock_guard<std::mutex> lock(gMutex);
        gPending.insert(static_cast<ID3D12CommandList*>(commandList));
    }
    return gEpoch.load(std::memory_order_acquire);
}

inline void Shutdown()
{
    std::lock_guard<std::mutex> lock(gMutex);
    gPending.clear();

    if (gExecuteCommandLists == nullptr)
        return;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID&)gExecuteCommandLists, ExecuteCommandLists);
    const LONG result = DetourTransactionCommit();
    if (result != NO_ERROR)
        LOG_WARN("RDR2 PureDark coexistence: NR submission epoch unhook failed: 0x{:X}",
                 static_cast<unsigned long>(result));

    gExecuteCommandLists = nullptr;
    gEpoch.store(0, std::memory_order_release);
}
} // namespace DlssNrRdr2Epoch
'@
    Write-Utf8NoBom -Path $epochHeader -Text $header
}
elseif (-not ([IO.File]::ReadAllText($epochHeader)).Contains($epochMarker)) {
    throw 'An unexpected DlssNr_Rdr2Epoch.h already exists. Refusing to overwrite it.'
}

# All native DX12 NR schedules flow through IFeature_Dx12::Evaluate in v0.8.4.
# Replace only the native frameCount assignment; interop bridges keep their submitted epoch.
$feature = Join-Path $root 'OptiScaler/upscalers/IFeature_Dx12.cpp'
$featureText = [IO.File]::ReadAllText($feature)
if (-not $featureText.Contains('DlssNr_Rdr2Epoch.h')) {
    Replace-LiteralOne -Path $feature -Old '#include <dlssnr/DlssNr_Pipeline_Dx12.h>' -New @'
#include <dlssnr/DlssNr_Pipeline_Dx12.h>
#include <shaders/dlssnr/DlssNr_Rdr2Epoch.h>
'@.TrimEnd("`r","`n") -Label 'RDR2 epoch include insertion'
}
$featureText = [IO.File]::ReadAllText($feature).Replace("`r`n","`n")
if (-not $featureText.Contains('submissionEpoch = DlssNrRdr2Epoch::Track(InCommandList);')) {
    Replace-LiteralOne -Path $feature -Old @'
    const bool interop = timingQueue != nullptr;
    if (!interop)
        submissionEpoch = State::Instance().frameCount;
'@.TrimEnd("`r","`n") -New @'
    const bool interop = timingQueue != nullptr;
    if (!interop)
        submissionEpoch = DlssNrRdr2Epoch::Track(InCommandList);
'@.TrimEnd("`r","`n") -Label 'native DX12 NR submission epoch'
}

# Detach the private queue detour during NR shutdown.
$nr = Join-Path $root 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp'
$nrText = [IO.File]::ReadAllText($nr)
if (-not $nrText.Contains('DlssNr_Rdr2Epoch.h')) {
    Replace-LiteralOne -Path $nr -Old '#include "DlssNr_SeamClock.h"' -New @'
#include "DlssNr_SeamClock.h"
#include "DlssNr_Rdr2Epoch.h"
'@.TrimEnd("`r","`n") -Label 'RDR2 epoch shutdown include'
}
$nrText = [IO.File]::ReadAllText($nr).Replace("`r`n","`n")
if (-not $nrText.Contains('DlssNrRdr2Epoch::Shutdown();')) {
    Replace-LiteralOne -Path $nr -Old 'void Shutdown() { WaitForFinishedPicture(); }' -New @'
void Shutdown()
{
    WaitForFinishedPicture();
    DlssNrRdr2Epoch::Shutdown();
}
'@.TrimEnd("`r","`n") -Label 'RDR2 epoch shutdown call'
}

# PureDark/Streamline can hand late-NR tracking a proxy device. Use the same real-object
# unwrapping mechanism already used elsewhere in ResTrack before installing queue hooks.
$resTrack = Join-Path $root 'OptiScaler/resource_tracking/ResTrack_dx12.cpp'
$resText = [IO.File]::ReadAllText($resTrack).Replace("`r`n","`n")
$lateMarker = 'RDR2 PureDark coexistence: late-NR tracking uses unwrapped D3D12 device'
if (-not $resText.Contains($lateMarker)) {
    Replace-LiteralOne -Path $resTrack -Old @'
void ResTrack_Dx12::HookLateNrQueue(ID3D12Device* device)
{
    static std::mutex hookMutex;
    std::lock_guard<std::mutex> lock(hookMutex);
    HookToQueue(device);
'@.TrimEnd("`r","`n") -New @'
void ResTrack_Dx12::HookLateNrQueue(ID3D12Device* device)
{
    static std::mutex hookMutex;
    std::lock_guard<std::mutex> lock(hookMutex);

    if (device != nullptr && State::Instance().externalFrameGeneration &&
        _wcsicmp(Util::ExePath().filename().c_str(), L"RDR2.exe") == 0)
    {
        ID3D12Device* realDevice = nullptr;
        if (CheckForRealObject("RDR2 PureDark late-NR device", device, (IUnknown**)&realDevice) &&
            realDevice != nullptr)
        {
            device = realDevice;
            LOG_INFO("RDR2 PureDark coexistence: late-NR tracking uses unwrapped D3D12 device");
        }
        else
        {
            LOG_INFO("RDR2 PureDark coexistence: late-NR device was already native");
        }
    }

    HookToQueue(device);
'@.TrimEnd("`r","`n") -Label 'late-NR queue device unwrapping'
}

# The release must contain the built-in RTX40/Ada patcher. In v0.8.4 it is compatible
# with External=true without an RDR2-specific source override. If that changes later, fail closed.
$mfgPath = Join-Path $root 'OptiScaler/framegen/dlssg/MfgUnlock.cpp'
$mfg = [IO.File]::ReadAllText($mfgPath)
$tryStart = $mfg.IndexOf('void MfgUnlock::TryApply', [StringComparison]::Ordinal)
$pendingStart = $mfg.IndexOf('bool MfgUnlock::Pending', [StringComparison]::Ordinal)
if ($tryStart -lt 0 -or $pendingStart -le $tryStart) {
    throw 'Could not verify the upstream RTX40 MFG patcher layout.'
}
$tryBlock = $mfg.Substring($tryStart, $pendingStart - $tryStart)
if ($tryBlock.Contains('externalFrameGeneration')) {
    throw 'The upstream MFG patcher now blocks External frame generation. Manual review is required before updating the RDR2 build.'
}

$required = @(
    @{ Path='OptiScaler/hooks/Dxgi_Hooks.cpp'; Text='RDR2 PureDark external FG: skipping global DXGI hook' },
    @{ Path='OptiScaler/hooks/D3D11_Hooks.cpp'; Text='RDR2 PureDark external FG: skipping global D3D11 hook' },
    @{ Path='OptiScaler/hooks/D3D12_Hooks.cpp'; Text='RDR2 PureDark external FG: skipping global D3D12 export hook' },
    @{ Path='OptiScaler/hooks/D3D12_Hooks.cpp'; Text='RDR2 PureDark external FG: skipping global D3D12 Agility hook' },
    @{ Path='OptiScaler/shaders/dlssnr/DlssNr_Rdr2Epoch.h'; Text='RDR2 PureDark coexistence: installed NR submission epoch hook' },
    @{ Path='OptiScaler/upscalers/IFeature_Dx12.cpp'; Text='DlssNrRdr2Epoch::Track(InCommandList)' },
    @{ Path='OptiScaler/resource_tracking/ResTrack_dx12.cpp'; Text='RDR2 PureDark coexistence: late-NR tracking uses unwrapped D3D12 device' },
    @{ Path='OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp'; Text='D3D12Hooks::HookDevice(InDevice)' }
)
foreach ($item in $required) {
    $p = Join-Path $root $item.Path
    if (-not ([IO.File]::ReadAllText($p)).Contains($item.Text)) {
        throw "RDR2 coexistence verification failed: $($item.Text)"
    }
}

Write-Host 'RDR2 PureDark coexistence, private NR submission epoch, late-NR unwrapping and RTX40 MFG compatibility applied.'
