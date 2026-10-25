#include "LightingTrial.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <fstream>
#include <string>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace LightingTrial {
namespace {
const GUID RoleTag={0xdc742729,0x45c8,0x41c6,{0x89,0xba,0xa3,0x12,0x25,0xc9,0xf0,0x24}};
struct Role {UINT type{},sun{},cloud=UINT_MAX,sampler{},fog[2]={UINT_MAX,UINT_MAX};uint64_t hash{};};
thread_local Role pixel{};
thread_local bool active{},trace{},changedSun{},changedCloud{},changedFog[2]{};
thread_local uint64_t frame{};
thread_local int eye=-1;
bool noSun{},noCloud{},noIndirect{},noProjected{},noFog{};uint64_t settingsTick{};
std::filesystem::path directory;
ComPtr<ID3D11ShaderResourceView> savedSun,savedCloud,neutralSun[2],neutralCloud;
ComPtr<ID3D11ShaderResourceView> savedFog[2],neutralFog;
void log(const std::string& text){std::ofstream f(directory/L"lighting-trial.log",std::ios::app);f<<text<<'\n';}
bool neutral(ID3D11Device* d,bool white,bool array,ComPtr<ID3D11ShaderResourceView>& output){
 if(output)return true;
 D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=desc.MipLevels=1;desc.ArraySize=array?4:1;desc.Format=DXGI_FORMAT_R32_FLOAT;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 float value=white?1.f:0.f;D3D11_SUBRESOURCE_DATA input[4]{};for(auto& s:input){s.pSysMem=&value;s.SysMemPitch=sizeof(value);}
 ComPtr<ID3D11Texture2D> texture;if(FAILED(d->CreateTexture2D(&desc,input,&texture)))return false;
 D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=desc.Format;view.ViewDimension=array?D3D11_SRV_DIMENSION_TEXTURE2DARRAY:D3D11_SRV_DIMENSION_TEXTURE2D;
 if(array){view.Texture2DArray.MipLevels=1;view.Texture2DArray.ArraySize=4;}else view.Texture2D.MipLevels=1;
 return SUCCEEDED(d->CreateShaderResourceView(texture.Get(),&view,&output));
}
void settings(){
 auto now=GetTickCount64();if(now-settingsTick<200)return;settingsTick=now;auto ini=(directory/L"PlagueTaleVR.ini").wstring();
 bool sun=GetPrivateProfileIntW(L"Diagnostics",L"DisableSunShadowSample",0,ini.c_str())==1;
 bool cloud=GetPrivateProfileIntW(L"Diagnostics",L"DisableCloudShadowSample",0,ini.c_str())==1;
 bool indirect=GetPrivateProfileIntW(L"Diagnostics",L"DisableIndirectLightingDraw",0,ini.c_str())==1;
 bool projected=GetPrivateProfileIntW(L"Diagnostics",L"DisableProjectedLightingDraw",0,ini.c_str())==1;
 bool fog=GetPrivateProfileIntW(L"Diagnostics",L"DisableFogSample",0,ini.c_str())==1;
 if(sun!=noSun || cloud!=noCloud || indirect!=noIndirect || projected!=noProjected || fog!=noFog)log("settings sun="+std::to_string(sun)+" cloud="+std::to_string(cloud)+" indirect="+std::to_string(indirect)+" projected="+std::to_string(projected)+" fog="+std::to_string(fog));
 noSun=sun;noCloud=cloud;noIndirect=indirect;noProjected=projected;noFog=fog;
}
}
void Initialize(const std::filesystem::path& path){directory=path;}
void RegisterShader(ID3D11DeviceChild* shader,const void* code,SIZE_T size){
 if(!shader)return;uint64_t hash=14695981039346656037ull;for(SIZE_T i=0;i<size;++i){hash^=static_cast<const uint8_t*>(code)[i];hash*=1099511628211ull;}
 Role role{};role.hash=hash;
 if(hash==0x4cb8ca5fa695577aull || hash==0x94a04983a94d1bbeull)role.type=1;
 if(hash==0x14d9e102aad19c98ull)role.type=3;
 ComPtr<ID3D11ShaderReflection> reflection;if(FAILED(D3DReflect(code,size,__uuidof(ID3D11ShaderReflection),&reflection)))return;
 D3D11_SHADER_DESC shaderDesc{};if(FAILED(reflection->GetDesc(&shaderDesc)) || (shaderDesc.Version>>16)!=0)return;
 D3D11_SHADER_INPUT_BIND_DESC b{};
 D3D11_SHADER_BUFFER_DESC shadowDesc{};
 // Cover the captured material variants sharing the four-slice native
 // shadow receiver, rather than only one deferred material shader.
 auto* shadow=reflection->GetConstantBufferByName("CBufferShadowReceiveConstant_Z");
 if(shadow && SUCCEEDED(shadow->GetDesc(&shadowDesc)) && shadowDesc.Size==384 && SUCCEEDED(reflection->GetResourceBindingDescByName("sShadowMap",&b)) && b.Dimension==D3D_SRV_DIMENSION_TEXTURE2DARRAY){
  role.sun=b.BindPoint;
  if(SUCCEEDED(reflection->GetResourceBindingDescByName("sShadowMapSampler",&b)) && b.Type==D3D_SIT_SAMPLER){role.sampler=b.BindPoint;role.type=2;}
 }
 if(role.type==2 && SUCCEEDED(reflection->GetResourceBindingDescByName("sCloudShadowMap",&b)) && b.Dimension==D3D_SRV_DIMENSION_TEXTURE2D)role.cloud=b.BindPoint;
 if(SUCCEEDED(reflection->GetResourceBindingDescByName("sMipFog0",&b)) && b.Dimension==D3D_SRV_DIMENSION_TEXTURE2D){role.fog[0]=b.BindPoint;if(!role.type)role.type=4;}
 if(SUCCEEDED(reflection->GetResourceBindingDescByName("sMipFog1",&b)) && b.Dimension==D3D_SRV_DIMENSION_TEXTURE2D)role.fog[1]=b.BindPoint;
 if(!role.type)return;
 shader->SetPrivateData(RoleTag,sizeof(role),&role);
}
void Pixel(ID3D11PixelShader* s){pixel={};UINT size=sizeof(pixel);if(s && FAILED(s->GetPrivateData(RoleTag,&size,&pixel)))pixel={};}
void Begin(uint64_t id,bool tracked,bool capture){frame=id;active=tracked;trace=capture;eye=-1;settings();}
void Eye(int e){eye=e;}
bool BeforeDraw(ID3D11DeviceContext* c){
 if(!active || eye<0 || eye>1)return true;
 if((pixel.type==1 && noIndirect)||(pixel.type==3 && noProjected)){if(trace)log("skip frame="+std::to_string(frame)+" eye="+std::to_string(eye)+" role="+std::to_string(pixel.type));return false;}
 if(pixel.type!=2 && pixel.type!=4)return true;
 ComPtr<ID3D11Device> d;c->GetDevice(&d);
 if(noSun && pixel.type==2){
  ComPtr<ID3D11SamplerState> sampler;c->PSGetSamplers(pixel.sampler,1,&sampler);
  D3D11_SAMPLER_DESC sd{};if(sampler)sampler->GetDesc(&sd);
  const bool less=sd.ComparisonFunc==D3D11_COMPARISON_LESS || sd.ComparisonFunc==D3D11_COMPARISON_LESS_EQUAL;
  const bool greater=sd.ComparisonFunc==D3D11_COMPARISON_GREATER || sd.ComparisonFunc==D3D11_COMPARISON_GREATER_EQUAL;
  if(sampler && (less || greater) && neutral(d.Get(),less,true,neutralSun[less?1:0])){
   c->PSGetShaderResources(pixel.sun,1,&savedSun);D3D11_SHADER_RESOURCE_VIEW_DESC original{};if(savedSun)savedSun->GetDesc(&original);
   if(savedSun && original.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2DARRAY && original.Texture2DArray.ArraySize==4){auto* srv=neutralSun[less?1:0].Get();c->PSSetShaderResources(pixel.sun,1,&srv);changedSun=true;if(trace)log("sun neutral frame="+std::to_string(frame)+" eye="+std::to_string(eye)+" shader="+std::to_string(pixel.hash)+" slot="+std::to_string(pixel.sun)+" comparison="+std::to_string(sd.ComparisonFunc)+" value="+std::to_string(less?1:0));}
  }
 }
 if(noCloud && pixel.cloud!=UINT_MAX && neutral(d.Get(),true,false,neutralCloud)){
  c->PSGetShaderResources(pixel.cloud,1,&savedCloud);D3D11_SHADER_RESOURCE_VIEW_DESC original{};if(savedCloud)savedCloud->GetDesc(&original);
  if(savedCloud && original.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D){auto* srv=neutralCloud.Get();c->PSSetShaderResources(pixel.cloud,1,&srv);changedCloud=true;if(trace)log("cloud neutral frame="+std::to_string(frame)+" eye="+std::to_string(eye));}
 }
 if(noFog && pixel.fog[0]!=UINT_MAX){
  if(!neutralFog){
   D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
   const float zero[4]{};D3D11_SUBRESOURCE_DATA input{};input.pSysMem=zero;input.SysMemPitch=sizeof(zero);ComPtr<ID3D11Texture2D> texture;
   if(SUCCEEDED(d->CreateTexture2D(&desc,&input,&texture)))d->CreateShaderResourceView(texture.Get(),nullptr,&neutralFog);
  }
  if(neutralFog)for(unsigned i=0;i<2;++i)if(pixel.fog[i]!=UINT_MAX){
   c->PSGetShaderResources(pixel.fog[i],1,&savedFog[i]);D3D11_SHADER_RESOURCE_VIEW_DESC vd{};if(savedFog[i])savedFog[i]->GetDesc(&vd);
   if(savedFog[i] && vd.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D){auto* srv=neutralFog.Get();c->PSSetShaderResources(pixel.fog[i],1,&srv);changedFog[i]=true;if(trace)log("fog neutral frame="+std::to_string(frame)+" eye="+std::to_string(eye)+" shader="+std::to_string(pixel.hash)+" slot="+std::to_string(pixel.fog[i]));}
  }
 }
 return true;
}
void AfterDraw(ID3D11DeviceContext* c){
 if(changedSun){auto* srv=savedSun.Get();c->PSSetShaderResources(pixel.sun,1,&srv);changedSun=false;}savedSun.Reset();
 if(changedCloud){auto* srv=savedCloud.Get();c->PSSetShaderResources(pixel.cloud,1,&srv);changedCloud=false;}savedCloud.Reset();
 for(unsigned i=0;i<2;++i){if(changedFog[i]){auto* srv=savedFog[i].Get();c->PSSetShaderResources(pixel.fog[i],1,&srv);changedFog[i]=false;}savedFog[i].Reset();}
}
void End(){active=false;eye=-1;}
}
