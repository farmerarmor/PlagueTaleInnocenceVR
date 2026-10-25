#include "DecalPass.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using Microsoft::WRL::ComPtr;
static unsigned checks{};
static void require(bool good,const char* text){++checks;if(!good){fprintf(stderr,"FAIL: %s\n",text);exit(1);}}
int wmain(int argc,wchar_t** argv){
 require(argc==3,"native marked shader and diagnostic directory supplied");std::ifstream file(argv[1],std::ios::binary);std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};require(!bytes.empty(),"captured native shader available");std::filesystem::path folder=argv[2];std::filesystem::create_directories(folder);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> immediate,deferred;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&immediate)),"hardware D3D11 device");require(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"deferred context");
 ComPtr<ID3D11PixelShader> target,other;require(SUCCEEDED(device->CreatePixelShader(bytes.data(),bytes.size(),nullptr,&target)),"native marked pixel shader");const char* source="float4 main():SV_Target{return 1;}";ComPtr<ID3DBlob> code;require(SUCCEEDED(D3DCompile(source,strlen(source),"validation",nullptr,nullptr,"main","ps_5_0",0,0,&code,nullptr)),"unrelated pixel shader fixture");require(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&other)),"unrelated pixel shader");
 DecalPass::Initialize(folder);DecalPass::RegisterShader(target.Get(),bytes.data(),bytes.size());DecalPass::RegisterShader(other.Get(),code->GetBufferPointer(),code->GetBufferSize());auto ini=folder/L"PlagueTaleVR.ini";uint64_t frame{};
 struct Setting {const wchar_t* value;const wchar_t* legacy;bool disabled;};
 for(auto setting:{Setting{L"0",L"2",false},{L"1",L"0",true},{nullptr,L"2",true},{nullptr,L"1",false}}){
  WritePrivateProfileStringW(L"Rendering",L"DisableDecals",setting.value,ini.c_str());WritePrivateProfileStringW(L"Rendering",L"DecalMode",setting.legacy,ini.c_str());Sleep(210);
  for(bool tracked:{false,true})for(int eye:{-1,0,1,2})for(bool marked:{false,true})for(bool worker:{false,true}){
   auto* c=worker?deferred.Get():immediate.Get();auto* ps=marked?target.Get():other.Get();c->PSSetShader(ps,nullptr,0);DecalPass::Pixel(ps);DecalPass::Begin(++frame,tracked,false,immediate.Get(),false);DecalPass::Eye(eye);bool allowed=DecalPass::BeforeDraw(c);
   require(allowed==!(setting.disabled && tracked && marked && !worker && eye>=0 && eye<2),"only marked pass in tracked native eyes follows optional disable setting");ComPtr<ID3D11PixelShader> bound;c->PSGetShader(&bound,nullptr,nullptr);require(bound.Get()==ps,"optional switch never substitutes or changes the bound shader");DecalPass::End(immediate.Get());
  }
 }
 require(SUCCEEDED(device->GetDeviceRemovedReason()),"device healthy");printf("PASS: %u optional decal checks: default on, persistent switch, legacy settings, both eyes, unrelated shaders, flat and deferred bypass.\n",checks);return 0;
}
