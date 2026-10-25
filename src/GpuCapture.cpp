#include "GpuCapture.h"
#include "EffectHistory.h"
#include "ScreenUI.h"
#include "AimReticle.h"
#include "LightingTrial.h"
#include "ShaderHunter.h"
#include "AllShaderHooks.h"
#include "LightList.h"
#include "ProjectedLightBounds.h"
#include "DecalPass.h"
#include "DlssPass.h"
#include "FirstPersonHead.h"
#include "MeshCapture.h"
#include <MinHook.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <intrin.h>
#include <atomic>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

using Microsoft::WRL::ComPtr;
namespace GpuCapture {
namespace {
const GUID ShaderCode={0x85ce7341,0xf6c7,0x4e8a,{0x90,0x13,0x24,0x70,0x1a,0x55,0x40,0x91}};
std::filesystem::path output;
uintptr_t gameBase{};
// 0 idle, 1 recording, 2 GPU pending. Native interval and Present are serial
// graphics tasks; the release/acquire transition owns this diagnostic data.
std::atomic<int> state{};
thread_local int currentEye=-1;
thread_local bool recording{};
uint64_t frame{},pose{},copiedBytes{},draws{},discarded{};
ComPtr<ID3D11DeviceContext> submit;
ComPtr<ID3D11Query> finished;
struct Buffer {std::string stage;UINT slot{},width{};uintptr_t source{};ComPtr<ID3D11Buffer> copy;};
struct Shader {uint64_t id{};std::vector<uint8_t> code;};
struct Sample {
 int eye{};std::string kind;uint64_t draw{};uintptr_t caller{};
 uintptr_t vs{},ps{},cs{},gs{},hs{},ds{},target{},depth{};
 UINT width{},height{},format{},vertices{},vertexStart{},vertexStride{},vertexOffset{};D3D11_VIEWPORT viewport{};
 std::string resources,outputs;Shader shaders[6];std::vector<Buffer> buffers;MeshCapture::Data mesh;
};
struct CameraData {int eye{};uintptr_t root{};std::vector<uint8_t> view,projections;unsigned count{};};
std::vector<Sample> samples;
thread_local UINT drawVertexStart{};
struct DrawData {bool indexed{};UINT start{},instances=1,firstInstance{};INT base{};};
thread_local DrawData drawData;
std::vector<CameraData> cameras;
using DrawFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
using IndexedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using DrawInstancedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
using IndexedInstancedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
using DispatchFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT);
DrawFn originalDraw{};IndexedFn originalIndexed{};
DrawInstancedFn originalInstanced{};IndexedInstancedFn originalIndexedInstanced{};DispatchFn originalDispatch{};
using VertexFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11VertexShader**);
using PixelFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
using ComputeFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11ComputeShader**);
VertexFn originalVertex{};PixelFn originalPixel{};ComputeFn originalCompute{};
using LayoutFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_INPUT_ELEMENT_DESC*,UINT,const void*,SIZE_T,ID3D11InputLayout**);
LayoutFn originalLayout{};
HRESULT STDMETHODCALLTYPE layoutHook(ID3D11Device* d,const D3D11_INPUT_ELEMENT_DESC* entries,UINT count,const void* code,SIZE_T bytes,ID3D11InputLayout** layout){auto hr=originalLayout(d,entries,count,code,bytes,layout);if(SUCCEEDED(hr)&&layout)MeshCapture::RememberLayout(*layout,entries,count);return hr;}
using SetPixelFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11PixelShader*,ID3D11ClassInstance*const*,UINT);
using SetComputeFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11ComputeShader*,ID3D11ClassInstance*const*,UINT);
using CopyFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*);
using RegionFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);
SetPixelFn originalSetPixel{};SetComputeFn originalSetCompute{};CopyFn originalCopy{};RegionFn originalRegion{};
using SetVertexFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11VertexShader*,ID3D11ClassInstance*const*,UINT);
SetVertexFn originalSetVertex{};
void STDMETHODCALLTYPE setVertexHook(ID3D11DeviceContext* c,ID3D11VertexShader* s,ID3D11ClassInstance*const* instances,UINT n){originalSetVertex(c,s,instances,n);if(DlssPass::Busy())return;ScreenUI::Vertex(s);AimReticle::Vertex(s);ShaderHunter::Vertex(s);}
void STDMETHODCALLTYPE setPixelHook(ID3D11DeviceContext* c,ID3D11PixelShader* s,ID3D11ClassInstance*const* instances,UINT n){originalSetPixel(c,s,instances,n);if(DlssPass::Busy())return;EffectHistory::Pixel(s);ScreenUI::Pixel(s);AimReticle::Pixel(s);LightingTrial::Pixel(s);ShaderHunter::Pixel(s);DecalPass::Pixel(s);FirstPersonHead::Pixel(s);}
void STDMETHODCALLTYPE setComputeHook(ID3D11DeviceContext* c,ID3D11ComputeShader* s,ID3D11ClassInstance*const* instances,UINT n){originalSetCompute(c,s,instances,n);if(DlssPass::Busy())return;EffectHistory::Compute(s);ShaderHunter::Compute(s);LightList::Compute(s);}
void STDMETHODCALLTYPE copyHook(ID3D11DeviceContext* c,ID3D11Resource* dst,ID3D11Resource* src){originalCopy(c,dst,src);if(DlssPass::Busy())return;EffectHistory::Copied(dst,src,reinterpret_cast<uintptr_t>(_ReturnAddress()),true);}
void STDMETHODCALLTYPE regionHook(ID3D11DeviceContext* c,ID3D11Resource* dst,UINT sub,UINT x,UINT y,UINT z,ID3D11Resource* src,UINT input,const D3D11_BOX* box){originalRegion(c,dst,sub,x,y,z,src,input,box);if(DlssPass::Busy())return;EffectHistory::Copied(dst,src,reinterpret_cast<uintptr_t>(_ReturnAddress()),false);}
HRESULT STDMETHODCALLTYPE vertexHook(ID3D11Device* d,const void* p,SIZE_T n,ID3D11ClassLinkage* l,ID3D11VertexShader** s){auto h=originalVertex(d,p,n,l,s);if(SUCCEEDED(h)&&s&&!DlssPass::Busy())RememberShader(*s,p,n,1);return h;}
HRESULT STDMETHODCALLTYPE pixelHook(ID3D11Device* d,const void* p,SIZE_T n,ID3D11ClassLinkage* l,ID3D11PixelShader** s){auto h=originalPixel(d,p,n,l,s);if(SUCCEEDED(h)&&s&&!DlssPass::Busy())RememberShader(*s,p,n,0);return h;}
HRESULT STDMETHODCALLTYPE computeHook(ID3D11Device* d,const void* p,SIZE_T n,ID3D11ClassLinkage* l,ID3D11ComputeShader** s){auto h=originalCompute(d,p,n,l,s);if(SUCCEEDED(h)&&s&&!DlssPass::Busy())RememberShader(*s,p,n,2);return h;}
Shader readShader(ID3D11DeviceChild* shader){
 Shader result{};if(!shader)return result;UINT size{};shader->GetPrivateData(ShaderCode,&size,nullptr);
 if(!size || size>1024*1024)return result;result.code.resize(size);
 if(FAILED(shader->GetPrivateData(ShaderCode,&size,result.code.data()))){result.code.clear();return result;}
 uint64_t hash=14695981039346656037ull;for(auto b:result.code){hash^=b;hash*=1099511628211ull;}result.id=hash;return result;
}
void copyBuffers(ID3D11DeviceContext* c,ID3D11Device* d,Sample& s,const char* stage){
 ID3D11Buffer* bound[14]{};
 if(!strcmp(stage,"vs"))c->VSGetConstantBuffers(0,14,bound);
 else if(!strcmp(stage,"ps"))c->PSGetConstantBuffers(0,14,bound);
 else if(!strcmp(stage,"gs"))c->GSGetConstantBuffers(0,14,bound);
 else if(!strcmp(stage,"hs"))c->HSGetConstantBuffers(0,14,bound);
 else if(!strcmp(stage,"ds"))c->DSGetConstantBuffers(0,14,bound);
 else c->CSGetConstantBuffers(0,14,bound);
 for(UINT i=0;i<14;++i){
  ComPtr<ID3D11Buffer> source;source.Attach(bound[i]);if(!source)continue;
  D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);
  if(!desc.ByteWidth || desc.ByteWidth>65536 || copiedBytes+desc.ByteWidth>64*1024*1024){++discarded;continue;}
  Buffer b;b.stage=stage;b.slot=i;b.width=desc.ByteWidth;b.source=reinterpret_cast<uintptr_t>(source.Get());
  desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;
  if(FAILED(d->CreateBuffer(&desc,nullptr,&b.copy))){++discarded;continue;}
  c->CopyResource(b.copy.Get(),source.Get());copiedBytes+=b.width;s.buffers.push_back(std::move(b));
 }
}
std::string resources(ID3D11DeviceContext* c,bool compute){
 ID3D11ShaderResourceView* bound[16]{};if(compute)c->CSGetShaderResources(0,16,bound);else c->PSGetShaderResources(0,16,bound);
 std::ostringstream out;out<<'[';bool first=true;
 for(unsigned i=0;i<16;++i){ComPtr<ID3D11ShaderResourceView> srv;srv.Attach(bound[i]);if(!srv)continue;
  ComPtr<ID3D11Resource> resource;srv->GetResource(&resource);D3D11_SHADER_RESOURCE_VIEW_DESC vd{};srv->GetDesc(&vd);
  ComPtr<ID3D11Texture2D> t;resource.As(&t);D3D11_TEXTURE2D_DESC td{};if(t)t->GetDesc(&td);
  if(!first)out<<',';first=false;out<<"{\"slot\":"<<i<<",\"resource\":"<<reinterpret_cast<uintptr_t>(resource.Get())<<",\"viewFormat\":"<<vd.Format<<",\"width\":"<<td.Width<<",\"height\":"<<td.Height<<",\"array\":"<<td.ArraySize<<'}';
 }out<<']';return out.str();
}
std::string outputBindings(ID3D11DeviceContext* c,bool compute){
 std::ostringstream out;out<<'[';bool first=true;
 auto emit=[&](const char* role,UINT slot,ID3D11View* view,UINT format,UINT dimension,UINT mip,UINT slice,UINT layers){
  if(!view)return;ComPtr<ID3D11Resource> r;view->GetResource(&r);ComPtr<ID3D11Texture2D> t;r.As(&t);D3D11_TEXTURE2D_DESC td{};if(t)t->GetDesc(&td);
  if(!first)out<<',';first=false;out<<"{\"role\":\""<<role<<"\",\"slot\":"<<slot<<",\"resource\":"<<reinterpret_cast<uintptr_t>(r.Get())<<",\"viewFormat\":"<<format<<",\"viewDimension\":"<<dimension<<",\"mip\":"<<mip<<",\"slice\":"<<slice<<",\"layers\":"<<layers<<",\"width\":"<<td.Width<<",\"height\":"<<td.Height<<",\"array\":"<<td.ArraySize<<",\"textureFormat\":"<<td.Format<<",\"samples\":"<<td.SampleDesc.Count<<",\"bindFlags\":"<<td.BindFlags<<'}';
 };
 if(!compute){
  ID3D11RenderTargetView* raw[8]{};ComPtr<ID3D11DepthStencilView> ds;c->OMGetRenderTargets(8,raw,&ds);
  for(UINT i=0;i<8;++i){ComPtr<ID3D11RenderTargetView> v;v.Attach(raw[i]);if(!v)continue;D3D11_RENDER_TARGET_VIEW_DESC d{};v->GetDesc(&d);UINT mip{},slice{},layers=1;if(d.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D)mip=d.Texture2D.MipSlice;if(d.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DARRAY){mip=d.Texture2DArray.MipSlice;slice=d.Texture2DArray.FirstArraySlice;layers=d.Texture2DArray.ArraySize;}emit("rtv",i,v.Get(),d.Format,d.ViewDimension,mip,slice,layers);}
  if(ds){D3D11_DEPTH_STENCIL_VIEW_DESC d{};ds->GetDesc(&d);UINT mip{},slice{},layers=1;if(d.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D)mip=d.Texture2D.MipSlice;if(d.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DARRAY){mip=d.Texture2DArray.MipSlice;slice=d.Texture2DArray.FirstArraySlice;layers=d.Texture2DArray.ArraySize;}emit("dsv",0,ds.Get(),d.Format,d.ViewDimension,mip,slice,layers);}
 }
 ID3D11UnorderedAccessView* raw[8]{};if(compute)c->CSGetUnorderedAccessViews(0,8,raw);else c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,8,raw);
 for(UINT i=0;i<8;++i){ComPtr<ID3D11UnorderedAccessView> v;v.Attach(raw[i]);if(!v)continue;D3D11_UNORDERED_ACCESS_VIEW_DESC d{};v->GetDesc(&d);UINT mip{},slice{},layers=1;if(d.ViewDimension==D3D11_UAV_DIMENSION_TEXTURE2D)mip=d.Texture2D.MipSlice;if(d.ViewDimension==D3D11_UAV_DIMENSION_TEXTURE2DARRAY){mip=d.Texture2DArray.MipSlice;slice=d.Texture2DArray.FirstArraySlice;layers=d.Texture2DArray.ArraySize;}emit(compute?"cs-uav":"om-uav",i,v.Get(),d.Format,d.ViewDimension,mip,slice,layers);}
 out<<']';return out.str();
}
void snapshot(ID3D11DeviceContext* c,const char* kind,UINT vertices,uintptr_t caller){
 if(!recording || state.load(std::memory_order_relaxed)!=1 || currentEye<0 || currentEye>1)return;
 ++draws;unsigned eyeSamples=0;for(auto& old:samples)if(old.eye==currentEye)++eyeSamples;
 if(eyeSamples>=192){++discarded;return;}
 ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11ComputeShader> cs;ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
 const bool compute=strstr(kind,"dispatch")!=nullptr;if(compute)c->CSGetShader(&cs,nullptr,nullptr);else{c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);c->GSGetShader(&gs,nullptr,nullptr);c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&ds,nullptr,nullptr);}
 ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11DepthStencilView> depth;ComPtr<ID3D11Resource> targetResource,depthResource;
 if(!compute){c->OMGetRenderTargets(1,&target,&depth);if(target)target->GetResource(&targetResource);if(depth)depth->GetResource(&depthResource);}
 D3D11_VIEWPORT viewport{};UINT count=1;c->RSGetViewports(&count,&viewport);
 const auto v=reinterpret_cast<uintptr_t>(vs.Get()),p=reinterpret_cast<uintptr_t>(ps.Get()),k=reinterpret_cast<uintptr_t>(cs.Get()),t=reinterpret_cast<uintptr_t>(targetResource.Get()),z=reinterpret_cast<uintptr_t>(depthResource.Get());
 const auto g=reinterpret_cast<uintptr_t>(gs.Get()),h=reinterpret_cast<uintptr_t>(hs.Get()),a=reinterpret_cast<uintptr_t>(ds.Get());
 const bool skinMesh=!compute && FirstPersonHead::SharedSkin(ps.Get());unsigned skinSamples=0;
 for(auto& old:samples){if(old.eye==currentEye && !old.mesh.buffers.empty())++skinSamples;if(!skinMesh && old.eye==currentEye && old.vs==v && old.ps==p && old.cs==k && old.gs==g && old.hs==h && old.ds==a && old.target==t && old.depth==z && old.viewport.Width==viewport.Width && old.viewport.Height==viewport.Height && old.kind==kind)return;}
 if(skinMesh && skinSamples>=24){++discarded;return;}
 Sample s;s.eye=currentEye;s.kind=kind;s.draw=draws;s.caller=(caller>=gameBase && caller<gameBase+0x181e000)?caller-gameBase:0;
 s.vs=v;s.ps=p;s.cs=k;s.gs=g;s.hs=h;s.ds=a;s.target=t;s.depth=z;s.vertices=vertices;s.viewport=viewport;
 ComPtr<ID3D11Texture2D> texture;if(targetResource)targetResource.As(&texture);else if(depthResource)depthResource.As(&texture);
 if(texture){D3D11_TEXTURE2D_DESC td{};texture->GetDesc(&td);s.width=td.Width;s.height=td.Height;s.format=td.Format;}
 s.shaders[0]=readShader(vs.Get());s.shaders[1]=readShader(ps.Get());s.shaders[2]=readShader(cs.Get());s.shaders[3]=readShader(gs.Get());s.shaders[4]=readShader(hs.Get());s.shaders[5]=readShader(ds.Get());s.resources=resources(c,compute);s.outputs=outputBindings(c,compute);
 ComPtr<ID3D11Device> d;c->GetDevice(&d);if(compute)copyBuffers(c,d.Get(),s,"cs");else{copyBuffers(c,d.Get(),s,"vs");copyBuffers(c,d.Get(),s,"ps");if(gs)copyBuffers(c,d.Get(),s,"gs");if(hs)copyBuffers(c,d.Get(),s,"hs");if(ds)copyBuffers(c,d.Get(),s,"ds");}
 if(skinMesh)MeshCapture::Capture(c,drawData.indexed,drawData.start,drawData.base,drawData.instances,drawData.firstInstance,copiedBytes,s.mesh);
 if(s.shaders[1].id==0x089fdfd845a6940full && s.kind=="draw" && vertices==4){
  ComPtr<ID3D11Buffer> vertex;c->IAGetVertexBuffers(0,1,&vertex,&s.vertexStride,&s.vertexOffset);s.vertexStart=drawVertexStart;
  D3D11_BUFFER_DESC bd{};if(vertex)vertex->GetDesc(&bd);
  const uint64_t first=uint64_t(s.vertexOffset)+uint64_t(s.vertexStart)*s.vertexStride,bytes=uint64_t(vertices)*s.vertexStride;
  if(vertex && bytes && bytes<=4096 && first+bytes<=bd.ByteWidth && first%4==0 && bytes%4==0){
   Buffer b;b.stage="ia";b.width=UINT(bytes);b.source=reinterpret_cast<uintptr_t>(vertex.Get());
   bd={};bd.ByteWidth=b.width;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
   if(SUCCEEDED(d->CreateBuffer(&bd,nullptr,&b.copy))){D3D11_BOX box{UINT(first),0,0,UINT(first+bytes),1,1};c->CopySubresourceRegion(b.copy.Get(),0,0,0,0,vertex.Get(),0,&box);copiedBytes+=bytes;s.buffers.push_back(std::move(b));}
  }
 }
 samples.push_back(std::move(s));
}
void safeSnapshot(ID3D11DeviceContext* c,const char* kind,UINT n,uintptr_t caller){try{snapshot(c,kind,n,caller);}catch(...){recording=false;++discarded;}}
void STDMETHODCALLTYPE drawHook(ID3D11DeviceContext* c,UINT n,UINT start){if(DlssPass::Busy()){originalDraw(c,n,start);return;}EffectHistory::BeforeDraw(c);const bool allowed=ShaderHunter::Draw(c,n,"draw");const bool draw=LightingTrial::BeforeDraw(c)&&allowed&&DecalPass::BeforeDraw(c);DlssPass::BeforeDraw(c);ScreenUI::BeforeDraw(c);if(draw){ProjectedLightBounds::BeforeDraw(c);AimReticle::BeforeDraw(c,n);}drawVertexStart=start;if(draw)FirstPersonHead::BeforeDraw(c,n,start,0,false);drawData={false,start,1,0,0};if(recording)safeSnapshot(c,draw?"draw":"skipped-draw",n,reinterpret_cast<uintptr_t>(_ReturnAddress()));if(draw)originalDraw(c,n,start);FirstPersonHead::AfterDraw(c);AimReticle::AfterDraw(c);ProjectedLightBounds::AfterDraw(c);ScreenUI::AfterDraw(c);DlssPass::AfterDraw(c);LightingTrial::AfterDraw(c);EffectHistory::AfterDraw(c);}
void STDMETHODCALLTYPE indexedHook(ID3D11DeviceContext* c,UINT n,UINT start,INT base){if(DlssPass::Busy()){originalIndexed(c,n,start,base);return;}EffectHistory::BeforeDraw(c);const bool allowed=ShaderHunter::Draw(c,n,"indexed");const bool draw=LightingTrial::BeforeDraw(c)&&allowed&&DecalPass::BeforeDraw(c);DlssPass::BeforeDraw(c);ScreenUI::BeforeDraw(c);if(draw)AimReticle::BeforeDraw(c,n,true);if(draw)FirstPersonHead::BeforeDraw(c,n,start,base,true);drawData={true,start,1,0,base};if(recording)safeSnapshot(c,draw?"indexed":"skipped-indexed",n,reinterpret_cast<uintptr_t>(_ReturnAddress()));if(draw)originalIndexed(c,n,start,base);FirstPersonHead::AfterDraw(c);AimReticle::AfterDraw(c);ScreenUI::AfterDraw(c);DlssPass::AfterDraw(c);LightingTrial::AfterDraw(c);EffectHistory::AfterDraw(c);}
void STDMETHODCALLTYPE instancedHook(ID3D11DeviceContext* c,UINT n,UINT instances,UINT start,UINT first){if(DlssPass::Busy()){originalInstanced(c,n,instances,start,first);return;}EffectHistory::BeforeDraw(c);const bool allowed=ShaderHunter::Draw(c,n,"instanced");const bool draw=LightingTrial::BeforeDraw(c)&&allowed&&DecalPass::BeforeDraw(c);DlssPass::BeforeDraw(c);ScreenUI::BeforeDraw(c);if(draw)FirstPersonHead::BeforeDraw(c,n,start,0,false);drawData={false,start,instances,first,0};if(recording)safeSnapshot(c,draw?"instanced":"skipped-instanced",n,reinterpret_cast<uintptr_t>(_ReturnAddress()));if(draw)originalInstanced(c,n,instances,start,first);FirstPersonHead::AfterDraw(c);ScreenUI::AfterDraw(c);DlssPass::AfterDraw(c);LightingTrial::AfterDraw(c);EffectHistory::AfterDraw(c);}
void STDMETHODCALLTYPE indexedInstancedHook(ID3D11DeviceContext* c,UINT n,UINT instances,UINT start,INT base,UINT first){if(DlssPass::Busy()){originalIndexedInstanced(c,n,instances,start,base,first);return;}EffectHistory::BeforeDraw(c);const bool allowed=ShaderHunter::Draw(c,n,"indexedInstanced");const bool draw=LightingTrial::BeforeDraw(c)&&allowed&&DecalPass::BeforeDraw(c);DlssPass::BeforeDraw(c);ScreenUI::BeforeDraw(c);if(draw)FirstPersonHead::BeforeDraw(c,n,start,base,true);drawData={true,start,instances,first,base};if(recording)safeSnapshot(c,draw?"indexedInstanced":"skipped-indexedInstanced",n,reinterpret_cast<uintptr_t>(_ReturnAddress()));if(draw)originalIndexedInstanced(c,n,instances,start,base,first);FirstPersonHead::AfterDraw(c);ScreenUI::AfterDraw(c);DlssPass::AfterDraw(c);LightingTrial::AfterDraw(c);EffectHistory::AfterDraw(c);}
void STDMETHODCALLTYPE dispatchHook(ID3D11DeviceContext* c,UINT x,UINT y,UINT z){if(DlssPass::Busy()){originalDispatch(c,x,y,z);return;}EffectHistory::BeforeDispatch(c);const bool draw=ShaderHunter::Dispatch(c);if(draw)LightList::BeforeDispatch(c,x,y,z);if(recording)safeSnapshot(c,draw?"dispatch":"skipped-dispatch",x,reinterpret_cast<uintptr_t>(_ReturnAddress()));if(draw)originalDispatch(c,x,y,z);LightList::AfterDispatch(c);EffectHistory::AfterDispatch(c,draw);}
void binary(const std::filesystem::path& p,const void* data,size_t size){std::ofstream f(p,std::ios::binary);f.write(static_cast<const char*>(data),size);}
std::string shaderName(uint64_t hash){std::ostringstream s;s<<"shader-"<<std::hex<<hash;return s.str();}
}
void RememberShader(ID3D11DeviceChild* shader,const void* code,SIZE_T size,UINT stage){
 if(!shader || !code || !size)return;
 ShaderHunter::RegisterShader(shader,code,size,stage);
 if(size<=1024*1024){shader->SetPrivateData(ShaderCode,UINT(size),code);if(stage<3){EffectHistory::RegisterShader(shader,code,size);ScreenUI::RegisterShader(shader,code,size);AimReticle::RegisterShader(shader,code,size);LightingTrial::RegisterShader(shader,code,size);ProjectedLightBounds::RegisterShader(shader,code,size);}if(stage==2)LightList::RegisterShader(shader,code,size);if(stage==0){DecalPass::RegisterShader(shader,code,size);FirstPersonHead::RegisterShader(shader,code,size);}}
}
void RecordCommand(ID3D11DeviceContext* c,const char* kind,UINT n,uintptr_t caller){if(recording)safeSnapshot(c,kind,n,caller);}
bool Install(ID3D11Device* d,ID3D11DeviceContext* c,const std::filesystem::path& dir,uintptr_t base){
 output=dir/L"gpu-captures";gameBase=base;
 EffectHistory::Initialize(dir,base);
 ScreenUI::Initialize(dir);
 AimReticle::Initialize(dir);
 LightingTrial::Initialize(dir);
 ShaderHunter::Initialize(dir);
 LightList::Initialize(dir);
 ProjectedLightBounds::Initialize(dir);
 DecalPass::Initialize(dir);
 DlssPass::Initialize(dir,base);
 auto** dv=*reinterpret_cast<void***>(d);auto** cv=*reinterpret_cast<void***>(c);
 const auto hook=[](void* address,void* replacement,void** original){return MH_CreateHook(address,replacement,original)==MH_OK;};
 return hook(dv[11],reinterpret_cast<void*>(layoutHook),reinterpret_cast<void**>(&originalLayout)) && hook(dv[12],reinterpret_cast<void*>(vertexHook),reinterpret_cast<void**>(&originalVertex)) && hook(dv[15],reinterpret_cast<void*>(pixelHook),reinterpret_cast<void**>(&originalPixel)) && hook(dv[18],reinterpret_cast<void*>(computeHook),reinterpret_cast<void**>(&originalCompute)) &&
  hook(cv[12],reinterpret_cast<void*>(indexedHook),reinterpret_cast<void**>(&originalIndexed)) && hook(cv[13],reinterpret_cast<void*>(drawHook),reinterpret_cast<void**>(&originalDraw)) && hook(cv[20],reinterpret_cast<void*>(indexedInstancedHook),reinterpret_cast<void**>(&originalIndexedInstanced)) && hook(cv[21],reinterpret_cast<void*>(instancedHook),reinterpret_cast<void**>(&originalInstanced)) && hook(cv[41],reinterpret_cast<void*>(dispatchHook),reinterpret_cast<void**>(&originalDispatch)) &&
  hook(cv[11],reinterpret_cast<void*>(setVertexHook),reinterpret_cast<void**>(&originalSetVertex)) && hook(cv[9],reinterpret_cast<void*>(setPixelHook),reinterpret_cast<void**>(&originalSetPixel)) && hook(cv[69],reinterpret_cast<void*>(setComputeHook),reinterpret_cast<void**>(&originalSetCompute)) && hook(cv[47],reinterpret_cast<void*>(copyHook),reinterpret_cast<void**>(&originalCopy)) && hook(cv[46],reinterpret_cast<void*>(regionHook),reinterpret_cast<void**>(&originalRegion)) && AllShaderHooks::Install(d,c,dir);
}
void Begin(uint64_t id,uint64_t tracking){
 if(state.load(std::memory_order_acquire)!=0)return;
 samples.clear();cameras.clear();finished.Reset();submit.Reset();frame=id;pose=tracking;draws=copiedBytes=discarded=0;currentEye=-1;recording=true;state.store(1,std::memory_order_release);
}
void Eye(int eye){currentEye=eye;}
void Camera(int eye,const void* view,const void* projections[],unsigned count){
 if(!recording || state.load()!=1 || count>8)return;
 for(auto& old:cameras)if(old.eye==eye && old.root==0)return;
 CameraData d;d.eye=eye;d.view.resize(0x160);memcpy(d.view.data(),view,0x160);d.count=count;d.projections.resize(count*0x2b0);
 for(unsigned i=0;i<count;++i)memcpy(d.projections.data()+i*0x2b0,projections[i],0x2b0);cameras.push_back(std::move(d));
}
void Auxiliary(int eye,const void* root,const void* view,const void* projections[],unsigned count){
 if(!recording || state.load()!=1 || !root || !view || count>8 || cameras.size()>=34)return;
 for(auto& old:cameras)if(old.eye==eye && old.root==reinterpret_cast<uintptr_t>(root))return;
 CameraData d;d.eye=eye;d.root=reinterpret_cast<uintptr_t>(root);d.view.resize(0x160);memcpy(d.view.data(),view,0x160);d.count=count;d.projections.resize(count*0x2b0);
 for(unsigned i=0;i<count;++i)memcpy(d.projections.data()+i*0x2b0,projections[i],0x2b0);cameras.push_back(std::move(d));
}
void End(ID3D11DeviceContext* c){
 if(state.load()!=1)return;recording=false;currentEye=-1;
 if(!c){state=0;return;}submit=c;ComPtr<ID3D11Device> d;c->GetDevice(&d);D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
 if(FAILED(d->CreateQuery(&q,&finished))){state=0;return;}c->End(finished.Get());state.store(2,std::memory_order_release);
}
void Flush(){
 if(state.load(std::memory_order_acquire)!=2)return;BOOL ready{};auto hr=submit->GetData(finished.Get(),&ready,sizeof(ready),0);
 if(hr==S_FALSE || (SUCCEEDED(hr)&&!ready))return;
 if(FAILED(hr)){state=0;return;}
 try{
  auto dir=output/(L"frame-"+std::to_wstring(frame));std::error_code ec;std::filesystem::create_directories(dir,ec);if(ec){state=0;return;}
  for(auto& c:cameras){auto prefix="camera-eye-"+std::to_string(c.eye)+(c.root?"-root-"+std::to_string(c.root):"");binary(dir/(prefix+"-view.bin"),c.view.data(),c.view.size());binary(dir/(prefix+"-projections.bin"),c.projections.data(),c.projections.size());}
  std::ofstream json(dir/L"trace.json");json<<"{\"sourceFrame\":"<<frame<<",\"poseId\":"<<pose<<",\"draws\":"<<draws<<",\"copiedBytes\":"<<copiedBytes<<",\"discarded\":"<<discarded<<",\"samples\":[";
  for(size_t i=0;i<samples.size();++i){auto& s=samples[i];if(i)json<<',';
   json<<"{\"sample\":"<<i<<",\"eye\":"<<s.eye<<",\"kind\":\""<<s.kind<<"\",\"draw\":"<<s.draw<<",\"callerRva\":"<<s.caller<<",\"count\":"<<s.vertices<<",\"target\":"<<s.target<<",\"depth\":"<<s.depth<<",\"width\":"<<s.width<<",\"height\":"<<s.height<<",\"format\":"<<s.format<<",\"viewport\":["<<s.viewport.TopLeftX<<','<<s.viewport.TopLeftY<<','<<s.viewport.Width<<','<<s.viewport.Height<<"],\"shaders\":[";
   for(unsigned stage=0;stage<6;++stage){auto& shader=s.shaders[stage];if(stage)json<<',';json<<'"'<<shaderName(shader.id)<<'"';
    if(!shader.code.empty()){auto path=dir/(shaderName(shader.id)+".dxbc");if(!std::filesystem::exists(path)){binary(path,shader.code.data(),shader.code.size());ComPtr<ID3DBlob> assembly;if(SUCCEEDED(D3DDisassemble(shader.code.data(),shader.code.size(),0,nullptr,&assembly)))binary(dir/(shaderName(shader.id)+".asm"),assembly->GetBufferPointer(),assembly->GetBufferSize());}}
   }json<<"],\"vertexStart\":"<<s.vertexStart<<",\"vertexStride\":"<<s.vertexStride<<",\"vertexOffset\":"<<s.vertexOffset<<",\"resources\":"<<s.resources<<",\"outputs\":"<<s.outputs<<",\"buffers\":[";bool first=true;
   for(auto& b:s.buffers){D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(submit->Map(b.copy.Get(),0,D3D11_MAP_READ,0,&mapped)))continue;
    auto name="sample-"+std::to_string(i)+"-"+b.stage+"-cb"+std::to_string(b.slot)+".bin";binary(dir/name,mapped.pData,b.width);submit->Unmap(b.copy.Get(),0);
    if(!first)json<<',';first=false;json<<"{\"stage\":\""<<b.stage<<"\",\"slot\":"<<b.slot<<",\"bytes\":"<<b.width<<",\"source\":"<<b.source<<",\"file\":\""<<name<<"\"}";
   }json<<"],\"mesh\":"<<MeshCapture::Write(submit.Get(),dir,"sample-"+std::to_string(i),s.mesh)<<'}';
  }json<<"]}\n";
 }catch(...){}
 samples.clear();cameras.clear();finished.Reset();submit.Reset();state.store(0,std::memory_order_release);
}
}
