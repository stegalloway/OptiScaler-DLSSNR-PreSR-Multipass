#pragma once

struct ID3D12GraphicsCommandList;

namespace DlssNr
{
unsigned long long SubmissionEpoch_Dx12(ID3D12GraphicsCommandList* commandList);
}