#include "AimReticle.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
using Microsoft::WRL::ComPtr;
static unsigned checks{};
static void require(bool value,const char* label){++checks;if(!value){fprintf(stderr,"FAIL: %s\n",label);exit(1);}}
static std::vector<char> read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);std::vector<char> bytes{std::istreambuf_iterator<char>(f),{}};require(!bytes.empty(),"captured fixture available");return bytes;}
static std::array<float,4> transform(std::array<float,4> p,const CameraMath::Matrix& m){std::array<float,4> out{};for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i)out[j]+=p[i]*m.m[4*i+j];return out;}
int wmain(int argc,wchar_t** argv){
 require(argc==3,"fixture and output directories supplied");std::filesystem::path fixture=argv[1],output=argv[2];std::filesystem::create_directories(output);
 auto vsBytes=read(fixture/L"shader-956f7d8ab6c49cf8.dxbc"),psBytes=read(fixture/L"shader-89fdfd845a6940f.dxbc"),globals=read(fixture/L"sample-146-vs-cb0.bin");
 require(globals.size()==1440,"captured native reticle global layout");
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> c;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&c)),"hardware D3D11 device");
 ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;require(SUCCEEDED(device->CreateVertexShader(vsBytes.data(),vsBytes.size(),nullptr,&vs)),"captured reticle vertex shader");require(SUCCEEDED(device->CreatePixelShader(psBytes.data(),psBytes.size(),nullptr,&ps)),"captured reticle pixel shader");
 AimReticle::Initialize(output);AimReticle::RegisterShader(vs.Get(),vsBytes.data(),vsBytes.size());AimReticle::RegisterShader(ps.Get(),psBytes.data(),psBytes.size());
 WritePrivateProfileStringW(L"UI",L"CorrectAimReticle",L"1",(output/L"PlagueTaleVR.ini").c_str());
 auto buffer=[&](UINT size,UINT flags,const void* data){D3D11_BUFFER_DESC desc{};desc.ByteWidth=size;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=flags;D3D11_SUBRESOURCE_DATA initial{data,0,0};ComPtr<ID3D11Buffer> result;require(SUCCEEDED(device->CreateBuffer(&desc,data?&initial:nullptr,&result)),"create GPU fixture buffer");return result;};
 auto global=buffer(UINT(globals.size()),D3D11_BIND_CONSTANT_BUFFER,globals.data()),sentinel=buffer(64,D3D11_BIND_CONSTANT_BUFFER,nullptr);auto* gb=global.Get();auto* sb=sentinel.Get();c->VSSetConstantBuffers(0,1,&gb);c->VSSetConstantBuffers(13,1,&sb);
 struct Vertex {float position[4],color[4],uv[2];};
 const Vertex vertices[]={{{-.025f,-.025f,2.f,1},{.9f,.7f,.2f,1},{0,1}},{{.025f,-.025f,2.f,1},{.9f,.7f,.2f,1},{1,1}},{{-.025f,.025f,2.f,1},{.9f,.7f,.2f,1},{0,0}},{{.025f,.025f,2.f,1},{.9f,.7f,.2f,1},{1,0}}};
 auto vb=buffer(sizeof(vertices),D3D11_BIND_VERTEX_BUFFER,vertices);auto* v=vb.Get();UINT stride=sizeof(Vertex),zero=0;c->IASetVertexBuffers(0,1,&v,&stride,&zero);
 D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};ComPtr<ID3D11InputLayout> layout;require(SUCCEEDED(device->CreateInputLayout(elements,3,vsBytes.data(),vsBytes.size(),&layout)),"captured reticle input signature");c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
 const char* gsText="struct V{float4 p:SV_Position;float4 previous:PREVIOUS_POSITION;float4 color:COLOR;float3 uv:TEXCOORD0;float instance:INSTANCE_COLOR;uint2 indexes:INSTANCE_INDEXES;};[maxvertexcount(1)]void main(point V input[1],inout PointStream<V> stream){stream.Append(input[0]);}";
 ComPtr<ID3DBlob> code,errors;require(SUCCEEDED(D3DCompile(gsText,strlen(gsText),"reticle-test",nullptr,nullptr,"main","gs_5_0",0,0,&code,&errors)),"compile GPU output observer");
 D3D11_SO_DECLARATION_ENTRY declarations[]={{0,"SV_Position",0,0,4,0},{0,"COLOR",0,0,4,0},{0,"TEXCOORD",0,0,3,0}};UINT outputStride=44;ComPtr<ID3D11GeometryShader> gs;require(SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(code->GetBufferPointer(),code->GetBufferSize(),declarations,3,&outputStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs)),"observe actual reticle GPU vertices");c->GSSetShader(gs.Get(),nullptr,0);
 auto so=buffer(176,D3D11_BIND_STREAM_OUTPUT,nullptr);D3D11_BUFFER_DESC stagingDesc{};stagingDesc.ByteWidth=176;stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> staging;require(SUCCEEDED(device->CreateBuffer(&stagingDesc,nullptr,&staging)),"readback output");
 D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=128;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_BC7_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Texture2D> atlas;require(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&atlas)),"128-square reticle atlas fixture");ComPtr<ID3D11ShaderResourceView> srv;require(SUCCEEDED(device->CreateShaderResourceView(atlas.Get(),nullptr,&srv)),"reticle atlas view");auto* image=srv.Get();c->PSSetShaderResources(0,1,&image);
 td.Width=td.Height=64;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target;require(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&target)),"final LDR target fixture");ComPtr<ID3D11RenderTargetView> rtv;require(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)),"target view");auto* rt=rtv.Get();c->OMSetRenderTargets(1,&rt,nullptr);D3D11_VIEWPORT viewport{0,0,64,64,0,1};c->RSSetViewports(1,&viewport);
 Transport::Pose reference{};reference.orientation.w=1;Transport::Tracking tracking{};tracking.valid=1;tracking.id=1;
 uint64_t frame{};
 for(float yaw:{0.f,.5f,-.5f,2.1f})for(bool reflected:{false,true}){
  tracking.head.orientation={0,std::sin(yaw/2),0,std::cos(yaw/2)};tracking.head.position={.12f,.04f,-.07f};
  auto native=CameraMath::Rotation({0,.3f,0,std::sqrt(1-.09f)});if(reflected)for(unsigned j=0;j<3;++j)native.m[j]=-native.m[j];native.m[12]=403;native.m[13]=19;native.m[14]=-192;
  const auto head=CameraMath::Multiply(CameraMath::Relative(reference,tracking.head,1.f),native);
  for(unsigned eye=0;eye<2;++eye){
   auto& e=tracking.eyes[eye];e.pose=tracking.head;auto offset=CameraMath::Rotate(tracking.head.orientation,{eye?.032f:-.032f,0,0});e.pose.position.x+=offset.x;e.pose.position.y+=offset.y;e.pose.position.z+=offset.z;
   const float cant=eye?.025f:-.025f;e.pose.orientation=CameraMath::Multiply(tracking.head.orientation,Transport::Quaternion{0,std::sin(cant/2),0,std::cos(cant/2)});e.left=-.84f;e.right=.93f;e.up=.83f;e.down=-.74f;
   c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);AimReticle::Vertex(vs.Get());AimReticle::Pixel(ps.Get());AimReticle::Begin(++frame,tracking,&native,&head,false);AimReticle::Eye(int(eye));AimReticle::BeforeDraw(c.Get(),4);
   ComPtr<ID3D11VertexShader> selected;c->VSGetShader(&selected,nullptr,nullptr);require(selected.Get()!=vs.Get(),"target reticle correction active in each eye");
   auto* outputBuffer=so.Get();c->SOSetTargets(1,&outputBuffer,&zero);c->Draw(4,0);ID3D11Buffer* empty{};c->SOSetTargets(1,&empty,&zero);AimReticle::AfterDraw(c.Get());AimReticle::End();
   c->VSGetShader(&selected,nullptr,nullptr);require(selected.Get()==vs.Get(),"native shader restored");ComPtr<ID3D11Buffer> restored;c->VSGetConstantBuffers(13,1,&restored);require(restored.Get()==sentinel.Get(),"native CB13 restored");c->VSGetConstantBuffers(0,1,restored.ReleaseAndGetAddressOf());require(restored.Get()==global.Get(),"native global untouched");
   c->CopyResource(staging.Get(),so.Get());D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"read GPU vertices");std::array<float,44> values{};memcpy(values.data(),mapped.pData,sizeof(values));c->Unmap(staging.Get(),0);
   const auto eyeWorld=CameraMath::Multiply(CameraMath::Relative(reference,e.pose,1.f),native);auto lens=CameraMath::Frustum(CameraMath::Matrix{},e);
   const float* g=reinterpret_cast<const float*>(globals.data());
   for(unsigned i=0;i<4;++i){auto p=transform({vertices[i].position[0],vertices[i].position[1],vertices[i].position[2],1},native);auto view=transform(p,CameraMath::InverseRigid(eyeWorld));auto expected=transform(view,lens);for(unsigned axis:{0u,1u,3u})require(std::abs(values[i*11+axis]-expected[axis])<.00015f,"GPU reticle agrees with independently projected native aim point");
    float nz=0,nw=0;for(unsigned j=0;j<4;++j){nz+=vertices[i].position[j]*g[51*4+j];nw+=vertices[i].position[j]*g[52*4+j];}require(std::abs(values[i*11+2]/values[i*11+3]-nz/nw)<1e-5f,"native overlay depth preserved");for(unsigned j=0;j<4;++j)require(values[i*11+4+j]==vertices[i].color[j],"native reticle color preserved");for(unsigned j=0;j<2;++j)require(values[i*11+8+j]==vertices[i].uv[j],"native reticle UV preserved");
   }
   for(auto count:{3u,6u}){AimReticle::Begin(++frame,tracking,&native,&head,false);AimReticle::Eye(int(eye));AimReticle::BeforeDraw(c.Get(),count);c->VSGetShader(&selected,nullptr,nullptr);require(selected.Get()==vs.Get(),"unrelated geometry count bypassed");AimReticle::End();}
   AimReticle::Begin(++frame,tracking,nullptr,nullptr,false);AimReticle::Eye(int(eye));AimReticle::BeforeDraw(c.Get(),4);c->VSGetShader(&selected,nullptr,nullptr);require(selected.Get()==vs.Get(),"unvalidated native camera bypassed");AimReticle::End();
  }
 }
 require(SUCCEEDED(device->GetDeviceRemovedReason()),"D3D device healthy");printf("PASS: %u reticle GPU checks: native aim projection, asymmetric/canted eyes, head turn/lean, reflected native basis, overlay depth, color/UV, restoration and guarded bypass.\n",checks);
}
