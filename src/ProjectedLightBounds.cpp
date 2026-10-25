#include "ProjectedLightBounds.h"
#include "ProjectedLightBoundsShader.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <string>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace ProjectedLightBounds {
namespace {
const GUID Tag={0x558a34b7,0x1f6b,0x4c6e,{0xa5,0x30,0x85,0x97,0x60,0xcc,0x0e,0x12}};
std::filesystem::path directory;
thread_local bool active{},trace{},changed{};
thread_local int eye=-1;
thread_local uint64_t frame{};
thread_local ComPtr<ID3D11VertexShader> savedVertex;
thread_local ComPtr<ID3D11Buffer> savedBuffers[2];
thread_local D3D11_RECT savedScissors[16]{};
thread_local UINT savedScissorCount{};
ComPtr<ID3D11VertexShader> replacement[2];UINT mode{};uint64_t tick{},counts[2]{};
void log(const std::string& value){std::ofstream f(directory/L"projected-light-bounds.log",std::ios::app);f<<value<<'\n';}
UINT role(ID3D11DeviceChild* shader){UINT r{},size=sizeof(r);if(shader && FAILED(shader->GetPrivateData(Tag,&size,&r)))return 0;return r;}
bool create(ID3D11Device* device,UINT variant){
 if(replacement[variant])return true;ComPtr<ID3DBlob> code,error;D3D_SHADER_MACRO macros[]={{"FULL_SCREEN","1"},{nullptr,nullptr}};
 auto hr=D3DCompile(ProjectedLightBoundsShaderCode,strlen(ProjectedLightBoundsShaderCode),"PlagueVR-ProjectedLightBounds",variant?macros:nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
 if(FAILED(hr)){log("compile failed HRESULT="+std::to_string(hr)+(error?" "+std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()):""));return false;}
 if(FAILED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&replacement[variant])))return false;
 log("created variant="+std::to_string(variant));return true;
}
void settings(){auto now=GetTickCount64();if(now-tick<200)return;tick=now;UINT next=GetPrivateProfileIntW(L"Diagnostics",L"ProjectedLightBoundsMode",0,(directory/L"PlagueTaleVR.ini").c_str());if(next>2)next=0;if(next!=mode)log("mode="+std::to_string(next));mode=next;}
}
void Initialize(const std::filesystem::path& path){directory=path;}
void RegisterShader(ID3D11DeviceChild* shader,const void* code,SIZE_T size){
 if(!shader || !code)return;uint64_t hash=14695981039346656037ull;for(SIZE_T i=0;i<size;++i){hash^=static_cast<const uint8_t*>(code)[i];hash*=1099511628211ull;}
 UINT r=hash==0x67a9d7afc61c4e91ull?1:hash==0xa84bb4fe7a8921f4ull?2:0;if(r)shader->SetPrivateData(Tag,sizeof(r),&r);
}
void Begin(uint64_t id,bool tracked,bool capture){frame=id;active=tracked;trace=capture;eye=-1;settings();}
void Eye(int value){eye=value;}
void BeforeDraw(ID3D11DeviceContext* c){
 if(!mode || !active || eye<0 || eye>1 || changed || c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 ComPtr<ID3D11PixelShader> ps;c->PSGetShader(&ps,nullptr,nullptr);if(role(ps.Get())!=2)return;
 ComPtr<ID3D11VertexShader> vs;c->VSGetShader(&vs,nullptr,nullptr);if(role(vs.Get())!=1)return;
 ComPtr<ID3D11Buffer> global,omni;c->PSGetConstantBuffers(0,1,&global);c->PSGetConstantBuffers(2,1,&omni);if(!global || !omni)return;
 D3D11_BUFFER_DESC g{},o{};global->GetDesc(&g);omni->GetDesc(&o);if(g.ByteWidth!=1440 || o.ByteWidth!=1120)return;
 D3D11_VIEWPORT viewport{};UINT n=1;c->RSGetViewports(&n,&viewport);if(n!=1 || viewport.Width<=0 || viewport.Height<=0)return;
 ComPtr<ID3D11Device> device;c->GetDevice(&device);if(!create(device.Get(),mode-1))return;
 savedVertex=vs;c->VSGetConstantBuffers(12,1,&savedBuffers[0]);c->VSGetConstantBuffers(13,1,&savedBuffers[1]);
 savedScissorCount=16;c->RSGetScissorRects(&savedScissorCount,savedScissors);
 D3D11_RECT full{LONG(viewport.TopLeftX),LONG(viewport.TopLeftY),LONG(viewport.TopLeftX+viewport.Width),LONG(viewport.TopLeftY+viewport.Height)};
 c->RSSetScissorRects(1,&full);ID3D11Buffer* buffers[]={global.Get(),omni.Get()};c->VSSetConstantBuffers(12,2,buffers);c->VSSetShader(replacement[mode-1].Get(),nullptr,0);changed=true;++counts[eye];
 if(trace)log("correct frame="+std::to_string(frame)+" eye="+std::to_string(eye)+" mode="+std::to_string(mode)+" viewport="+std::to_string(UINT(viewport.Width))+"x"+std::to_string(UINT(viewport.Height))+" originalScissors="+std::to_string(savedScissorCount));
}
void AfterDraw(ID3D11DeviceContext* c){
 if(!changed)return;c->VSSetShader(savedVertex.Get(),nullptr,0);ID3D11Buffer* buffers[]={savedBuffers[0].Get(),savedBuffers[1].Get()};c->VSSetConstantBuffers(12,2,buffers);c->RSSetScissorRects(savedScissorCount,savedScissors);savedVertex.Reset();for(auto& b:savedBuffers)b.Reset();changed=false;
}
void End(){if(trace)log("counts left="+std::to_string(counts[0])+" right="+std::to_string(counts[1]));active=false;eye=-1;}
}
