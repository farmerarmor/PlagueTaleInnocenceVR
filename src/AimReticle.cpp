#include "AimReticle.h"
#include "AimReticleShader.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <cstring>
#include <string>
using Microsoft::WRL::ComPtr;
namespace AimReticle {
namespace {
const GUID RoleTag={0x78c2cab9,0xccd3,0x421e,{0xb6,0x36,0x7d,0xdc,0xa2,0xeb,0xab,0x49}};
std::filesystem::path directory;
thread_local UINT vertexRole{},pixelRole{},savedRole{};
thread_local bool active{},changed{},trace{};
thread_local int eye=-1;
thread_local uint64_t frame{};
thread_local CameraMath::Matrix transforms[2];
ComPtr<ID3D11VertexShader> replacements[3],savedVertex;
ComPtr<ID3D11Buffer> constants[2],savedConstants;
uint64_t updated[2]{},counts[2]{};
bool enabled=true;uint64_t settingsTick{};
UINT role(ID3D11DeviceChild* shader){UINT value{},size=sizeof(value);if(shader && FAILED(shader->GetPrivateData(RoleTag,&size,&value)))return 0;return value;}
void log(const std::string& message){std::ofstream f(directory/L"aim-reticle.log",std::ios::app);f<<message<<'\n';}
bool create(ID3D11Device* device,UINT variant){
 if(replacements[variant])return true;
 ComPtr<ID3DBlob> code,errors;
 const char* source=variant?AimIndicatorShaderCode:AimReticleShaderCode;
 D3D_SHADER_MACRO macros[]={{"BILLBOARD","1"},{nullptr,nullptr}};
 if(FAILED(D3DCompile(source,strlen(source),"PlagueVR-NativeAimReticle",variant==2?macros:nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors))){log("reticle shader compilation failed: "+std::string(errors?static_cast<const char*>(errors->GetBufferPointer()):"no compiler details"));return false;}
 if(FAILED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&replacements[variant])))return false;
 log("created native-aim reticle projection variant="+std::to_string(variant));return true;
}
}
void Initialize(const std::filesystem::path& path){directory=path;}
void RegisterShader(ID3D11DeviceChild* shader,const void* bytecode,SIZE_T size){
 if(!shader)return;uint64_t hash=14695981039346656037ull;
 for(SIZE_T i=0;i<size;++i){hash^=static_cast<const uint8_t*>(bytecode)[i];hash*=1099511628211ull;}
 UINT value=hash==0x956f7d8ab6c49cf8ull?1:hash==0x089fdfd845a6940full?2:0;
 if(hash==0x1fe6f6181d9219fbull)value=3;
 if(hash==0x2a61d83c081dd26bull)value=4;
 if(hash==0x4ad560d89db2c231ull)value=5;
 if(value)shader->SetPrivateData(RoleTag,sizeof(value),&value);
}
void Vertex(ID3D11VertexShader* shader){vertexRole=role(shader);}
void Pixel(ID3D11PixelShader* shader){pixelRole=role(shader);}
void Begin(uint64_t id,const Transport::Tracking& tracking,const CameraMath::Matrix* nativeWorld,const CameraMath::Matrix* headWorld,bool capture){
 frame=id;eye=-1;trace=capture;
 auto now=GetTickCount64();if(now-settingsTick>=100){settingsTick=now;enabled=GetPrivateProfileIntW(L"UI",L"CorrectAimReticle",1,(directory/L"PlagueTaleVR.ini").c_str())!=0;}
 active=tracking.valid && nativeWorld && headWorld && CameraMath::Rigid(*nativeWorld) && CameraMath::Rigid(*headWorld);
 if(!active)return;
 const auto aimToHead=CameraMath::Multiply(*nativeWorld,CameraMath::InverseRigid(*headWorld));
 CameraMath::Matrix depth{};depth.m[14]=.059f;
 for(unsigned i=0;i<2;++i)transforms[i]=CameraMath::Multiply(aimToHead,CameraMath::CenteredEyeProjection(depth,tracking.head,tracking.eyes[i],1.f));
 if(trace){
  std::ofstream f(directory/L"aim-reticle-captures.jsonl",std::ios::app);
  f<<"{\"sourceFrame\":"<<frame<<",\"poseId\":"<<tracking.id<<",\"enabled\":"<<enabled<<",\"nativeToHead\":[";
  for(unsigned i=0;i<16;++i){if(i)f<<',';f<<aimToHead.m[i];}f<<"]}\n";
 }
}
void Eye(int value){eye=value;}
void BeforeDraw(ID3D11DeviceContext* context,UINT vertices,bool indexed){
 if(!active || !enabled || changed || eye<0 || eye>1 || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 UINT variant{};
 if(!indexed && vertices==4 && vertexRole==1 && pixelRole==2)variant=0;
 else if(indexed && vertices==24 && (vertexRole==3 || vertexRole==4) && pixelRole==5)variant=vertexRole-2;
 else return;
 // Exact captured aim paths use a 128-square atlas and a full LDR target.
 ComPtr<ID3D11ShaderResourceView> image;context->PSGetShaderResources(0,1,&image);if(!image)return;
 ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;image->GetResource(&resource);resource.As(&texture);if(!texture)return;
 D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);if(desc.Width!=128 || desc.Height!=128 || desc.ArraySize!=1)return;
 ComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);if(!target)return;
 resource.Reset();texture.Reset();target->GetResource(&resource);resource.As(&texture);if(!texture)return;texture->GetDesc(&desc);
 D3D11_VIEWPORT viewport{};UINT count=1;context->RSGetViewports(&count,&viewport);
 if(desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 || count!=1 || viewport.TopLeftX!=0 || viewport.TopLeftY!=0 || viewport.Width!=float(desc.Width) || viewport.Height!=float(desc.Height))return;
 ComPtr<ID3D11Buffer> global;context->VSGetConstantBuffers(0,1,&global);if(!global)return;D3D11_BUFFER_DESC bd{};global->GetDesc(&bd);if(bd.ByteWidth!=1440)return;
 if(variant){ComPtr<ID3D11Buffer> object;context->VSGetConstantBuffers(variant==1?1:2,1,&object);if(!object)return;object->GetDesc(&bd);if(bd.ByteWidth!=512)return;
  if(variant==2){ComPtr<ID3D11Buffer> light;context->VSGetConstantBuffers(1,1,&light);if(!light)return;light->GetDesc(&bd);if(bd.ByteWidth!=48)return;}}
 ComPtr<ID3D11Device> device;context->GetDevice(&device);if(!create(device.Get(),variant))return;
 if(!constants[eye]){bd={};bd.ByteWidth=64;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(FAILED(device->CreateBuffer(&bd,nullptr,&constants[eye])))return;}
 if(updated[eye]!=frame){context->UpdateSubresource(constants[eye].Get(),0,nullptr,&transforms[eye],0,0);updated[eye]=frame;}
 context->VSGetShader(&savedVertex,nullptr,nullptr);context->VSGetConstantBuffers(13,1,&savedConstants);savedRole=vertexRole;
 context->VSSetShader(replacements[variant].Get(),nullptr,0);auto* buffer=constants[eye].Get();context->VSSetConstantBuffers(13,1,&buffer);changed=true;++counts[eye];
 if(trace)log("corrected frame="+std::to_string(frame)+" eye="+std::to_string(eye)+" variant="+std::to_string(variant)+" count="+std::to_string(counts[eye]));
}
void AfterDraw(ID3D11DeviceContext* context){
 if(!changed)return;context->VSSetShader(savedVertex.Get(),nullptr,0);auto* buffer=savedConstants.Get();context->VSSetConstantBuffers(13,1,&buffer);savedVertex.Reset();savedConstants.Reset();vertexRole=savedRole;changed=false;
}
void End(){active=false;eye=-1;}
}
