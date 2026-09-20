// Compiles the current production EncodeInput body, not a copied simulator.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#define LOG_WARN(...) ((void)0)
enum D3D12_RESOURCE_STATES { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
 D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST };
enum {D3D12_RESOURCE_DIMENSION_TEXTURE2D=3, DXGI_FORMAT_R32G32B32A32_FLOAT=2};
struct D3D12_RESOURCE_DESC { int Dimension=3,Format=2; unsigned Width=64,Height=64,DepthOrArraySize=1; struct {unsigned Count=1;} SampleDesc; };
struct ID3D12Resource { D3D12_RESOURCE_DESC desc; D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS; int writes=0; auto GetDesc(){return desc;} };
struct ID3D12Device{}; struct ID3D12GraphicsCommandList { void CopyResource(ID3D12Resource*d,ID3D12Resource*s){d->writes=s->writes;} };
template<class T> struct Option { T val{}; T value_or_default()const{return val;} };
using Scaler=int;
struct Config {
 Option<bool> DlssNrHoldFrame;
 Option<unsigned> DlssNrWhitePointSource,DlssNrReversibleMode;
 Option<float> DlssNrWhitePointScale{1};
 Option<Scaler> DlssNrScalingDownscaler;
 static Config* Instance(){static Config c;return &c;}
};
enum {DlssNrMode_Encode=0,DlssNrMode_Downsample=1,DlssNrMode_Meter=2,DlssNrMode_AutoExposure=3};
struct DlssNrConstants { unsigned Mode=0,Width=0,Height=0,ExposureSourceWidth=0,ExposureSourceHeight=0,Passthrough=0,ReversibleMode=0;float WhitePoint=1,PreExposure=1; };
void Barrier(ID3D12GraphicsCommandList*,ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
 if(!r || r->state!=before) throw std::runtime_error("incorrect barrier state");
 r->state=after;
}
namespace DlssNr {
void ExposureConstants(DlssNrConstants& c,const Config&,unsigned,float p){c.PreExposure=p;}
}
struct OS_Dx12 { OS_Dx12(const char*,ID3D12Device*,bool,Scaler){} bool DispatchResources(ID3D12GraphicsCommandList*,ID3D12Resource*,ID3D12Resource*){return false;} };
struct Frame {bool ColourIsLinearHdr=true;float WhitePointOverride=0,PreExposure=1;void* ExposureTexture=nullptr;unsigned ExposureState=0;};
struct Shader {
 int failMode=-1;
 std::vector<unsigned> calls;
 bool DispatchPass(ID3D12GraphicsCommandList*,DlssNrConstants c,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*a,ID3D12Resource*b) {
  calls.push_back(c.Mode);
  if((int)c.Mode==failMode)return false;
  if(a){if(a->state!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS)throw std::runtime_error("write not UAV");++a->writes;}
  if(b){if(b->state!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS)throw std::runtime_error("write not UAV");++b->writes;}return true;
 }
};
struct DlssNr_Dx12 { struct State {
 struct NR {
  unsigned width=64,height=64,workWidth=64,workHeight=64,heldWidth=0,heldHeight=0,exposureSource=0;
  int heldFormat=0;bool heldActive=false,exposureReadable=false,exposureValid=false;
  float heldWhitePoint=1,exposurePreExposure=1;Scaler nrScaler=0;
  ID3D12Resource *heldColor=nullptr,*exposure=nullptr,*exposureMeter=nullptr,*colorCopy=nullptr,*hdrCopy=nullptr,*colorSmall=nullptr;
  OS_Dx12 *superUp=nullptr,*superDown=nullptr;
 } nr;
 Shader shader;bool warnedSuper=false;
 struct EncodeContext { ID3D12GraphicsCommandList* cmdList;ID3D12Device* device;ID3D12Resource* target;D3D12_RESOURCE_STATES targetState;Frame frame;float workScale=1;bool targetSupportsUav=true;float whitePoint=1;ID3D12Resource*modelInput=nullptr;ID3D12Resource*exposure=nullptr;DlssNrConstants exposureConstants{}; };
 std::vector<ID3D12Resource*> allocations;
 ~State(){for(auto*p:allocations)delete p;delete nr.superUp;delete nr.superDown;}
 void ParkNrResource(ID3D12Resource*&r){r=nullptr;}
 ID3D12Resource*CreateScratch(ID3D12Device*,int f,unsigned w,unsigned h){auto*r=new ID3D12Resource;r->desc.Format=f;r->desc.Width=w;r->desc.Height=h;allocations.push_back(r);return r;}
 void ReleaseSupersamplers(){delete nr.superUp;delete nr.superDown;nr.superUp=nr.superDown=nullptr;}
 bool EncodeInput(EncodeContext&);
}; };
#include "production-encode.inc"
static unsigned checks;
void Check(bool v){++checks;if(!v)throw std::runtime_error("encode safety invariant");}
int main(){
 for(int reduced=0;reduced<2;++reduced) for(int fail=-1;fail<2;++fail) for(int uav=0;uav<2;++uav){
  *Config::Instance()={};
  DlssNr_Dx12::State s;ID3D12Resource target,base,hdr,small;ID3D12Device dev;ID3D12GraphicsCommandList cmd;
  s.nr.colorCopy=&base;s.nr.hdrCopy=&hdr;s.nr.colorSmall=&small;s.shader.failMode=fail;
  if(reduced){s.nr.workWidth=s.nr.workHeight=32;}
  DlssNr_Dx12::State::EncodeContext c{&cmd,&dev,&target,target.state,{},reduced?.5f:1.f,uav!=0};
  bool ok=s.EncodeInput(c);
  Check(ok == (fail!=DlssNrMode_Encode && !(reduced && fail==DlssNrMode_Downsample)));
  if(!ok){Check(!c.modelInput);Check(base.state==D3D12_RESOURCE_STATE_UNORDERED_ACCESS && hdr.state==base.state && small.state==base.state);}
  else {Check(c.modelInput==(reduced?&small:&base));Check(hdr.writes==1 && hdr.state==D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
  if(fail==DlssNrMode_Encode)Check(hdr.writes==0 && base.writes==0);
 }
 std::cout<<checks<<" production encode checks passed\n";
 return 0;
}
