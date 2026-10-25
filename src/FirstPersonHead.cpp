#include "FirstPersonHead.h"
#include "FirstPersonHeadShader.h"
#include "FirstPersonMouthMesh.h"
#include <wrl/client.h>
#include <fstream>
using Microsoft::WRL::ComPtr;
namespace FirstPersonHead {
namespace {
const GUID RoleTag={0x27a76bd8,0xd524,0x45e9,{0xb9,0x57,0x8e,0x2b,0xf3,0x0a,0xd1,0x55}};
const GUID CodeTag={0x4f17848f,0x664f,0x40b5,{0x9d,0xc5,0x84,0xa8,0xef,0x76,0x11,0xa2}};
struct Material {uint64_t hash;UINT objectSlot;bool hide=true;};
// 4454 is shared by hands and oral geometry. Its head substitution additionally
// requires the exact captured oral vertex/index contents and valid draw range.
constexpr Material materials[]={{0xff676ccf866fa591ull,2},{0x28845477e81a348eull,2},{0xbe83a74e76f54f2full,2},{0x4454d6441802bb5dull,2,false},{0x77223718785a511cull,5},{0xa97f3beec18e4446ull,5},{0xa5996970fccc10f6ull,4}};
constexpr unsigned MaterialCount=sizeof(materials)/sizeof(materials[0]);
std::filesystem::path directory;ComPtr<ID3D11PixelShader> replacements[MaterialCount];ComPtr<ID3D11Buffer> constants;
thread_local ComPtr<ID3D11PixelShader> savedPixel;thread_local ComPtr<ID3D11Buffer> savedConstants;
thread_local UINT role{},savedRole{};thread_local int eye=-1;thread_local bool active{},changed{},trace{};thread_local uint64_t frame{};thread_local Control control{};
uint64_t uploaded{},settingsTick{},counts[2][MaterialCount]{};bool enabled=true;
void log(const std::string& s){std::ofstream f(directory/L"first-person-head.log",std::ios::app);f<<s<<'\n';}
bool create(ID3D11Device* device,ID3D11PixelShader* native,UINT variant){
 if(replacements[variant])return true;UINT size{};native->GetPrivateData(CodeTag,&size,nullptr);if(!size || size>1024*1024)return false;std::vector<uint8_t> bytes(size);if(FAILED(native->GetPrivateData(CodeTag,&size,bytes.data())))return false;
 ComPtr<ID3DBlob> code;std::string error;auto hr=FirstPersonHeadShader::Build(bytes.data(),size,&code,error);if(FAILED(hr)){log(error);return false;}
 if(FAILED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&replacements[variant])))return false;
 log("created local-player material mask="+std::to_string(variant));return true;
}
}
void Initialize(const std::filesystem::path& path){directory=path;FirstPersonMouthMesh::Reset();}
void RegisterShader(ID3D11DeviceChild* shader,const void* bytes,SIZE_T size){
 if(!shader)return;uint64_t hash=14695981039346656037ull;for(SIZE_T i=0;i<size;++i){hash^=static_cast<const uint8_t*>(bytes)[i];hash*=1099511628211ull;}
 UINT value{};for(unsigned i=0;i<MaterialCount;++i)if(hash==materials[i].hash){UINT slot{};if(SUCCEEDED(FirstPersonHeadShader::ObjectSlot(bytes,size,slot)) && slot==materials[i].objectSlot)value=i+1;break;}
 if(value){shader->SetPrivateData(RoleTag,sizeof(value),&value);shader->SetPrivateData(CodeTag,UINT(size),bytes);}
}
void Pixel(ID3D11PixelShader* shader){role=0;UINT size=sizeof(role);if(shader && FAILED(shader->GetPrivateData(RoleTag,&size,&role)))role=0;}
bool SharedSkin(ID3D11PixelShader* shader){UINT value{},size=sizeof(value);return shader && SUCCEEDED(shader->GetPrivateData(RoleTag,&size,&value)) && (value==2 || value==4);}
void Begin(uint64_t id,bool firstPerson,const CameraMath::Matrix& player,const CameraMath::Matrix& view,bool capture){
 frame=id;trace=capture;eye=-1;auto now=GetTickCount64();if(now-settingsTick>=100){settingsTick=now;enabled=GetPrivateProfileIntW(L"Camera",L"HideFirstPersonHead",1,(directory/L"PlagueTaleVR.ini").c_str())!=0;}
 active=firstPerson && enabled && CameraMath::Rigid(player) && CameraMath::Rigid(view);if(!active)return;control={};
 for(unsigned i=0;i<3;++i){control.player[i]=player.m[12+i];control.origin[i]=view.m[12+i];for(unsigned j=0;j<3;++j){control.playerRows[i][j]=player.m[j*4+i];control.viewRows[i][j]=view.m[i*4+j];}}
}
void Eye(int value){eye=value;}
void BeforeDraw(ID3D11DeviceContext* c,UINT count,UINT start,INT base,bool indexed){
 if(!active || eye<0 || eye>1 || changed || !role || c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 if(!materials[role-1].hide && !FirstPersonMouthMesh::Match(c,frame,count,start,base,indexed))return;
 // The shader itself verifies the player object and main scene camera. The
 // original shader remains active for every other character and shadow view.
 ComPtr<ID3D11Buffer> global,object;c->PSGetConstantBuffers(0,1,&global);c->PSGetConstantBuffers(materials[role-1].objectSlot,1,&object);if(!global || !object)return;D3D11_BUFFER_DESC bd{};global->GetDesc(&bd);if(bd.ByteWidth!=1440)return;object->GetDesc(&bd);if(bd.ByteWidth!=512)return;
 ComPtr<ID3D11PixelShader> native;c->PSGetShader(&native,nullptr,nullptr);if(!native)return;ComPtr<ID3D11Device> device;c->GetDevice(&device);const auto variant=role-1;if(!create(device.Get(),native.Get(),variant))return;
 if(!constants){bd={};bd.ByteWidth=sizeof(Control);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(FAILED(device->CreateBuffer(&bd,nullptr,&constants)))return;}
 if(uploaded!=frame){c->UpdateSubresource(constants.Get(),0,nullptr,&control,0,0);uploaded=frame;}
 savedPixel=native;c->PSGetConstantBuffers(13,1,&savedConstants);savedRole=role;c->PSSetShader(replacements[variant].Get(),nullptr,0);auto* buffer=constants.Get();c->PSSetConstantBuffers(13,1,&buffer);changed=true;++counts[eye][variant];
}
void AfterDraw(ID3D11DeviceContext* c){if(!changed)return;c->PSSetShader(savedPixel.Get(),nullptr,0);auto* buffer=savedConstants.Get();c->PSSetConstantBuffers(13,1,&buffer);role=savedRole;savedPixel.Reset();savedConstants.Reset();changed=false;}
void End(){
 if(trace){std::ofstream f(directory/L"first-person-head.json");f<<"{\"sourceFrame\":"<<frame<<",\"active\":"<<active<<",\"drawCandidates\":[";for(unsigned e=0;e<2;++e){if(e)f<<',';f<<'[';for(unsigned i=0;i<MaterialCount;++i){if(i)f<<',';f<<counts[e][i];}f<<']';}f<<"],\"mouthMesh\":"<<FirstPersonMouthMesh::Status()<<'}';}
 active=false;eye=-1;
}
}
