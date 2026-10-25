#include "AllShaderHooks.h"
#include "GpuCapture.h"
#include "ShaderHunter.h"
#include "DecalPass.h"
#include "DlssPass.h"
#include <MinHook.h>
#include <wrl/client.h>
#include <intrin.h>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <fstream>
using Microsoft::WRL::ComPtr;
namespace AllShaderHooks {
namespace {
std::filesystem::path directory;
std::shared_mutex hookMutex;
std::unordered_map<void*,void*> originals;
template<class Fn> Fn original(void* object,UINT slot){
 auto* target=(*reinterpret_cast<void***>(object))[slot];std::shared_lock lock(hookMutex);
 return reinterpret_cast<Fn>(originals.at(target));
}
bool add(void* address,void* replacement,bool enable,const char* label){
 std::unique_lock lock(hookMutex);if(originals.contains(address))return true;
 void* trampoline{};auto result=MH_CreateHook(address,replacement,&trampoline);
 if(result==MH_ERROR_ALREADY_CREATED)return true; // Core immediate hook already covers this implementation.
 std::ofstream log(directory/L"shader-hunter-hooks.log",std::ios::app);log<<label<<" target="<<address<<" status="<<MH_StatusToString(result)<<'\n';
 if(result!=MH_OK)return false;originals[address]=trampoline;
 return !enable || MH_EnableHook(address)==MH_OK;
}
using CreateGeometry=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11GeometryShader**);
using CreateHull=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11HullShader**);
using CreateDomain=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11DomainShader**);
using CreateSO=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,const D3D11_SO_DECLARATION_ENTRY*,UINT,const UINT*,UINT,UINT,ID3D11ClassLinkage*,ID3D11GeometryShader**);
HRESULT STDMETHODCALLTYPE geometry(ID3D11Device* d,const void* p,SIZE_T n,ID3D11ClassLinkage* l,ID3D11GeometryShader** s){auto hr=original<CreateGeometry>(d,13)(d,p,n,l,s);if(SUCCEEDED(hr)&&s&&!DlssPass::Busy())GpuCapture::RememberShader(*s,p,n,3);return hr;}
HRESULT STDMETHODCALLTYPE geometrySO(ID3D11Device* d,const void* p,SIZE_T n,const D3D11_SO_DECLARATION_ENTRY* entries,UINT count,const UINT* strides,UINT strideCount,UINT stream,ID3D11ClassLinkage* l,ID3D11GeometryShader** s){auto hr=original<CreateSO>(d,14)(d,p,n,entries,count,strides,strideCount,stream,l,s);if(SUCCEEDED(hr)&&s&&!DlssPass::Busy())GpuCapture::RememberShader(*s,p,n,3);return hr;}
HRESULT STDMETHODCALLTYPE hull(ID3D11Device* d,const void* p,SIZE_T n,ID3D11ClassLinkage* l,ID3D11HullShader** s){auto hr=original<CreateHull>(d,16)(d,p,n,l,s);if(SUCCEEDED(hr)&&s&&!DlssPass::Busy())GpuCapture::RememberShader(*s,p,n,4);return hr;}
HRESULT STDMETHODCALLTYPE domain(ID3D11Device* d,const void* p,SIZE_T n,ID3D11ClassLinkage* l,ID3D11DomainShader** s){auto hr=original<CreateDomain>(d,17)(d,p,n,l,s);if(SUCCEEDED(hr)&&s&&!DlssPass::Busy())GpuCapture::RememberShader(*s,p,n,5);return hr;}
using Draw=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
using Indexed=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using Instanced=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
using IndexedInstanced=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
using Auto=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
using Indirect=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Buffer*,UINT);
using Dispatch=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT);
bool drawAllowed(ID3D11DeviceContext* c,UINT count,const char* name,uintptr_t caller){if(DlssPass::Busy())return true;bool allowed=ShaderHunter::Draw(c,count,name)&&DecalPass::BeforeDraw(c);GpuCapture::RecordCommand(c,allowed?name:"skipped-extended-draw",count,caller);return allowed;}
bool dispatchAllowed(ID3D11DeviceContext* c,const char* name,uintptr_t caller){if(DlssPass::Busy())return true;bool allowed=ShaderHunter::Dispatch(c);GpuCapture::RecordCommand(c,allowed?name:"skipped-extended-dispatch",0,caller);return allowed;}
void STDMETHODCALLTYPE draw(ID3D11DeviceContext* c,UINT n,UINT s){if(drawAllowed(c,n,"deferred-draw",reinterpret_cast<uintptr_t>(_ReturnAddress())))original<Draw>(c,13)(c,n,s);}
void STDMETHODCALLTYPE indexed(ID3D11DeviceContext* c,UINT n,UINT s,INT b){if(drawAllowed(c,n,"deferred-indexed",reinterpret_cast<uintptr_t>(_ReturnAddress())))original<Indexed>(c,12)(c,n,s,b);}
void STDMETHODCALLTYPE instanced(ID3D11DeviceContext* c,UINT n,UINT i,UINT s,UINT first){if(drawAllowed(c,n,"deferred-instanced",reinterpret_cast<uintptr_t>(_ReturnAddress())))original<Instanced>(c,21)(c,n,i,s,first);}
void STDMETHODCALLTYPE indexedInstanced(ID3D11DeviceContext* c,UINT n,UINT i,UINT s,INT b,UINT first){if(drawAllowed(c,n,"deferred-indexed-instanced",reinterpret_cast<uintptr_t>(_ReturnAddress())))original<IndexedInstanced>(c,20)(c,n,i,s,b,first);}
void STDMETHODCALLTYPE drawAuto(ID3D11DeviceContext* c){if(drawAllowed(c,0,"draw-auto",reinterpret_cast<uintptr_t>(_ReturnAddress())))original<Auto>(c,38)(c);}
void STDMETHODCALLTYPE indexedIndirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT offset){if(drawAllowed(c,0,"indexed-indirect",reinterpret_cast<uintptr_t>(_ReturnAddress())))original<Indirect>(c,39)(c,b,offset);}
void STDMETHODCALLTYPE drawIndirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT offset){if(drawAllowed(c,0,"draw-indirect",reinterpret_cast<uintptr_t>(_ReturnAddress())))original<Indirect>(c,40)(c,b,offset);}
void STDMETHODCALLTYPE dispatch(ID3D11DeviceContext* c,UINT x,UINT y,UINT z){if(dispatchAllowed(c,"deferred-dispatch",reinterpret_cast<uintptr_t>(_ReturnAddress())))original<Dispatch>(c,41)(c,x,y,z);}
void STDMETHODCALLTYPE dispatchIndirect(ID3D11DeviceContext* c,ID3D11Buffer* b,UINT offset){if(dispatchAllowed(c,"dispatch-indirect",reinterpret_cast<uintptr_t>(_ReturnAddress())))original<Indirect>(c,42)(c,b,offset);}
bool context(ID3D11DeviceContext* c,bool enable){
 auto** v=*reinterpret_cast<void***>(c);bool ok=true;
 struct Hook{UINT slot;void* replacement;const char* name;};
 const Hook hooks[]={{12,reinterpret_cast<void*>(indexed),"DrawIndexed"},{13,reinterpret_cast<void*>(draw),"Draw"},{20,reinterpret_cast<void*>(indexedInstanced),"DrawIndexedInstanced"},{21,reinterpret_cast<void*>(instanced),"DrawInstanced"},{38,reinterpret_cast<void*>(drawAuto),"DrawAuto"},{39,reinterpret_cast<void*>(indexedIndirect),"DrawIndexedInstancedIndirect"},{40,reinterpret_cast<void*>(drawIndirect),"DrawInstancedIndirect"},{41,reinterpret_cast<void*>(dispatch),"Dispatch"},{42,reinterpret_cast<void*>(dispatchIndirect),"DispatchIndirect"}};
 for(auto h:hooks)if(!add(v[h.slot],h.replacement,enable,h.name))ok=false;
 return ok;
}
using Deferred=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,UINT,ID3D11DeviceContext**);
HRESULT STDMETHODCALLTYPE deferred(ID3D11Device* d,UINT flags,ID3D11DeviceContext** c){auto hr=original<Deferred>(d,27)(d,flags,c);if(SUCCEEDED(hr)&&c&&*c)context(*c,true);return hr;}
}
bool Install(ID3D11Device* d,ID3D11DeviceContext* c,const std::filesystem::path& dir){
 directory=dir;auto** v=*reinterpret_cast<void***>(d);bool ok=true;
 if(!add(v[13],reinterpret_cast<void*>(geometry),false,"CreateGeometryShader"))ok=false;
 if(!add(v[14],reinterpret_cast<void*>(geometrySO),false,"CreateGeometryShaderWithStreamOutput"))ok=false;
 if(!add(v[16],reinterpret_cast<void*>(hull),false,"CreateHullShader"))ok=false;
 if(!add(v[17],reinterpret_cast<void*>(domain),false,"CreateDomainShader"))ok=false;
 if(!context(c,false))ok=false;
 ComPtr<ID3D11DeviceContext> worker;auto hr=d->CreateDeferredContext(0,&worker);
 if(SUCCEEDED(hr)){if(!context(worker.Get(),false))ok=false;}else {std::ofstream log(directory/L"shader-hunter-hooks.log",std::ios::app);log<<"CreateDeferredContext failed HRESULT="<<hr<<'\n';ok=false;}
 if(!add(v[27],reinterpret_cast<void*>(deferred),false,"CreateDeferredContext"))ok=false;
 return ok;
}
}
