// Production copy-back / history lambdas extracted by Run-FinalSafety.ps1.
// Uses single-mip resources: stock v0.8.5 copy/barrier policy is intentionally unchanged.
// Model/selector outcomes are injected; real D3D12 copies and readbacks verify the resource contract.
#define main existing_active_main
#include "nr_active_color_smoke.cpp"
#undef main
#define Barrier barrier
void testHostCopies(ID3D12Device* device, unsigned route, unsigned outcome)
{
 const bool cropColor=route==2, targetSupportsUav=route!=0;
 const unsigned width=cropColor?53:64,height=cropColor?27:32;
 auto gd=texture(64,32,route==1);gd.MipLevels=1;
 auto game=create(device,gd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
 auto scratch=texture(width,height,true);
 auto compact=create(device,scratch,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 auto hdr=create(device,scratch,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 auto prev=create(device,scratch,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 auto stab=create(device,scratch,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 struct {ID3D12Resource *activeColor,*hdrCopy,*stabPrevBase,*stabResolved;
 bool stabPrevBaseReadable=false,stabPrevBaseValid=false,stabHistoryValid=true;} nr{compact.Get(),hdr.Get(),prev.Get(),stab.Get()};
 ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};
 check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
 ComPtr<ID3D12CommandAllocator> allocator;check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
 ComPtr<ID3D12GraphicsCommandList> commands;check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)));
 auto* cmdList=commands.Get();
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp[1]{};UINT64 bytes=0;
 device->GetCopyableFootprints(&gd,0,1,0,fp,nullptr,nullptr,&bytes);
 auto initial=buffer(device,bytes,false),final=buffer(device,bytes,true),history=buffer(device,bytes,true);
 unsigned char* memory=nullptr;D3D12_RANGE noRead{0,0};
 check(initial->Map(0,&noRead,(void**)&memory));
 constexpr uint32_t original=0x11112222,raw=0x33334444,selected=0x55556666,padding=0xABCD1234;
 for(unsigned mip=0;mip<1;++mip)for(unsigned y=0;y<fp[mip].Footprint.Height;++y)for(unsigned x=0;x<fp[mip].Footprint.Width;++x)
  reinterpret_cast<uint32_t*>(memory+fp[mip].Offset+y*fp[mip].Footprint.RowPitch)[x]=(mip==0&&x<width&&y<height)?original:padding+mip;
 initial->Unmap(0,nullptr);
 for(unsigned mip=0;mip<1;++mip){auto a=location(initial.Get()),b=location(game.Get());a.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;a.PlacedFootprint=fp[mip];b.SubresourceIndex=mip;cmdList->CopyTextureRegion(&b,0,0,0,&a,nullptr);}
 const auto outputArrival=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
 Barrier(cmdList,game.Get(),D3D12_RESOURCE_STATE_COPY_DEST,outputArrival);
 ID3D12Resource*target=game.Get();auto targetState=outputArrival;
 const auto active=std::optional<DlssNr::ColorExtent>{{width,height}};
 const auto TransitionTarget=[&](D3D12_RESOURCE_STATES next){Barrier(cmdList,target,targetState,next);targetState=next;};
 #include "production-crop-finish.inc"
 // Inject a successful current-frame clean encode (codec is tested separately on WARP).
 const auto saved=targetState;TransitionTarget(D3D12_RESOURCE_STATE_COPY_SOURCE);
 Barrier(cmdList,nr.hdrCopy,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
 cmdList->CopyResource(nr.hdrCopy,target);
 TransitionTarget(saved);
 Barrier(cmdList,nr.hdrCopy,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 #include "production-store-base.inc"
 expect(StoreCurrentBase() && nr.stabPrevBaseValid,"history capture failed");
 Barrier(cmdList,nr.stabPrevBase,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
 auto a=location(nr.stabPrevBase),b=location(history.Get());b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=fp[0];
 D3D12_BOX box{0,0,0,width,height,1};cmdList->CopyTextureRegion(&b,0,0,0,&a,&box);
 Barrier(cmdList,nr.stabPrevBase,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 const bool nrStabFrameActive=outcome==2;
 const bool resolved=outcome!=5;
 bool compositionSucceeded=resolved;
 bool nrStabHdrCopyWritable=false;
 std::vector<ComPtr<ID3D12Resource>> uploads;
 auto write=[&](ID3D12Resource*r,D3D12_RESOURCE_STATES before,uint32_t value){
  auto u=buffer(device,bytes,false);unsigned char*p=nullptr;check(u->Map(0,&noRead,(void**)&p));
  for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x)reinterpret_cast<uint32_t*>(p+y*fp[0].Footprint.RowPitch)[x]=value;
  u->Unmap(0,nullptr);Barrier(cmdList,r,before,D3D12_RESOURCE_STATE_COPY_DEST);
  auto src=location(u.Get()),dst=location(r);src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp[0];
  cmdList->CopyTextureRegion(&dst,0,0,0,&src,&box);
  Barrier(cmdList,r,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  uploads.push_back(u);
 };
 // OFF/warming/guide rejection/selector rejection all produce raw NR when composition succeeds.
 if(targetSupportsUav) {
  if(resolved){write(target,targetState,nrStabFrameActive?selected:raw);targetState=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;}
 } else if(nrStabFrameActive) write(nr.stabResolved,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,selected);
 else {
  Barrier(cmdList,nr.hdrCopy,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  nrStabHdrCopyWritable=true;
  if(resolved)write(nr.hdrCopy,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,raw);
 }
 #include "production-copy-back.inc"
 FinishColor(compositionSucceeded);
 if(cropColor)expect(targetState==D3D12_RESOURCE_STATE_UNORDERED_ACCESS,"active staging rest state");
 else expect(targetState==outputArrival,"game rest state");
 expect(compositionSucceeded==resolved,"composition result changed");
 Barrier(cmdList,game.Get(),outputArrival,D3D12_RESOURCE_STATE_COPY_SOURCE);
 for(unsigned mip=0;mip<1;++mip){
  
  auto src=location(game.Get()),dst=location(final.Get());src.SubresourceIndex=mip;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp[mip];cmdList->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
 }
 check(cmdList->Close());ID3D12CommandList*lists[]{cmdList};queue->ExecuteCommandLists(1,lists);
 ComPtr<ID3D12Fence> fence;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
 HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);expect(done!=nullptr,"event");check(queue->Signal(fence.Get(),1));check(fence->SetEventOnCompletion(1,done));
 auto wait=WaitForSingleObject(done,30000);CloseHandle(done);expect(wait==WAIT_OBJECT_0,"GPU timeout");
 check(history->Map(0,nullptr,(void**)&memory));
 for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)expect(reinterpret_cast<uint32_t*>(memory+y*fp[0].Footprint.RowPitch)[x]==original,"history captured proxy/output instead of clean active colour");
 history->Unmap(0,nullptr);
 check(final->Map(0,nullptr,(void**)&memory));
 for(unsigned mip=0;mip<1;++mip)for(unsigned y=0;y<fp[mip].Footprint.Height;++y)for(unsigned x=0;x<fp[mip].Footprint.Width;++x){
  uint32_t expected=padding+mip;
  if(mip==0&&x<width&&y<height)expected=resolved?(nrStabFrameActive?selected:raw):original;
  expect(reinterpret_cast<uint32_t*>(memory+fp[mip].Offset+y*fp[mip].Footprint.RowPitch)[x]==expected,"wrong final surface, stale output, padding or mip corruption");
 }
 final->Unmap(0,nullptr);
 printf("PASS: production history/copy-back route=%u outcome=%u\n",route,outcome);
}
int main()try {
 ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
 ComPtr<IDXGIAdapter> adapter;check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
 ComPtr<ID3D12Device> device;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
 for(unsigned route=0;route<3;++route)for(unsigned outcome=0;outcome<6;++outcome)testHostCopies(device.Get(),route,outcome);
 puts("PASS: 18 production copy contracts; injected model outcomes; not NGX runtime validation");
 return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
