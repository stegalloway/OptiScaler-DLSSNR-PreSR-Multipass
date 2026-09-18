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
} // namespace DlssNr
