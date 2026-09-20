// Execute the exact embedded DX12 NRSTAB shader on WARP. No proprietary model/game required.
#define main active_color_fixture_main
#include "nr_active_color_smoke.cpp"
#undef main
#include "../OptiScaler/shaders/dlssnr/DlssNr_Common.h"
#include "../OptiScaler/shaders/dlssnr/precompile/dlssnr_residual_Shader.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <array>
#include <cmath>
#include <limits>
#include <cstring>
using Pixel = std::array<float,4>;
constexpr unsigned extent = 64;
using Pixels = std::vector<Pixel>;
Pixels solid(float value) { return Pixels(extent*extent, Pixel{value,value,value,1}); }

Pixels dispatch(ID3D12Device* device, const DlssNrConstants& constants, const Pixels& source,
                const Pixels& model, const Pixels& history, const Pixels& motion)
{
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC qd {};
    check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> commands;
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)));
    auto desc = texture(extent, extent, true); desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {}; UINT64 size = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &size);
    ComPtr<ID3D12Resource> textures[4], uploads[4];
    const Pixels* pixels[] = {&source,&model,&history,&motion};
    for (unsigned i=0;i<4;++i)
    {
        textures[i] = create(device,desc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        uploads[i] = buffer(device,size,false);
        unsigned char* mapped=nullptr; D3D12_RANGE noRead{0,0};
        check(uploads[i]->Map(0,&noRead,(void**)&mapped));
        for(unsigned y=0;y<extent;++y)
            std::memcpy(mapped+y*footprint.Footprint.RowPitch,pixels[i]->data()+y*extent,extent*sizeof(Pixel));
        uploads[i]->Unmap(0,nullptr);
        auto from=location(uploads[i].Get()), to=location(textures[i].Get());
        from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint=footprint;
        commands->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        barrier(commands.Get(),textures[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    auto output=create(device,desc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto readback=buffer(device,size,true), params=buffer(device,sizeof(constants),false);
    void* mapped=nullptr; D3D12_RANGE noRead{0,0};
    check(params->Map(0,&noRead,&mapped)); std::memcpy(mapped,&constants,sizeof(constants)); params->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=7; hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap; check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    auto handle=heap->GetCPUDescriptorHandleForHeapStart();
    const auto stride=device->GetDescriptorHandleIncrementSize(hd.Type);
    for(unsigned i=0;i<5;++i) {
        device->CreateShaderResourceView(textures[i<4?i:0].Get(),nullptr,handle); handle.ptr+=stride;
    }
    for(unsigned i=0;i<2;++i) {
        device->CreateUnorderedAccessView(output.Get(),nullptr,nullptr,handle); handle.ptr+=stride;
    }
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors=5;
    ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors=2;
    ranges[1].OffsetInDescriptorsFromTableStart=5;
    D3D12_ROOT_PARAMETER roots[2]{};
    roots[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
    roots[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    roots[1].DescriptorTable={2,ranges};
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS; sampler.MaxLOD=D3D12_FLOAT32_MAX;
    D3D12_ROOT_SIGNATURE_DESC signature{2,roots,1,&sampler,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob,errors; check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors));
    ComPtr<ID3D12RootSignature> root;
    check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{}; pipeline.pRootSignature=root.Get();
    pipeline.CS={dlssnr_residual_cso,sizeof(dlssnr_residual_cso)};
    ComPtr<ID3D12PipelineState> pso; check(device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&pso)));
    ID3D12DescriptorHeap* heaps[]={heap.Get()};
    commands->SetDescriptorHeaps(1,heaps); commands->SetComputeRootSignature(root.Get()); commands->SetPipelineState(pso.Get());
    commands->SetComputeRootConstantBufferView(0,params->GetGPUVirtualAddress());
    commands->SetComputeRootDescriptorTable(1,heap->GetGPUDescriptorHandleForHeapStart());
    commands->Dispatch((constants.Width+7)/8,(constants.Height+7)/8,1);
    barrier(commands.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    auto from=location(output.Get()),to=location(readback.Get());
    to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint=footprint;
    commands->CopyTextureRegion(&to,0,0,0,&from,nullptr); check(commands->Close());
    ID3D12CommandList* lists[]={commands.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); expect(event!=nullptr,"event");
    check(queue->Signal(fence.Get(),1)); check(fence->SetEventOnCompletion(1,event));
    const auto result=WaitForSingleObject(event,30000); CloseHandle(event); expect(result==WAIT_OBJECT_0,"shader timeout");
    D3D12_RANGE read{0,(SIZE_T)size}; check(readback->Map(0,&read,&mapped));
    Pixels out(extent*extent);
    for(unsigned y=0;y<extent;++y)
        std::memcpy(out.data()+y*extent,(unsigned char*)mapped+y*footprint.Footprint.RowPitch,extent*sizeof(Pixel));
    D3D12_RANGE noWrite{0,0}; readback->Unmap(0,&noWrite); return out;
}

int main() try
{
    ComPtr<ID3D12ShaderReflection> reflect;
    check(D3DReflect(dlssnr_residual_cso,sizeof(dlssnr_residual_cso),IID_PPV_ARGS(&reflect)));
    auto cb=reflect->GetConstantBufferByName("Params");
    for(auto field: {std::pair{"gResidualMotionSign",offsetof(DlssNrConstants,ResidualMotionSign)},
                    std::pair{"gResidualOutputHeight",offsetof(DlssNrConstants,ResidualOutputHeight)},
                    std::pair{"gResidualBlend",offsetof(DlssNrConstants,ResidualBlend)}})
    {
        D3D12_SHADER_VARIABLE_DESC vd{}; check(cb->GetVariableByName(field.first)->GetDesc(&vd));
        expect(vd.StartOffset==field.second,"CPU/shader layout mismatch");
    }
    ComPtr<IDXGIFactory4> factory; check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> warp; check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    ComPtr<ID3D12Device> device; check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    DlssNrConstants c{}; c.Mode=DlssNrResidualMode_VarianceSelect;
    c.Width=c.Height=c.GuideWidth=c.GuideHeight=c.ResidualFrameWidth=c.ResidualFrameHeight=extent;
    c.ResidualOutputWidth=c.ResidualOutputHeight=extent;
    c.MvScaleX=c.MvScaleY=1.0f/extent; c.ResidualMotionSign=1; c.ResidualBlend=1; c.MaxRatio=1;
    auto source=solid(1),model=solid(1),history=solid(0.3f),motion=solid(0);
    for(unsigned i=0;i<model.size();++i) {
        const float v=1.0f+((i+i/extent)%2 ? 0.2f:0.4f); model[i]={v,v,v,1};
    }
    auto verify=[&](const Pixels& out,bool useHistory) {
        for(unsigned y=0;y<extent;++y) for(unsigned x=0;x<extent;++x) {
            const auto i=y*extent+x;
            const float expected=useHistory && x && y && x+1<extent && y+1<extent ? 0.3f : model[i][0]-1;
            expect(std::abs(out[i][0]-expected)<0.00001f,"binary selection / edge / cold history");
        }
    };
    verify(dispatch(device.Get(),c,source,model,history,motion),false);
    c.ResidualHistoryValid=1; verify(dispatch(device.Get(),c,source,model,history,motion),true);
    verify(dispatch(device.Get(),c,source,model,solid(10),motion),false);
    const unsigned center=32*extent+32;
    motion[center+1][0]=0.75f;
    auto low=dispatch(device.Get(),c,source,model,history,motion);
    expect(std::abs(low[center][0]-0.3f)<0.00001f,"render pixel gate");
    c.ResidualOutputWidth=c.ResidualOutputHeight=extent*2;
    auto high=dispatch(device.Get(),c,source,model,history,motion);
    expect(std::abs(high[center][0]-(model[center][0]-1))<0.00001f,"output-pixel motion gate");
    motion[center][0]=std::numeric_limits<float>::quiet_NaN();
    auto invalid=dispatch(device.Get(),c,source,model,history,motion);
    expect(std::abs(invalid[center][0]-(model[center][0]-1))<0.00001f,"nonfinite motion fallback");
    c.ResidualHistoryValid=0;
    auto clipped=dispatch(device.Get(),c,source,solid(-1),history,solid(0));
    expect(clipped[center][0]==-1,"visible residual clipping");
    c.Mode=DlssNrResidualMode_Apply; c.TransferStrength=1;
    auto applied=dispatch(device.Get(),c,source,clipped,history,solid(0));
    expect(applied[center][0]==0 && applied[center][3]==1,"residual application");
    puts("PASS: embedded shader layout, binary selector, cold history, edges, output-pixel motion gate, finite fallback and visible residual");

    c.Mode=DlssNrResidualMode_MvSelfTest;
    for(int direction: {1,-1}) {
        auto current=solid(0),previous=solid(0),mv=solid(0);
        for(unsigned y=0;y<extent;++y) for(unsigned x=0;x<extent;++x) {
            const auto i=y*extent+x; const float a=float(x)/extent, b=float(int(x)+direction)/extent;
            previous[i]={a,a,a,1}; current[i]={b,b,b,1}; mv[i]={1,0,0,1};
        }
        auto diag=dispatch(device.Get(),c,current,model,previous,mv);
        unsigned valid=0; double e0=0,ep=0,em=0;
        for(const auto& p:diag) if(p[3]>0.5f) { ++valid; e0+=p[0]; ep+=p[1]; em+=p[2]; }
        expect(valid>=128 && (direction>0 ? ep:em)<e0*0.01 && (direction>0 ? em:ep)>e0,
               "motion convention direction");
    }
    auto noMotion=dispatch(device.Get(),c,source,model,history,solid(0));
    for(const auto& p:noMotion) expect(p[3]==0,"stationary diagnostic voted");
    puts("PASS: startup diagnostic plus/minus conventions and zero-motion rejection");
    return 0;
}
catch(const std::exception& e) { fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
