// Executes the complete production DlssNr::RenderMenu implementation.
// Only the host, ImGui widget API and unrelated subpanels are test seams.
// This proves call-chain reachability/edits/status, not in-game pixels or GPU activity.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "../OptiScaler/dlssnr/DlssNr_Placement.h"
#include "../OptiScaler/dlssnr/DlssNr_NrStabStatus.h"
template<class T> struct Option {
 T value; T value_or_default() const {return value;}
 Option& operator=(T v){value=v;return *this;}
};
struct Config {
 Option<bool> DlssNrEnabled{true},DlssNrStabilizerEnabled{true},DlssNrApplyModel{true};
 Option<bool> DlssNrRunBeforeSr{true},DlssNrFinishedPicture{false},DlssNrDeferredDlss{false},DlssNrResidualAcrossRr{false};
 Option<float> DlssNrStabilizerK{1},DlssNrStabilizerMotionRejectPx{2},DlssNrWorkingScale{1};
 Option<unsigned> DlssNrPasses{1},DlssNrDebugView{0},DlssNrCompare{0};
 Option<int> DlssNrPrivateUpscaler{0};
};
enum class API {DX11,DX12,Vulkan};
enum class Upscaler {DLSS,DLSSD};
enum class SwapchainInteropApi {None,Dx11wDx12};
struct Feature {
 API api=API::DX12;bool bridge=false;
 API Api(){return api;} bool IsWithDx12(){return bridge;}
 Upscaler GetUpscalerType(){return Upscaler::DLSS;}
};
struct State {
 Feature*currentFeature=nullptr;API swapchainApi=API::DX12;
 SwapchainInteropApi swapchainInteropApi=SwapchainInteropApi::None;
 std::vector<double> frameTimes;void*currentFG=nullptr;double lastFGFrameTime=16,presentFrameTime=16;
 static State& Instance(){static State s;return s;}
};
struct ImVec2 {float x=800,y=600;};
struct ImVec4 {float x,y,z,w;};
enum {ImGuiCol_Text,ImGuiHoveredFlags_AllowWhenDisabled};
namespace Test {
 std::vector<std::string> labels,text;
 std::string click;float sliderValue=0;bool sliderEdit=false;
 unsigned checks=0,section=0,subpanel=0;
 bool open=true;
 void Check(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
 void Clear(){labels.clear();text.clear();click.clear();sliderEdit=false;}
 bool Has(const std::vector<std::string>&v,const std::string&s){
  return std::any_of(v.begin(),v.end(),[&](auto&x){return x.find(s)!=std::string::npos;});
 }
}
namespace ImGui {
 struct Style{ImVec2 ItemSpacing{8,8};};
 Style& GetStyle(){static Style s;return s;}
 ImVec2 GetContentRegionAvail(){return {};}
 float GetCursorPosX(){return 0;}
 ImVec4 GetStyleColorVec4(int){return {1,1,1,1};}
 void PushStyleColor(int,ImVec4){} void PopStyleColor(){}
 void Spacing(){} void Separator(){} void SameLine(float=0){}
 void SeparatorText(const char*s){Test::labels.push_back(s);}
 void PushItemWidth(float){} void PopItemWidth(){}
 std::vector<bool> disabled;
 void BeginDisabled(bool b=true){disabled.push_back(b || (!disabled.empty() && disabled.back()));}
 void EndDisabled(){disabled.pop_back();}
 bool Disabled(){return !disabled.empty() && disabled.back();}
 bool IsItemHovered(int=0){return false;}
 void SetTooltip(const char*,...){}
 void AddText(const char*fmt,va_list args){char s[2048];vsnprintf(s,sizeof(s),fmt,args);Test::text.push_back(s);}
 void Text(const char*fmt,...){va_list a;va_start(a,fmt);AddText(fmt,a);va_end(a);}
 void TextWrapped(const char*fmt,...){va_list a;va_start(a,fmt);AddText(fmt,a);va_end(a);}
 void TextDisabled(const char*fmt,...){va_list a;va_start(a,fmt);AddText(fmt,a);va_end(a);}
 void TextUnformatted(const char*s){Test::text.push_back(s);}
 bool Checkbox(const char*s,bool*v){Test::labels.push_back(s);if(!Disabled()&&Test::click==s){*v=!*v;return true;}return false;}
 bool SliderFloat(const char*s,float*v,float,float,const char*){
  Test::labels.push_back(s);if(!Disabled()&&Test::sliderEdit&&Test::click==s){*v=Test::sliderValue;return true;}return false;
 }
 bool SmallButton(const char*s){Test::labels.push_back(s);return !Disabled()&&Test::click==s;}
 bool Combo(const char*s,int*,const char*){Test::labels.push_back(s);return false;}
 bool CollapsingHeader(const char*){return false;}
}
void HelpMarker(const char*){}
struct ScopedCollapsingHeader {explicit ScopedCollapsingHeader(const char*){} bool IsHeaderOpen(){return Test::open;}};
struct ScopedIndent {};
namespace DlssNr {
 enum class Backend {Dx12,Vulkan};
 struct Status {bool running=true;std::string failureReason;std::optional<float> gpuTime;unsigned long long frames=0;};
 Status ReadStatus(Backend b){Status s;s.running=b==Backend::Dx12;return s;}
 void RetryAfterFailure(){}
 std::string FinishedVkStatus(){return "finished vk";}
 std::string FinishedPictureStatus(){return "finished dx12";}
 std::string DeferredDlssStatus(){return "deferred";}
 int GetPrivateUpscaler(int i){return i;} const char*PrivateUpscalerName(int){return "DLSS";}
 namespace MenuSections {
  void RenderInput(Config*){Test::subpanel=1;}
  void RenderModel(Config*){Test::subpanel=2;}
  void RenderBlend(Config*){Test::subpanel=3;}
  void RenderInspect(Config*){}
 }
 namespace PipelineUi {
  enum class Section{Placement,Input,Model,Blend};
  enum class Route{FinishedBefore,Finished,Deferred,Before,After};
  struct View {const char*privateUpscaler;bool enabled,applyModel,rayReconstruction;unsigned passes;int scalePercent;Route route;};
  const char*SectionName(Section s){static const char*n[]={"Placement","Input","Model","Blend"};return n[(int)s];}
  bool CheckboxWrapped(const char*s,bool*v,float){return ImGui::Checkbox(s,v);}
  void Draw(const View&,Section&s){s=(Section)Test::section;}
  void DrawTimingBar(double,double){}
 }
}
#include "production-menu.inc"
void Render(Config&c){DlssNr::RenderMenu(&c,1);Test::Check(ImGui::disabled.empty(),"unbalanced disabled stack");}
void Visible(){
 for(auto*s:{"Stabilization active","Residual confidence K","Motion rejection (output px)","Reset stabilizer defaults"})
  Test::Check(Test::Has(Test::labels,s),"NRSTAB control unreachable through main menu");
}
int main()try {
 Feature feature;State::Instance().currentFeature=&feature;
 // This is the original regression: finished-picture OFF, both ordinary placements,
 // every selected subpanel. Testing a helper in isolation would miss it.
 for(bool before:{false,true})for(unsigned section=0;section<4;++section){
  Test::Clear();Config c;c.DlssNrRunBeforeSr=before;Test::section=section;Test::subpanel=0;
  Render(c);Visible();Test::Check(Test::subpanel==section,"selected section changed");
 }
 // Stale ACTIVE runtime values must not imply active processing in these modes.
 for(unsigned mode=0;mode<8;++mode){
  Test::Clear();Config c;feature.api=API::DX12;
  DlssNr::NrStabUi::Publish(DlssNr::NrStabUi::ACTIVE,1,true,1,1);
  switch(mode){
   case 0:c.DlssNrEnabled=false;break;
   case 1:c.DlssNrStabilizerEnabled=false;break;
   case 2:c.DlssNrFinishedPicture=true;break;
   case 3:c.DlssNrDeferredDlss=true;break;
   case 4:c.DlssNrResidualAcrossRr=true;break;
   case 5:feature.api=API::Vulkan;break;
   case 6:c.DlssNrApplyModel=false;break;
   case 7:c.DlssNrDebugView=1;break;
  }
  Render(c);Visible();
  Test::Check(!Test::Has(Test::text,"ACTIVE"),"stale active status in ineligible mode");
  Test::Check(Test::Has(Test::text,"NRSTAB inactive")||Test::Has(Test::text,"NRSTAB unavailable")||Test::Has(Test::text,"BYPASSED"),"missing route explanation");
 }
 feature.api=API::DX12;
 Test::Clear();Config c;c.DlssNrCompare=1;Render(c);Visible();
 Test::Check(!Test::Has(Test::text,"ACTIVE"),"comparison must suppress stale active status");
 c={};
 Test::Clear();Test::click="Stabilization active";Render(c);
 Test::Check(!c.DlssNrStabilizerEnabled.value,"toggle didn't update configuration");
 Test::Clear();Test::click="Residual confidence K";Test::sliderEdit=true;Test::sliderValue=1.4f;Render(c);
 Test::Check(c.DlssNrStabilizerK.value==1,"disabled control edited configuration");
 Test::Clear();Test::click="Stabilization active";Render(c);
 Test::Check(c.DlssNrStabilizerEnabled.value,"toggle didn't re-enable");
 for(auto pair:{std::pair{"Residual confidence K",1.25f},std::pair{"Motion rejection (output px)",1.75f}}){
  Test::Clear();Test::click=pair.first;Test::sliderEdit=true;Test::sliderValue=pair.second;Render(c);
 }
 Test::Check(c.DlssNrStabilizerK.value==1.25f && c.DlssNrStabilizerMotionRejectPx.value==1.75f,"live knobs not saved");
 Test::Clear();Test::click="Reset stabilizer defaults";Render(c);
 Test::Check(c.DlssNrStabilizerK.value==1 && c.DlssNrStabilizerMotionRejectPx.value==2,"reset defaults");
 Test::Clear();Render(c);Test::Check(Test::Has(Test::text,"ACTIVE"),"eligible runtime status missing");
 Test::Clear();Test::open=false;Render(c);
 Test::Check(!Test::Has(Test::labels,"Stabilization active"),"closed parent must remain closed");
 printf("PASS: %u production RenderMenu assertions; pre/post x four subpanels, inactive explanations, A/B, knobs, defaults\n",Test::checks);
 return 0;
}catch(const std::exception&e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}

