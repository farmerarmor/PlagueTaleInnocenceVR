#include "ScreenUI.h"
#include "CameraMath.h"
#include "InventoryUIShader.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <string>
#include <cmath>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace ScreenUI {
namespace {
const GUID RoleTag={0xa091a23a,0x4e28,0x43fd,{0x98,0x4c,0x27,0x67,0xc2,0x1d,0xbc,0xa1}};
std::filesystem::path directory;
thread_local UINT vertexRole{},pixelRole{};
thread_local bool active{},trace{},changed{};
thread_local int eye=-1;
thread_local uint64_t frame{};
thread_local Transport::Tracking tracking{};
ComPtr<ID3D11VertexShader> replacements[4];
ComPtr<ID3D11Buffer> panelConstants[2];
uint64_t updated[2]{};
ComPtr<ID3D11VertexShader> savedVertex;
ComPtr<ID3D11Buffer> savedConstants;
UINT savedVertexRole{};
bool enabled=true;float width=2.4f,distance=2.f;
uint64_t settingsTick{},counts[2]{};
void log(const std::string& text){std::ofstream f(directory/L"screen-ui.log",std::ios::app);f<<text<<'\n';}
UINT role(ID3D11DeviceChild* shader){UINT result{},size=sizeof(result);if(shader && FAILED(shader->GetPrivateData(RoleTag,&size,&result)))return 0;return result;}
const char* code=R"hlsl(
cbuffer Global : register(b0) {float4 c[90];};
cbuffer User : register(b1) {float4 u[58];};
cbuffer VRPanel : register(b13) {row_major float4x4 panel;};
struct Input {float4 position:POSITION;float4 color:COLOR;float2 uv:TEXCOORD0;};
struct Output {float4 position:SV_Position;float4 color:COLOR;float4 uv:TEXCOORD0;
#ifdef FONT
 float2 font:TEXCOORD1;float instanceColor:INSTANCE_COLOR;
#else
 float instanceColor:INSTANCE_COLOR;
#endif
 uint2 indexes:INSTANCE_INDEXES;};
Output main(Input v) {
 Output o=(Output)0;
 float2 logical=(v.position.xy-c[16].zw)/c[16].xy;
 float nativeW=v.position.z*c[52].z+c[52].w;
 float nativeZ=v.position.z*c[51].z+c[51].w;
 float4 clip=mul(float4(logical,0,1),panel);
 o.position=float4(clip.xy,nativeZ/max(abs(nativeW),1e-7)*clip.w,clip.w);
 o.color=v.color;o.uv.xy=v.uv;
#ifdef FONT
 o.color.xyz+=u[3].xyz*u[3].w;
 o.uv.zw=v.uv*u[0].xy;o.font=u[2].xz;
#endif
 return o;
}
)hlsl";
bool create(ID3D11Device* d,UINT variant){
 if(replacements[variant])return true;
 D3D_SHADER_MACRO macros[]={{variant==1?"FONT":"LIT","1"},{nullptr,nullptr}};
 ComPtr<ID3DBlob> shader,errors;
 const char* source=variant<2?code:InventoryUIShaderCode;
 if(FAILED(D3DCompile(source,strlen(source),"PlagueVR-ScreenUI",variant==1 || variant==2?macros:nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors))){log("UI shader compilation failed");return false;}
 if(FAILED(d->CreateVertexShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&replacements[variant])))return false;
 log("created native UI variant="+std::to_string(variant));return true;
}
void settings(){
 auto now=GetTickCount64();if(now-settingsTick<200)return;settingsTick=now;
 const auto ini=(directory/L"PlagueTaleVR.ini").wstring();
 enabled=GetPrivateProfileIntW(L"UI",L"CorrectScreenUI",1,ini.c_str())!=0;
 wchar_t value[64]{};
 GetPrivateProfileStringW(L"UI",L"WidthMeters",L"2.4",value,64,ini.c_str());float w=wcstof(value,nullptr);
 GetPrivateProfileStringW(L"UI",L"DistanceMeters",L"2.0",value,64,ini.c_str());float z=wcstof(value,nullptr);
 if(std::isfinite(w) && w>=.5f && w<=5.f)width=w;
 if(std::isfinite(z) && z>=.5f && z<=10.f)distance=z;
}
}
void Initialize(const std::filesystem::path& dir){directory=dir;}
void RegisterShader(ID3D11DeviceChild* shader,const void* bytecode,SIZE_T size){
 if(!shader)return;uint64_t hash=14695981039346656037ull;
 for(SIZE_T i=0;i<size;++i){hash^=static_cast<const uint8_t*>(bytecode)[i];hash*=1099511628211ull;}
 UINT r{};
 // Captured screen-pixel vertices, image composite and signed-distance font.
 if(hash==0x7c8aa06c1e5fc378ull)r=1;
 if(hash==0xe5888dbb72eebe8bull)r=2;
 if(hash==0xe36ed4bcd207c235ull)r=3;
 if(hash==0x5b5eda9e65b367e2ull)r=4;
 if(hash==0x1582f381218e03aeull)r=5; // Alpha-tested HUD atlas.
 if(hash==0x5132b28ca7564882ull)r=6; // Inventory lit mesh.
 if(hash==0x14aec2e0c3461f0bull)r=7;
 if(hash==0x1fe6f6181d9219fbull)r=8; // Inventory silhouette, shared with aim mesh.
 if(hash==0x0c7764018d343db9ull)r=9;
 if(r)shader->SetPrivateData(RoleTag,sizeof(r),&r);
}
void Vertex(ID3D11VertexShader* shader){vertexRole=role(shader);}
void Pixel(ID3D11PixelShader* shader){pixelRole=role(shader);}
void Begin(uint64_t id,const Transport::Tracking& pose,bool capture){frame=id;tracking=pose;active=pose.valid!=0;trace=capture;eye=-1;settings();}
void Eye(int value){eye=value;}
void BeforeDraw(ID3D11DeviceContext* c){
 if(!active || !enabled || eye<0 || eye>1 || changed || c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 UINT variant{};
 if(vertexRole==1 && (pixelRole==3 || pixelRole==5))variant=0;
 else if(vertexRole==2 && pixelRole==4)variant=1;
 else if(vertexRole==6 && pixelRole==7)variant=2;
 else if(vertexRole==8 && pixelRole==9)variant=3;
 else return;
 // Restrict the correction to the final native full-sized LDR target.
 ComPtr<ID3D11RenderTargetView> target;c->OMGetRenderTargets(1,&target,nullptr);if(!target)return;
 ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;target->GetResource(&resource);resource.As(&texture);if(!texture)return;
 D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
 D3D11_VIEWPORT viewport{};UINT count=1;c->RSGetViewports(&count,&viewport);
 if(desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 || count!=1 || viewport.TopLeftX!=0 || viewport.TopLeftY!=0 || viewport.Width!=float(desc.Width) || viewport.Height!=float(desc.Height))return;
 ComPtr<ID3D11Buffer> global;c->VSGetConstantBuffers(0,1,&global);if(!global)return;
 D3D11_BUFFER_DESC bd{};global->GetDesc(&bd);if(bd.ByteWidth!=1440)return;
 if(variant>=2){ComPtr<ID3D11Buffer> object;c->VSGetConstantBuffers(1,1,&object);if(!object)return;object->GetDesc(&bd);if(bd.ByteWidth!=512)return;}
 ComPtr<ID3D11Device> d;c->GetDevice(&d);if(!create(d.Get(),variant))return;
 if(!panelConstants[eye]){D3D11_BUFFER_DESC b{};b.ByteWidth=64;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(FAILED(d->CreateBuffer(&b,nullptr,&panelConstants[eye])))return;}
 if(updated[eye]!=frame){auto matrix=CameraMath::ScreenPanelProjection(tracking.head,tracking.eyes[eye],width,distance);c->UpdateSubresource(panelConstants[eye].Get(),0,nullptr,&matrix,0,0);updated[eye]=frame;}
 c->VSGetShader(&savedVertex,nullptr,nullptr);c->VSGetConstantBuffers(13,1,&savedConstants);savedVertexRole=vertexRole;
 c->VSSetShader(replacements[variant].Get(),nullptr,0);auto* buffer=panelConstants[eye].Get();c->VSSetConstantBuffers(13,1,&buffer);changed=true;++counts[eye];
 if(trace)log("corrected frame="+std::to_string(frame)+" pose="+std::to_string(tracking.id)+" eye="+std::to_string(eye)+" variant="+std::to_string(variant)+" width="+std::to_string(width)+" distance="+std::to_string(distance));
}
void AfterDraw(ID3D11DeviceContext* c){
 if(!changed)return;c->VSSetShader(savedVertex.Get(),nullptr,0);auto* buffer=savedConstants.Get();c->VSSetConstantBuffers(13,1,&buffer);savedVertex.Reset();savedConstants.Reset();vertexRole=savedVertexRole;changed=false;
}
void End(){active=false;eye=-1;}
}
