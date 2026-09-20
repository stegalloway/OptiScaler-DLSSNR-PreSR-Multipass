// Test-only shader entry points appended to actual production sources, never deployed.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3d11shader.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iostream>
#include <vector>
#include "nr_layout512_expected.h"
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
static void Check(HRESULT hr,const char* what) { if(FAILED(hr)) throw std::runtime_error(std::string(what)+" HRESULT="+std::to_string(unsigned(hr))); }
static void Require(bool v,const char* what) { if(!v) throw std::runtime_error(what); }
static ComPtr<ID3DBlob> Compile(const std::string& source,const char* entry) {
    ComPtr<ID3DBlob> code,errors;
    const auto hr=D3DCompile(source.data(),source.size(),"production-with-test-entry",nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(errors) std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
    Check(hr,"compile"); return code;
}
static bool Validate(ID3DBlob* code,unsigned shader,bool wrongLast=false) {
    ComPtr<ID3D11ShaderReflection> reflection;
    Check(D3DReflect(code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&reflection)),"reflection");
    auto* cb=reflection->GetConstantBufferByName("Params");
    D3D11_SHADER_BUFFER_DESC bd{}; if(FAILED(cb->GetDesc(&bd))) return false;
    if(bd.Size!=272 || bd.Variables!=50) return false;
    for(unsigned i=0;i<50;++i) {
        auto* v=cb->GetVariableByName(kShaderFields[shader][i]);
        D3D11_SHADER_VARIABLE_DESC d{}; D3D11_SHADER_TYPE_DESC t{};
        if(FAILED(v->GetDesc(&d)) || FAILED(v->GetType()->GetDesc(&t))) return false;
        const auto& expected=kLayoutFields[i];
        const auto offset=wrongLast && i==49 ? 252u : expected.offset;
        if(d.StartOffset!=offset || d.Size!=expected.size) return false;
        if(t.Type!=(expected.isFloat?D3D_SVT_FLOAT:D3D_SVT_UINT)) return false;
    }
    return true;
}
static bool RangeFits(unsigned bytes) { return bytes%256==0 && bytes>=260; }
static D3D12_RESOURCE_DESC Buffer(UINT64 size,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;return d;
}
int wmain(int argc,wchar_t** argv) try {
    Require(argc==3,"arguments: production precompile directory, fresh evidence directory");
    const fs::path pre=argv[1],out=argv[2];fs::create_directories(out);
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter> adapter;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP");
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    // Debug-layer availability is diagnostic only; do not silently claim validation coverage.
    ComPtr<ID3D12Debug> debug; const bool debugAvailable=SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    std::cout<<"DX12 WARP; debug layer available="<<debugAvailable<<" (not enabled for this test)\n";
    D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;Check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"queue");
    ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");Check(list->Close(),"initial close");
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"event");
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_CBV;ranges[0].NumDescriptors=1;
    ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=1;ranges[1].BaseShaderRegister=7;ranges[1].OffsetInDescriptorsFromTableStart=1;
    D3D12_ROOT_PARAMETER root{};root.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;root.DescriptorTable={2,ranges};
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&root;
    ComPtr<ID3DBlob> serialized,errors;Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors),"serialize root");
    ComPtr<ID3D12RootSignature> signature;Check(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&signature)),"root signature");
    const auto resource=[&](UINT64 size,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE) {
        D3D12_HEAP_PROPERTIES hp{};hp.Type=type;auto desc=Buffer(size,flags);ComPtr<ID3D12Resource> result;
        Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result)),"resource");return result;
    };
    auto constants=resource(sizeof(DlssNrConstants)*kSlotCount,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto output=resource(kWordCount*4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto readback=resource(kWordCount*4,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=kSlotCount*2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"heap");
    const auto stride=device->GetDescriptorHandleIncrementSize(hd.Type);
    for(unsigned slot=0;slot<kSlotCount;++slot) {
        auto cpu=heap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=slot*2*stride;
        Require(RangeFits(sizeof(DlssNrConstants)),"CBV size guard");
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{constants->GetGPUVirtualAddress()+slot*sizeof(DlssNrConstants),sizeof(DlssNrConstants)};device->CreateConstantBufferView(&cbv,cpu);
        cpu.ptr+=stride;D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Buffer.NumElements=kWordCount;uav.Buffer.StructureByteStride=4;device->CreateUnorderedAccessView(output.Get(),nullptr,&uav,cpu);
    }
    UINT64 serial=0;
    for(unsigned shader=0;shader<3;++shader) {
        std::ifstream input(pre/kShaderFiles[shader]);Require(input.good(),"source open");
        std::string source((std::istreambuf_iterator<char>(input)),{});
        std::ostringstream probe;probe<<source<<"\n#if defined(VK_MODE) || defined(VULKAN)\n[[vk::binding(7,0)]]\n#endif\nRWStructuredBuffer<uint> layoutWords : register(u7);\n[numthreads(1,1,1)] void LayoutProbe() {\n";
        for(unsigned f=0;f<50;++f) {
            const auto& expected=kLayoutFields[f];
            for(unsigned w=0;w<expected.size/4;++w) {
                std::string expression=kShaderFields[shader][f];
                if(expected.size>4) expression+="["+std::to_string(w/4)+"]["+std::to_string(w%4)+"]";
                probe<<"layoutWords["<<expected.offset/4+w<<"]="<<(expected.isFloat?"asuint(":"uint(")<<expression<<");\n";
            }
        }
        probe<<"}\n";const std::string testSource=probe.str();
        // Persist the exact generated test translation unit for DXC/SPIR-V verification.
        std::ofstream saved(out/(std::string(kShaderFiles[shader])+".probe.hlsl"),std::ios::binary);saved<<testSource;saved.close();
        auto code=Compile(testSource,"LayoutProbe");
        Require(Validate(code.Get(),shader),"production declaration differs from frozen map");
        Require(!Validate(code.Get(),shader,true),"negative wrong-offset validator did not reject");
        Require(!RangeFits(256) && !RangeFits(320),"negative truncated/unaligned CBV accepted");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=signature.Get();pd.CS={code->GetBufferPointer(),code->GetBufferSize()};
        ComPtr<ID3D12PipelineState> pipeline;Check(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipeline)),"probe PSO");
        for(unsigned round=0;round<kRounds;++round) {
            const auto slot=round%kSlotCount;auto values=MakeSentinels(shader,round);void* mapped=nullptr;D3D12_RANGE empty{0,0};
            Check(constants->Map(0,&empty,&mapped),"upload map");std::memcpy(static_cast<unsigned char*>(mapped)+slot*sizeof(values),&values,sizeof(values));constants->Unmap(0,nullptr);
            Check(allocator->Reset(),"allocator reset");Check(list->Reset(allocator.Get(),pipeline.Get()),"list reset");
            ID3D12DescriptorHeap* heaps[]={heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(signature.Get());
            auto gpu=heap->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=slot*2*stride;list->SetComputeRootDescriptorTable(0,gpu);list->Dispatch(1,1,1);
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
            list->CopyBufferRegion(readback.Get(),0,output.Get(),0,kWordCount*4);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);Check(list->Close(),"close");
            ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);Check(queue->Signal(fence.Get(),++serial),"signal");Check(fence->SetEventOnCompletion(serial,event),"completion event");
            Require(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"GPU completion timeout");Require(fence->GetCompletedValue()!=UINT64_MAX && fence->GetCompletedValue()>=serial,"invalid completion");
            D3D12_RANGE range{0,kWordCount*4};Check(readback->Map(0,&range,&mapped),"readback map");
            const auto* words=static_cast<const uint32_t*>(mapped);bool good=MatchesSentinels(words,shader,round);std::array<uint32_t,kWordCount> corrupt{};std::memcpy(corrupt.data(),words,kWordCount*4);corrupt[64]^=1;
            const bool negative=!MatchesSentinels(corrupt.data(),shader,round);readback->Unmap(0,&empty);Require(good,"sentinel word mismatch (including offset 256)");Require(negative,"corruption validator accepted wrong tail");
        }
        std::cout<<"PASS DX12 "<<kShaderFiles[shader]<<": 50 field offsets, 65 words x 6 writes / 3 slots, tail offset 256; wrong-offset/range/corruption negatives rejected\n";
    }
    CloseHandle(event);std::cout<<"PASS: 1170 sentinel words, explicit submitted-fence completion; no game or NGX runtime\n";return 0;
} catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<"\n";return 1; }
