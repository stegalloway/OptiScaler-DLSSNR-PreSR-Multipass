// Optional real-driver LOAD ONLY test. Never launches a CUDA kernel or game FG.
#define NOMINMAX
#include <windows.h>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "../../OptiScaler/framegen/dlssg/MfgQuality.h"
using CUdevice=int; using CUcontext=void*; using CUmodule=void*; using CUfunction=void*;
void Check(int error, const char* what)
{ if (error) throw std::runtime_error(std::string(what)+" CUDA error="+std::to_string(error)); }
void Require(bool condition, const char* what) { if (!condition) throw std::runtime_error(what); }

int main(int argc, char** argv) try
{
    Require(argc == 2 || argc == 3, "Pass provider path and optional magnitude-only cubin path");
    auto provider = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    Require(provider != nullptr, "provider mapping");
    namespace t = mfgunlock::thingeometry::internal;
    const auto* profile = t::ProfileFor(mfgunlock::thingeometry::Mechanism::ValidatedWarpBlend);
    t::LocatedFatbin located;
    std::vector<uint8_t> upstream;
    std::string why;
    Require(t::LocateUniqueFatbin(provider,*profile,located,why), why.c_str());
    Require(t::BuildRedirectedFatbin(located.address,located.size,*profile,upstream,why), why.c_str());
    auto selected = upstream;
    Require(MfgQuality::SelectWarpPtxForAda(selected,why), why.c_str());
    auto malformed = upstream;
    malformed.pop_back();
    Require(!MfgQuality::SelectWarpPtxForAda(malformed,why), "truncated fatbin accepted");
    Require(!MfgQuality::SelectWarpPtxForAda(selected,why), "already selected fatbin accepted twice");
    std::string directPtx;
    unsigned choices = 0;
    for (size_t at=16; at+64<=selected.size();)
    {
        const auto header=t::ReadU32(selected.data()+at+4);
        const auto bytes=t::ReadU64(selected.data()+at+8);
        if (t::ReadU32(selected.data()+at+28)==89)
        {
            ++choices;
            Require(t::ReadU16(selected.data()+at)==1 && t::ReadU32(selected.data()+at+16)==0,
                    "old Ada cubin/PTX still selectable");
            directPtx.assign(reinterpret_cast<const char*>(selected.data()+at+header), static_cast<size_t>(bytes));
        }
        at+=header+static_cast<size_t>(bytes);
    }
    Require(choices==1, "exactly one Ada warp image required");
    directPtx.push_back('\0');
    MfgQuality::Result prepared;
    Require(MfgQuality::Prepare(provider,{2,true},{},prepared), prepared.detail.c_str());
    Require(prepared.warpAllocation && std::memcmp(prepared.warpAllocation,selected.data(),selected.size())==0,
            "production adapter did not use the validated selection");
    // Adaptive is an exclusive profile: its warp rewrite must be selected by
    // the production adapter, and planning it must not leak the vendor-global
    // adaptive switch into another profile's later planning.
    mfgunlock::thingeometry::g_adaptive_quality_enabled = true;
    std::vector<uint8_t> adaptiveRebuild;
    Require(t::BuildRedirectedFatbin(located.address,located.size,*profile,adaptiveRebuild,why), why.c_str());
    mfgunlock::thingeometry::g_adaptive_quality_enabled = false;
    Require(MfgQuality::SelectWarpPtxForAda(adaptiveRebuild,why), why.c_str());
    MfgQuality::Result adaptivePrepared;
    Require(MfgQuality::Prepare(provider,{4,false},{},adaptivePrepared), adaptivePrepared.detail.c_str());
    Require(!mfgunlock::thingeometry::g_adaptive_quality_enabled,
            "production adaptive planning leaked its global selection");
    Require(adaptivePrepared.warpAllocation &&
                std::memcmp(adaptivePrepared.warpAllocation,adaptiveRebuild.data(),adaptiveRebuild.size())==0,
            "production adapter did not select the complete adaptive warp profile");
    auto driver=LoadLibraryExW(L"nvcuda.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    Require(driver!=nullptr,"system CUDA driver missing");
    auto init=reinterpret_cast<int(*)(unsigned)>(GetProcAddress(driver,"cuInit"));
    auto getDevice=reinterpret_cast<int(*)(CUdevice*,int)>(GetProcAddress(driver,"cuDeviceGet"));
    auto getName=reinterpret_cast<int(*)(char*,int,CUdevice)>(GetProcAddress(driver,"cuDeviceGetName"));
    auto capability=reinterpret_cast<int(*)(int*,int*,CUdevice)>(GetProcAddress(driver,"cuDeviceComputeCapability"));
    auto create=reinterpret_cast<int(*)(CUcontext*,unsigned,CUdevice)>(GetProcAddress(driver,"cuCtxCreate_v2"));
    auto destroy=reinterpret_cast<int(*)(CUcontext)>(GetProcAddress(driver,"cuCtxDestroy_v2"));
    auto load=reinterpret_cast<int(*)(CUmodule*,const void*)>(GetProcAddress(driver,"cuModuleLoadData"));
    auto unload=reinterpret_cast<int(*)(CUmodule)>(GetProcAddress(driver,"cuModuleUnload"));
    auto getFunction=reinterpret_cast<int(*)(CUfunction*,CUmodule,const char*)>(GetProcAddress(driver,"cuModuleGetFunction"));
    auto attribute=reinterpret_cast<int(*)(int*,int,CUfunction)>(GetProcAddress(driver,"cuFuncGetAttribute"));
    Require(init&&getDevice&&getName&&capability&&create&&destroy&&load&&unload&&getFunction&&attribute,"CUDA entry points");
    Check(init(0),"init"); CUdevice device; Check(getDevice(&device,0),"device");
    int major=0,minor=0;Check(capability(&major,&minor,device),"capability");
    Require(major==8 && minor==9,"this load/selection test requires an actual Ada GPU");
    char name[256]{};Check(getName(name,256,device),"name");std::cout<<"Actual GPU: "<<name<<'\n';
    CUcontext context;Check(create(&context,0,device),"context");
    auto inspect=[&](const void* data,const char* label){
        CUmodule module;Check(load(&module,data),label);CUfunction fn;
        Check(getFunction(&fn,module,"Kernel_BlendCandidatesFused"),"warp function");
        std::array<int,7> values{};
        for(int i=0;i<7;++i) Check(attribute(&values[i],i,fn),"function attributes");
        std::cout<<label<<" regs="<<values[4]<<" shared="<<values[1]<<" binary="<<values[6]<<'\n';
        Check(unload(module),"unload");return values;
    };
    const auto original=inspect(located.address,"original provider warp");
    const auto unadjusted=inspect(upstream.data(),"unadjusted upstream rebuild");
    const auto expected=inspect(directPtx.c_str(),"modified PTX loaded directly");
    const auto actual=inspect(prepared.warpAllocation,"OptiScaler selected warp");
    const auto adaptiveActual=inspect(adaptivePrepared.warpAllocation,"OptiScaler selected adaptive warp");
    Require(unadjusted==original,"unadjusted selection observation changed; review test");
    Require(actual==expected && actual!=original,"new warp kernel selection not established");
    Require(adaptiveActual!=original,"adaptive warp kernel selection not established");
#if MFGUNLOCK_HAS_GENERATED_BLACKWELL_CUBINS && MFGUNLOCK_HAS_GENERATED_THIN_GEOMETRY_CUBINS
    for (const auto& kernel : mfgunlock::blackwell::generated::kCubinPatches)
    { CUmodule module;Check(load(&module,kernel.data),kernel.what);Check(unload(module),"unload baseline");std::cout<<"PASS CUDA loads "<<kernel.what<<'\n'; }
    for (const auto& kernel : mfgunlock::blackwell::generated_thin_geometry::kThinGeometryCubins)
    { CUmodule module;Check(load(&module,kernel.data),kernel.mechanism);Check(unload(module),"unload variant");std::cout<<"PASS CUDA loads "<<kernel.mechanism<<'\n'; }
#else
    throw std::runtime_error("generated tables absent");
#endif
    if (argc == 3)
    {
        std::ifstream cubin(argv[2], std::ios::binary | std::ios::ate);
        Require(cubin.good() && cubin.tellg() == 38432, "magnitude-only cubin size/identity input");
        const auto bytes = static_cast<size_t>(cubin.tellg());
        cubin.seekg(0);
        std::vector<uint8_t> image(bytes);
        cubin.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(bytes));
        Require(cubin.good(), "read magnitude-only cubin");
        CUmodule module;
        Check(load(&module, image.data()), "magnitude-only cubin load");
        CUfunction function;
        Check(getFunction(&function, module, "Kernel_EstimateIntermMvecsScatter"),
              "magnitude-only kernel selection");
        Check(unload(module), "unload magnitude-only cubin");
        std::cout << "PASS CUDA loads magnitude-only research cubin (not calibrated/deployed)\n";
    }
    Check(destroy(context),"destroy");
    MfgQuality::DiscardUncommitted(adaptivePrepared);
    MfgQuality::DiscardUncommitted(prepared);FreeLibrary(provider);FreeLibrary(driver);
    std::cout<<"PASS CUDA load/selection tests: zero kernel launches; no initialized NGX/game FG\n";
    return 0;
}
catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
