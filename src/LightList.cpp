#include "LightList.h"
#include "LightListShader.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <string>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace LightList {
namespace {
const GUID Tag={0x5f80d5db,0x2ca4,0x4dba,{0x9f,0x98,0x8a,0x4e,0x49,0x61,0x22,0x1c}};
std::filesystem::path directory;
thread_local bool role{},active{},trace{},changed{};
thread_local int eye=-1;
thread_local uint64_t frame{};
thread_local ComPtr<ID3D11ComputeShader> saved;
ComPtr<ID3D11ComputeShader> replacement[2];
UINT mode{};uint64_t settingsTick{},counts[2]{};
void log(const std::string& value){std::ofstream f(directory/L"light-list.log",std::ios::app);f<<value<<'\n';}
bool tagged(ID3D11DeviceChild* s){UINT value{},size=sizeof(value);return s && SUCCEEDED(s->GetPrivateData(Tag,&size,&value)) && value==1;}
bool create(ID3D11Device* d,UINT variant){
 if(replacement[variant])return true;ComPtr<ID3DBlob> code,error;D3D_SHADER_MACRO macros[]={{"NO_TILE_CULL","1"},{nullptr,nullptr}};
 auto hr=D3DCompile(LightListShaderCode,strlen(LightListShaderCode),"PlagueVR-LightList",variant?macros:nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
 if(FAILED(hr)){log("compile failed HRESULT="+std::to_string(hr)+(error?" "+std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):""));return false;}
 if(FAILED(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&replacement[variant])))return false;
 log("created variant="+std::to_string(variant));return true;
}
void settings(){auto now=GetTickCount64();if(now-settingsTick<200)return;settingsTick=now;const auto ini=(directory/L"PlagueTaleVR.ini").wstring();UINT next=GetPrivateProfileIntW(L"Diagnostics",L"LightListMode",0,ini.c_str());if(next>2)next=0;if(next!=mode)log("mode="+std::to_string(next));mode=next;}
}
void Initialize(const std::filesystem::path& path){directory=path;}
void RegisterShader(ID3D11DeviceChild* shader,const void* code,SIZE_T size){
 uint64_t hash=14695981039346656037ull;for(SIZE_T i=0;i<size;++i){hash^=static_cast<const uint8_t*>(code)[i];hash*=1099511628211ull;}
 if(shader && hash==0x7d6c9892c70a07a6ull){UINT value=1;shader->SetPrivateData(Tag,sizeof(value),&value);}
}
void Compute(ID3D11ComputeShader* shader){role=tagged(shader);}
void Begin(uint64_t id,bool tracked,bool capture){frame=id;active=tracked;trace=capture;eye=-1;settings();}
void Eye(int value){eye=value;}
void BeforeDispatch(ID3D11DeviceContext* c,UINT x,UINT y,UINT z){
 if(!mode || !active || !role || changed || eye<0 || eye>1 || !x || !y || z!=1 || c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 ComPtr<ID3D11ComputeShader> native;c->CSGetShader(&native,nullptr,nullptr);if(!tagged(native.Get()))return;
 ComPtr<ID3D11UnorderedAccessView> target;c->CSGetUnorderedAccessViews(0,1,&target);if(!target)return;
 D3D11_UNORDERED_ACCESS_VIEW_DESC view{};target->GetDesc(&view);if(view.ViewDimension!=D3D11_UAV_DIMENSION_BUFFER || uint64_t(view.Buffer.NumElements)<uint64_t(x)*y*256)return;
 ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Buffer> buffer;target->GetResource(&resource);resource.As(&buffer);if(!buffer)return;D3D11_BUFFER_DESC output{};buffer->GetDesc(&output);if(output.StructureByteStride!=4)return;
 ID3D11Buffer* bound[3]{};c->CSGetConstantBuffers(0,3,bound);const UINT minimum[]={1440,928,61456};bool valid=true;
 for(UINT i=0;i<3;++i){ComPtr<ID3D11Buffer> b;b.Attach(bound[i]);D3D11_BUFFER_DESC desc{};if(b)b->GetDesc(&desc);if(desc.ByteWidth<minimum[i])valid=false;}
 if(!valid)return;ComPtr<ID3D11Device> d;c->GetDevice(&d);if(!create(d.Get(),mode-1))return;
 saved=native;c->CSSetShader(replacement[mode-1].Get(),nullptr,0);changed=true;++counts[eye];
 if(trace)log("replace frame="+std::to_string(frame)+" eye="+std::to_string(eye)+" mode="+std::to_string(mode)+" groups="+std::to_string(x)+"x"+std::to_string(y)+" elements="+std::to_string(view.Buffer.NumElements));
}
void AfterDispatch(ID3D11DeviceContext* c){if(!changed)return;c->CSSetShader(saved.Get(),nullptr,0);saved.Reset();changed=false;}
void End(){if(trace)log("counts left="+std::to_string(counts[0])+" right="+std::to_string(counts[1]));active=false;eye=-1;}
}
