#include "EffectHistory.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace EffectHistory {
namespace {
const GUID BindingsTag={0xa749ac87,0xb72f,0x4165,{0x90,0xe4,0x36,0xc7,0x07,0x8e,0xd4,0x58}};
struct Bindings {UINT role{},color{},depth{},ao{},output{};};
thread_local Bindings pixel{},compute{};
thread_local bool active{},enabled{},trace{};
thread_local bool probesOff{};
thread_local int eye=-1;
thread_local uint64_t frame{};
std::filesystem::path directory;uintptr_t gameBase{};
enum Type {Color,Depth,Ao,Count};
struct Image {ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;uint64_t frame{};};
Image history[Count][2];
struct Watch {Type type{};ComPtr<ID3D11Resource> destination,source;uintptr_t caller{};uint64_t tick{};};
Watch watched[16];unsigned cursor{};
ComPtr<ID3D11ShaderResourceView> nativeColor,nativeDepth,nativeAo;
struct Restore {UINT slot{};ComPtr<ID3D11ShaderResourceView> view;};
thread_local std::vector<Restore> pixelRestore,computeRestore;
thread_local ComPtr<ID3D11Texture2D> aoOutput;
uint64_t substitutions[Count][2]{};
ComPtr<ID3D11Texture2D> probeTexture;
ComPtr<ID3D11RenderTargetView> probeTarget;
uint64_t probeCleared[2]{};
void log(const std::string& message){std::ofstream f(directory/L"effect-history.log",std::ios::app);f<<message<<'\n';}
Bindings read(ID3D11DeviceChild* shader){Bindings result{};if(shader){UINT size=sizeof(result);if(FAILED(shader->GetPrivateData(BindingsTag,&size,&result)))result={};}return result;}
void watch(Type type,ID3D11ShaderResourceView* srv){
 if(!srv)return;ComPtr<ID3D11Resource> target;srv->GetResource(&target);
 for(auto& old:watched)if(old.destination.Get()==target.Get() && old.type==type)return;
 auto& w=watched[cursor++%_countof(watched)];w={};w.type=type;w.destination=target;
}
Watch* source(Type type){Watch* result{};for(auto& w:watched)if(w.type==type && w.source && (!result || w.tick>result->tick))result=&w;return result;}
bool store(ID3D11DeviceContext* c,Type type,int which,ID3D11Texture2D* src,ID3D11ShaderResourceView* native){
 if(which<0 || which>1 || !src || !native)return false;
 ComPtr<ID3D11Resource> target;ComPtr<ID3D11Texture2D> original;native->GetResource(&target);target.As(&original);if(!original)return false;
 D3D11_TEXTURE2D_DESC input{},expected{};src->GetDesc(&input);original->GetDesc(&expected);
 if(input.Width!=expected.Width || input.Height!=expected.Height || input.MipLevels!=expected.MipLevels || input.ArraySize!=1 || expected.ArraySize!=1 || input.SampleDesc.Count!=1 || expected.SampleDesc.Count!=1 || input.Format!=expected.Format)return false;
 auto& h=history[type][which];
 if(h.texture){D3D11_TEXTURE2D_DESC prior{};h.texture->GetDesc(&prior);if(prior.Width!=expected.Width || prior.Height!=expected.Height || prior.Format!=expected.Format || prior.MipLevels!=expected.MipLevels){h={};}}
 if(!h.texture){
  expected.BindFlags=D3D11_BIND_SHADER_RESOURCE;expected.Usage=D3D11_USAGE_DEFAULT;expected.CPUAccessFlags=0;expected.MiscFlags=0;
  ComPtr<ID3D11Device> d;src->GetDevice(&d);D3D11_SHADER_RESOURCE_VIEW_DESC view{};native->GetDesc(&view);
  if(FAILED(d->CreateTexture2D(&expected,nullptr,&h.texture)) || FAILED(d->CreateShaderResourceView(h.texture.Get(),&view,&h.view))){h={};return false;}
  log("allocate type="+std::to_string(type)+" eye="+std::to_string(which)+" size="+std::to_string(expected.Width)+"x"+std::to_string(expected.Height)+" format="+std::to_string(expected.Format)+" mips="+std::to_string(expected.MipLevels));
 }
 c->CopyResource(h.texture.Get(),src);h.frame=frame;
 if(trace)log("store frame="+std::to_string(frame)+" eye="+std::to_string(which)+" type="+std::to_string(type)+" source="+std::to_string(reinterpret_cast<uintptr_t>(src)));
 return true;
}
void substitute(ID3D11DeviceContext* c,Type type,UINT slot,ID3D11ShaderResourceView* native,bool cs){
 auto& h=history[type][eye];if(!enabled || !h.view || !h.frame || h.frame>=frame)return;
 auto& saved=cs?computeRestore:pixelRestore;saved.push_back({slot,native});auto* v=h.view.Get();
 if(cs)c->CSSetShaderResources(slot,1,&v);else c->PSSetShaderResources(slot,1,&v);
 ++substitutions[type][eye];if(trace)log("substitute frame="+std::to_string(frame)+" eye="+std::to_string(eye)+" type="+std::to_string(type)+" historyFrame="+std::to_string(h.frame));
}
}
void Initialize(const std::filesystem::path& path,uintptr_t base){directory=path;gameBase=base;}
void RegisterShader(ID3D11DeviceChild* shader,const void* code,SIZE_T size){
 ComPtr<ID3D11ShaderReflection> reflection;if(!shader || FAILED(D3DReflect(code,size,__uuidof(ID3D11ShaderReflection),&reflection)))return;
 D3D11_SHADER_DESC desc{};if(FAILED(reflection->GetDesc(&desc)))return;
 Bindings b{};bool hasColor=false,hasDepth=false,hasAo=false,hasVelocity=false,hasUser1=false,hasUser3=false,hasPost=false,hasScene=false,hasGrade=false;
 for(UINT i=0;i<desc.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC r{};if(FAILED(reflection->GetResourceBindingDesc(i,&r)) || !r.Name)continue;
  if(!strcmp(r.Name,"sLastFrameColorBuffer")){hasColor=true;b.color=r.BindPoint;}
  if(!strcmp(r.Name,"sLastFrameHiZBuffer")){hasDepth=true;b.depth=r.BindPoint;}
  if(!strcmp(r.Name,"sAO") && r.Type==D3D_SIT_UAV_RWTYPED){hasAo=true;b.output=r.BindPoint;}
  if(!strcmp(r.Name,"sVelocityBuffer"))hasVelocity=true;
  if(!strcmp(r.Name,"sUserResource1")){hasUser1=true;b.ao=r.BindPoint;}
  if(!strcmp(r.Name,"sUserResource3"))hasUser3=true;
  if(!strcmp(r.Name,"CBufferPostProcessConstant_Z"))hasPost=true;
  if(!strcmp(r.Name,"s0")){hasScene=true;b.color=r.BindPoint;}
  if(!strcmp(r.Name,"s3_3D"))hasGrade=true;
 }
 if(hasColor && hasDepth)b.role=1;else if(hasAo && hasVelocity && hasUser1 && hasUser3)b.role=2;else if(hasPost && hasScene && hasGrade)b.role=3;
 // This captured native pass normalizes the reflection accumulator t3 and
 // composites it into scene lighting even when the probe producer is off.
 uint64_t hash=14695981039346656037ull;for(SIZE_T i=0;i<size;++i){hash^=static_cast<const uint8_t*>(code)[i];hash*=1099511628211ull;}
 if(hash==0xcd92d83e58117d62ull && hasUser1 && b.ao==3)b.role=4;
 if(b.role)shader->SetPrivateData(BindingsTag,sizeof(b),&b);
}
void Pixel(ID3D11PixelShader* shader){pixel=read(shader);}
void Compute(ID3D11ComputeShader* shader){compute=read(shader);}
void Begin(uint64_t id,bool tracked,bool on,bool capture,bool reflectionProbesOff){frame=id;active=tracked;enabled=on;trace=capture;probesOff=reflectionProbesOff;eye=-1;}
void Eye(int value){eye=value;}
void BeforeDraw(ID3D11DeviceContext* c){
 if(!active || eye<0 || eye>1)return;
 if(pixel.role==4 && probesOff){
  ComPtr<ID3D11ShaderResourceView> srv;c->PSGetShaderResources(pixel.ao,1,&srv);
  ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;
  if(!srv)return;srv->GetResource(&resource);resource.As(&texture);if(!texture)return;
  D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
  if(desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 || !(desc.BindFlags&D3D11_BIND_RENDER_TARGET))return;
  if(probeTexture.Get()!=texture.Get()){
   probeTexture=texture;probeTarget.Reset();probeCleared[0]=probeCleared[1]=0;
   ComPtr<ID3D11Device> d;texture->GetDevice(&d);
   D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=desc.Format;view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;view.Texture2D.MipSlice=0;
   if(FAILED(d->CreateRenderTargetView(texture.Get(),&view,&probeTarget)))return;
   log("reflection accumulator clear target size="+std::to_string(desc.Width)+"x"+std::to_string(desc.Height));
  }
  if(probeTarget && probeCleared[eye]!=frame){
   // The producer draw disappears with native probes disabled, but this
   // consumer remains. A zero accumulator removes the stale previous scene.
   // Unbind and restore its exact input; ClearRTV needs no output rebinding.
   ID3D11ShaderResourceView* unbound{};c->PSSetShaderResources(pixel.ao,1,&unbound);
   const float zero[4]{};c->ClearRenderTargetView(probeTarget.Get(),zero);
   auto* input=srv.Get();c->PSSetShaderResources(pixel.ao,1,&input);probeCleared[eye]=frame;
   if(trace)log("cleared reflection accumulator frame="+std::to_string(frame)+" eye="+std::to_string(eye));
  }
  return;
 }
 if(pixel.role==3){
  ComPtr<ID3D11ShaderResourceView> srv;c->PSGetShaderResources(pixel.color,1,&srv);
  ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;if(srv){srv->GetResource(&resource);resource.As(&texture);}
  // Capture this eye's final native HDR scene before tone mapping. The
  // format/mip guard prevents replacing the history with desktop color.
  store(c,Color,eye,texture.Get(),nativeColor.Get());return;
 }
 if(pixel.role!=1)return;
 ID3D11ShaderResourceView* color{};ID3D11ShaderResourceView* depth{};
 c->PSGetShaderResources(pixel.color,1,&color);c->PSGetShaderResources(pixel.depth,1,&depth);
 nativeColor.Attach(color);nativeDepth.Attach(depth);watch(Color,color);watch(Depth,depth);
 substitute(c,Color,pixel.color,color,false);substitute(c,Depth,pixel.depth,depth,false);
}
void AfterDraw(ID3D11DeviceContext* c){for(auto& r:pixelRestore){auto* v=r.view.Get();c->PSSetShaderResources(r.slot,1,&v);}pixelRestore.clear();}
void BeforeDispatch(ID3D11DeviceContext* c){
 if(!active || eye<0 || eye>1 || compute.role!=2)return;
 ID3D11ShaderResourceView* srv{};c->CSGetShaderResources(compute.ao,1,&srv);nativeAo.Attach(srv);watch(Ao,srv);
 ComPtr<ID3D11UnorderedAccessView> uav;c->CSGetUnorderedAccessViews(compute.output,1,&uav);
 aoOutput.Reset();if(uav){ComPtr<ID3D11Resource> resource;uav->GetResource(&resource);resource.As(&aoOutput);}
 substitute(c,Ao,compute.ao,srv,true);
}
void AfterDispatch(ID3D11DeviceContext* c,bool executed){
 for(auto& r:computeRestore){auto* v=r.view.Get();c->CSSetShaderResources(r.slot,1,&v);}computeRestore.clear();
 if(active && eye>=0 && eye<2 && compute.role==2 && aoOutput){if(executed)store(c,Ao,eye,aoOutput.Get(),nativeAo.Get());aoOutput.Reset();}
}
void Copied(ID3D11Resource* destination,ID3D11Resource* src,uintptr_t caller,bool entire){
 if(!destination || !src)return;
 for(auto& w:watched)if(w.destination.Get()==destination){
  const bool first=!w.source;w.source=src;w.tick=GetTickCount64();w.caller=caller>=gameBase && caller<gameBase+0x181e000?caller-gameBase:0;
  if(first || trace){
   ComPtr<ID3D11Texture2D> input,target;src->QueryInterface(IID_PPV_ARGS(&input));destination->QueryInterface(IID_PPV_ARGS(&target));
   D3D11_TEXTURE2D_DESC a{},b{};if(input)input->GetDesc(&a);if(target)target->GetDesc(&b);
   log("native-copy frame="+std::to_string(frame)+" eye="+std::to_string(eye)+" type="+std::to_string(w.type)+" caller="+std::to_string(w.caller)+" entire="+std::to_string(entire)+" source="+std::to_string(reinterpret_cast<uintptr_t>(src))+" srcSize="+std::to_string(a.Width)+"x"+std::to_string(a.Height)+" srcFormat="+std::to_string(a.Format)+" srcMips="+std::to_string(a.MipLevels)+" dstFormat="+std::to_string(b.Format)+" dstMips="+std::to_string(b.MipLevels));
  }
 }
}
void EndEye(ID3D11DeviceContext* c,int which){
 if(!active || !c || which<0 || which>1)return;
 for(Type type:{Color,Depth}){
  if(history[type][which].frame==frame)continue;
  auto* w=source(type);if(!w || GetTickCount64()-w->tick>250)continue;
  ComPtr<ID3D11Texture2D> texture;w->source.As(&texture);
  bool stored=store(c,type,which,texture.Get(),type==Color?nativeColor.Get():nativeDepth.Get());
  if(trace && !stored)log("history-source-unavailable frame="+std::to_string(frame)+" eye="+std::to_string(which)+" type="+std::to_string(type));
 }
 eye=-1;
}
void End(){
 if(trace)for(int type=0;type<Count;++type)log("substitutions type="+std::to_string(type)+" left="+std::to_string(substitutions[type][0])+" right="+std::to_string(substitutions[type][1]));
 active=false;trace=false;eye=-1;
}
}
