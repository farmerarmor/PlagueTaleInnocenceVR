#include "ProjectedLightBounds.h"
#include "ProjectedLightBoundsShader.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
using Microsoft::WRL::ComPtr;
static unsigned checks{};
static void require(bool good,const char* message){++checks;if(!good){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
static std::vector<char> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);std::vector<char> data{std::istreambuf_iterator<char>(f),{}};require(!data.empty(),"read captured fixture");return data;}
static ComPtr<ID3DBlob> compile(const char* source,const char* profile,bool full=false){D3D_SHADER_MACRO macros[]={{"FULL_SCREEN","1"},{nullptr,nullptr}};ComPtr<ID3DBlob> code,error;auto hr=D3DCompile(source,strlen(source),"bounds-validation",full?macros:nullptr,nullptr,"main",profile,0,0,&code,&error);if(FAILED(hr)&&error)fprintf(stderr,"%s\n",static_cast<const char*>(error->GetBufferPointer()));require(SUCCEEDED(hr),"compile shader");return code;}
int wmain(int argc,wchar_t** argv){
 require(argc==3,"fixture and output folders supplied");std::filesystem::path fixture=argv[1],output=argv[2];std::filesystem::create_directories(output);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> c;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&c)),"hardware D3D11 device");
 auto vsCode=read(fixture/L"native-vs.dxbc"),psCode=read(fixture/L"native-ps.dxbc");ComPtr<ID3D11VertexShader> nativeVS;ComPtr<ID3D11PixelShader> nativePS;
 require(SUCCEEDED(device->CreateVertexShader(vsCode.data(),vsCode.size(),nullptr,&nativeVS)),"create captured rectangle vertex shader");require(SUCCEEDED(device->CreatePixelShader(psCode.data(),psCode.size(),nullptr,&nativePS)),"create captured projected light pixel shader");
 ProjectedLightBounds::Initialize(output);ProjectedLightBounds::RegisterShader(nativeVS.Get(),vsCode.data(),vsCode.size());ProjectedLightBounds::RegisterShader(nativePS.Get(),psCode.data(),psCode.size());
 WritePrivateProfileStringW(L"Diagnostics",L"ProjectedLightBoundsMode",L"1",(output/L"PlagueTaleVR.ini").c_str());
 for(bool full:{false,true}){auto code=compile(ProjectedLightBoundsShaderCode,"vs_5_0",full);ComPtr<ID3D11VertexShader> v;require(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&v)),"create bounds shader variant");}
 auto gsCode=compile("struct V{float4 p:SV_Position;float4 uv:TEXCOORD0;};[maxvertexcount(1)]void main(point V input[1],inout PointStream<V> stream){stream.Append(input[0]);}","gs_5_0");
 D3D11_SO_DECLARATION_ENTRY decl[]={{0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,4,0}};UINT stride=32;ComPtr<ID3D11GeometryShader> gs;require(SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(gsCode->GetBufferPointer(),gsCode->GetBufferSize(),decl,2,&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs)),"stream-output actual vertex results");c->GSSetShader(gs.Get(),nullptr,0);
 D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0}};ComPtr<ID3D11InputLayout> layout;require(SUCCEEDED(device->CreateInputLayout(elements,3,vsCode.data(),vsCode.size(),&layout)),"native quad input layout");c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
 auto buffer=[&](UINT bytes,UINT bind,const void* data){D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=bind;d.Usage=D3D11_USAGE_DEFAULT;D3D11_SUBRESOURCE_DATA in{data,0,0};ComPtr<ID3D11Buffer> b;require(SUCCEEDED(device->CreateBuffer(&d,data?&in:nullptr,&b)),"create GPU buffer");return b;};
 const float quad[16]={-1,-1,0,1, 1,-1,0,1, -1,1,0,1, 1,1,0,1};auto vertex=buffer(sizeof(quad),D3D11_BIND_VERTEX_BUFFER,quad);auto* vb=vertex.Get();UINT vertexStride=16,offset=0;c->IASetVertexBuffers(0,1,&vb,&vertexStride,&offset);
 auto stream=buffer(4*32,D3D11_BIND_STREAM_OUTPUT,nullptr);D3D11_BUFFER_DESC sd{};sd.ByteWidth=4*32;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> staging;require(SUCCEEDED(device->CreateBuffer(&sd,nullptr,&staging)),"readback buffer");
 D3D11_VIEWPORT viewport{0,0,4000,4768,0,1};c->RSSetViewports(1,&viewport);D3D11_RECT scissors[2]={{10,20,3000,3500},{30,40,3900,4700}};
 uint64_t frame{};
 for(UINT eye=0;eye<2;++eye){
  auto g=read(fixture/(L"global-eye"+std::to_wstring(eye)+L".bin")),o=read(fixture/(L"omni-eye"+std::to_wstring(eye)+L".bin")),u=read(fixture/(L"user-eye"+std::to_wstring(eye)+L".bin"));
  require(g.size()==1440 && o.size()==1120 && u.size()==928,"captured constant layouts exact");auto gb=buffer(UINT(g.size()),D3D11_BIND_CONSTANT_BUFFER,g.data()),ob=buffer(UINT(o.size()),D3D11_BIND_CONSTANT_BUFFER,o.data()),ub=buffer(UINT(u.size()),D3D11_BIND_CONSTANT_BUFFER,u.data());
  auto* global=gb.Get();auto* omni=ob.Get();auto* user=ub.Get();c->PSSetConstantBuffers(0,1,&global);c->PSSetConstantBuffers(2,1,&omni);c->VSSetConstantBuffers(0,1,&user);ID3D11Buffer* previous[]={ub.Get(),gb.Get()};c->VSSetConstantBuffers(12,2,previous);
  auto run=[&](bool correct){c->VSSetShader(nativeVS.Get(),nullptr,0);c->PSSetShader(nativePS.Get(),nullptr,0);c->RSSetScissorRects(2,scissors);ProjectedLightBounds::Begin(++frame,true,false);ProjectedLightBounds::Eye(int(eye));if(correct)ProjectedLightBounds::BeforeDraw(c.Get());ComPtr<ID3D11VertexShader> bound;c->VSGetShader(&bound,nullptr,nullptr);require((bound.Get()!=nativeVS.Get())==correct,"only enabled target draw substituted");
   auto* so=stream.Get();UINT start=0;c->SOSetTargets(1,&so,&start);c->Draw(4,0);ID3D11Buffer* empty{};c->SOSetTargets(1,&empty,&start);ProjectedLightBounds::AfterDraw(c.Get());ProjectedLightBounds::End();
   c->VSGetShader(&bound,nullptr,nullptr);require(bound.Get()==nativeVS.Get(),"native vertex shader restored");ID3D11Buffer* restored[2]{};c->VSGetConstantBuffers(12,2,restored);for(UINT i=0;i<2;++i){require(restored[i]==previous[i],"extra constant slot restored");if(restored[i])restored[i]->Release();}
   D3D11_RECT restoredRects[16]{};UINT n=16;c->RSGetScissorRects(&n,restoredRects);require(n==2 && !memcmp(scissors,restoredRects,sizeof(scissors)),"both original scissor rectangles restored");
   c->CopyResource(staging.Get(),stream.Get());D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"read GPU vertex output");std::array<float,32> result;memcpy(result.data(),mapped.pData,sizeof(result));c->Unmap(staging.Get(),0);return result;};
  auto native=run(false),corrected=run(true);const auto* globals=reinterpret_cast<const float*>(g.data());const auto* light=reinterpret_cast<const float*>(o.data());const auto* userValues=reinterpret_cast<const float*>(u.data());
  float low[2]={1e20f,1e20f},high[2]={-1e20f,-1e20f};for(UINT corner=0;corner<8;++corner){float p[4]={light[0]+light[13]*((corner&1)?1:-1),light[1]+light[13]*((corner&2)?1:-1),light[2]+light[13]*((corner&4)?1:-1),1};float clip[4]{};for(UINT row=0;row<4;++row)for(UINT col=0;col<4;++col)clip[row]+=p[col]*globals[49*4+row*4+col];require(clip[3]>0,"captured light volume ahead of eye");for(UINT axis=0;axis<2;++axis){float q=clip[axis]/clip[3];low[axis]=std::min(low[axis],q);high[axis]=std::max(high[axis],q);}}
  bool nativeClipped=false;for(UINT axis=0;axis<2;++axis){nativeClipped=nativeClipped || low[axis]<userValues[2+axis]-fabsf(userValues[axis]) || high[axis]>userValues[2+axis]+fabsf(userValues[axis]);low[axis]=std::max(-1.f,low[axis]-.002f);high[axis]=std::min(1.f,high[axis]+.002f);}
  require(nativeClipped,"actual native rectangle excludes projected volume in this eye");for(UINT v=0;v<4;++v){for(UINT axis=0;axis<2;++axis){float input=quad[v*4+axis];require(fabsf(native[v*8+axis]-(input*userValues[axis]+userValues[2+axis]))<1e-5f,"native rectangle reproduced by captured shader");float expected=(low[axis]+high[axis])*.5f-input*(high[axis]-low[axis])*.5f;require(fabsf(corrected[v*8+axis]-expected)<1e-5f,"GPU rectangle matches full-eye projected bounds");}require(fabsf(corrected[v*8+4]-(corrected[v*8]*.5f+.5f))<1e-5f && fabsf(corrected[v*8+5]-(.5f-corrected[v*8+1]*.5f))<1e-5f,"depth sampling UV agrees with corrected screen position");}
  c->VSSetShader(nativeVS.Get(),nullptr,0);ProjectedLightBounds::Begin(++frame,false,false);ProjectedLightBounds::Eye(int(eye));ProjectedLightBounds::BeforeDraw(c.Get());ComPtr<ID3D11VertexShader> flat;c->VSGetShader(&flat,nullptr,nullptr);require(flat.Get()==nativeVS.Get(),"flat rendering bypassed");ProjectedLightBounds::End();
 }
 require(SUCCEEDED(device->GetDeviceRemovedReason()),"device healthy");printf("PASS: %u projected-light GPU checks: captured native clipping, both-eye conservative bounds, sampling UV, scissor/CB/shader restoration and flat bypass.\n",checks);return 0;
}
