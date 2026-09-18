#pragma once

struct ID3D12GraphicsCommandList;
struct ID3D12CommandList;

namespace DlssNr
{
unsigned long long SubmissionEpoch_Dx12(ID3D12GraphicsCommandList* commandList);

// Called from the RDR2/PureDark-coexistence-only Reset() hook in D3D12_Hooks.cpp -- once per
// Reset() call on any command list in the process, whether or not NR is currently enabled, since
// hook installation is tied to the game (RDR2.exe) and cannot react to a config value changing
// later. `succeeded` must be SUCCEEDED() of the real Reset() call's HRESULT: a failed Reset did
// not start a new recording lifetime, so it must not advance the tracked generation.
void NoteRdr2CommandListReset(ID3D12CommandList* commandList, bool succeeded);

// Identity for ExposureScan::Tick()'s per-frame dedup only -- NOT the same value as
// SubmissionEpoch_Dx12(). That epoch advances only once a frame's command list is CONFIRMED
// submitted at ExecuteCommandLists, which FinishedCompose relies on for consecutive-frame
// ordering. Tick() only needs "was this call already processed", and feeding it the confirmed
// epoch directly means a single dropped/aborted RDR2 frame (unmatched submission) freezes the
// scan -- and, with a scan-driven white point, the white point itself -- until the next match,
// silently. This instead changes the moment a genuinely new recording is detected on
// `commandList` (a different list, or the same list since its last successful Reset()), so a
// dropped frame can stall this by at most one call. For every other game this returns
// State::Instance().frameCount, identically to SubmissionEpoch_Dx12.
unsigned long long ScanTickEpoch_Dx12(ID3D12GraphicsCommandList* commandList);
} // namespace DlssNr