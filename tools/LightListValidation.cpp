#include "LightList.h"
#include "LightListShader.h"
#include "CameraMath.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <fstream>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using Microsoft::WRL::ComPtr;
static unsigned checks{};
static void require(bool good,const char* message){++checks;if(!good){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
static ComPtr<ID3DBlob> compile(UINT variant){ComPtr<ID3DBlob> code,error;D3D_SHADER_MACRO macros[]={{"NO_TILE_CULL","1"},{nullptr,nullptr}};auto hr=D3DCompile(LightListShaderCode,strlen(LightListShaderCode),"validation",variant?macros:nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);if(FAILED(hr)&&error)fprintf(stderr,"%s\n",static_cast<const char*>(error->GetBufferPointer()));require(SUCCEEDED(hr),"compile light-list variant");return code;}
static ComPtr<ID3D11Buffer> makeBuffer(ID3D11Device* d,UINT size,UINT bind,const void* data){D3D11_BUFFER_DESC desc{};desc.ByteWidth=size;desc.BindFlags=bind;desc.Usage=D3D11_USAGE_DEFAULT;D3D11_SUBRESOURCE_DATA input{data,0,0};ComPtr<ID3D11Buffer> b;require(SUCCEEDED(d->CreateBuffer(&desc,data?&input:nullptr,&b)),"create constant buffer");return b;}
int wmain(int argc,wchar_t** argv){
 require(argc==3,"native shader path and diagnostic output path supplied");std::filesystem::path dir=argv[2];std::filesystem::create_directories(dir);
 std::ifstream file(argv[1],std::ios::binary);std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};require(!bytes.empty(),"captured native bytecode available");
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> c;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&c)),"hardware device");
 ComPtr<ID3D11ComputeShader> native;require(SUCCEEDED(device->CreateComputeShader(bytes.data(),bytes.size(),nullptr,&native)),"create captured native light-list kernel");
 for(UINT variant=0;variant<2;++variant){auto code=compile(variant);ComPtr<ID3D11ComputeShader> shader;require(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader)),"create replacement variant");}
 LightList::Initialize(dir);LightList::RegisterShader(native.Get(),bytes.data(),bytes.size());
 WritePrivateProfileStringW(L"Diagnostics",L"LightListMode",L"1",(dir/L"PlagueTaleVR.ini").c_str());
 std::array<float,360> global{};std::array<float,232> user{};std::array<float,15364> lights{};
 user[0]=user[1]=2;user[2]=user[3]=1;user[4]=user[5]=32;user[6]=user[7]=1.f/32;
 UINT count=1;memcpy(lights.data()+15360,&count,4);lights[14338]=10;lights[14339]=2;
 auto gb=makeBuffer(device.Get(),sizeof(global),D3D11_BIND_CONSTANT_BUFFER,global.data()),ub=makeBuffer(device.Get(),sizeof(user),D3D11_BIND_CONSTANT_BUFFER,user.data()),lb=makeBuffer(device.Get(),sizeof(lights),D3D11_BIND_CONSTANT_BUFFER,lights.data());
 ID3D11Buffer* constants[]={gb.Get(),ub.Get(),lb.Get()};c->CSSetConstantBuffers(0,3,constants);
 std::array<float,1024> depth{};depth.fill(-0.00001f+0.059f/10);
 D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=32;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA data{depth.data(),32*4,0};ComPtr<ID3D11Texture2D> texture;require(SUCCEEDED(device->CreateTexture2D(&td,&data,&texture)),"synthetic constant-depth texture");
 ComPtr<ID3D11ShaderResourceView> depthView;require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,&depthView)),"depth SRV");auto* srv=depthView.Get();c->CSSetShaderResources(1,1,&srv);
 D3D11_BUFFER_DESC out{};out.ByteWidth=1024*4;out.BindFlags=D3D11_BIND_UNORDERED_ACCESS;out.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;out.StructureByteStride=4;ComPtr<ID3D11Buffer> output;require(SUCCEEDED(device->CreateBuffer(&out,nullptr,&output)),"native-layout tile records");
 D3D11_UNORDERED_ACCESS_VIEW_DESC vd{};vd.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;vd.Buffer.NumElements=1024;ComPtr<ID3D11UnorderedAccessView> uav;require(SUCCEEDED(device->CreateUnorderedAccessView(output.Get(),&vd,&uav)),"structured light-list UAV");auto* view=uav.Get();c->CSSetUnorderedAccessViews(0,1,&view,nullptr);
 out.Usage=D3D11_USAGE_STAGING;out.BindFlags=out.MiscFlags=out.StructureByteStride=0;out.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> staging;require(SUCCEEDED(device->CreateBuffer(&out,nullptr,&staging)),"readback buffer");
 uint64_t frame{};auto run=[&](bool correct){
  UINT zero[4]{};c->ClearUnorderedAccessViewUint(uav.Get(),zero);c->CSSetShader(native.Get(),nullptr,0);LightList::Compute(native.Get());LightList::Begin(++frame,true,false);LightList::Eye(0);
  if(correct){LightList::BeforeDispatch(c.Get(),2,2,1);ComPtr<ID3D11ComputeShader> selected;c->CSGetShader(&selected,nullptr,nullptr);require(selected.Get()!=native.Get(),"exact native kernel replaced");}
  c->Dispatch(2,2,1);LightList::AfterDispatch(c.Get());ComPtr<ID3D11ComputeShader> restored;c->CSGetShader(&restored,nullptr,nullptr);require(restored.Get()==native.Get(),"native shader restored after dispatch");LightList::End();
  c->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"GPU tile readback");std::array<std::set<UINT>,4> result;
  auto* words=static_cast<const UINT*>(mapped.pData);for(UINT tile=0;tile<4;++tile){UINT n=words[tile*256];require(n<=64,"native per-tile capacity preserved");for(UINT i=0;i<n;++i)result[tile].insert(words[tile*256+1+i]);}c->Unmap(staging.Get(),0);return result;
 };
 for(float shift:{0.f,0.6f,-0.6f}){
  CameraMath::Matrix p{};p.m[0]=p.m[5]=1;p.m[2]=shift;p.m[10]=-0.00001f;p.m[11]=0.059f;p.m[14]=1;CameraMath::Matrix inverse{};require(CameraMath::Inverse(p,inverse),"synthetic asymmetric projection invertible");
  memcpy(global.data()+49*4,p.m,64);memcpy(global.data()+53*4,inverse.m,64);lights[14336]=shift>0?-4.f:shift<0?4.f:0.f;
  c->UpdateSubresource(gb.Get(),0,nullptr,global.data(),0,0);c->UpdateSubresource(lb.Get(),0,nullptr,lights.data(),0,0);
  auto old=run(false),fixed=run(true);
  if(shift==0){require(old==fixed,"symmetric projection preserves native tile membership");for(auto& tile:fixed)require(tile.contains(0),"symmetric sphere reaches expected tile");}
  else {UINT column=shift>0?1:0;for(UINT row=0;row<2;++row){UINT tile=row*2+column;require(!old[tile].contains(0),"native diagonal-only planes reproduce omitted visible light");require(fixed[tile].contains(0),"full projection includes visible light for this asymmetric eye");}}
 }
 require(SUCCEEDED(device->GetDeviceRemovedReason()),"GPU device healthy after comparison");printf("PASS: %u light-list GPU checks: original symmetry, both asymmetric eyes, tile capacity and shader restoration.\n",checks);return 0;
}
