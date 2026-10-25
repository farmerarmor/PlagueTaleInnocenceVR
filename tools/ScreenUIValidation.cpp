#include "ScreenUI.h"
#include "CameraMath.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <fstream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using Microsoft::WRL::ComPtr;
static unsigned checks{};
static void require(bool v,const char* label){++checks;if(!v){fprintf(stderr,"FAIL: %s\n",label);exit(1);}}
static std::vector<char> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);std::vector<char> b{std::istreambuf_iterator<char>(f),{}};require(!b.empty(),"captured fixture exists");return b;}
static std::array<float,4> transform(std::array<float,4> p,const CameraMath::Matrix& m){std::array<float,4> r{};for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i)r[j]+=p[i]*m.m[i*4+j];return r;}
int wmain(int argc,wchar_t** argv){
 require(argc==3,"runtime fixtures and output supplied");std::filesystem::path root=argv[1],output=argv[2];std::filesystem::create_directories(output);ScreenUI::Initialize(output);
 WritePrivateProfileStringW(L"UI",L"CorrectScreenUI",L"1",(output/L"PlagueTaleVR.ini").c_str());
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)),"hardware D3D11");
 auto buffer=[&](UINT size,UINT flags,const void* data){D3D11_BUFFER_DESC b{};b.ByteWidth=size;b.BindFlags=flags;b.Usage=D3D11_USAGE_DEFAULT;D3D11_SUBRESOURCE_DATA initial{data,0,0};ComPtr<ID3D11Buffer> r;require(SUCCEEDED(d->CreateBuffer(&b,data?&initial:nullptr,&r)),"buffer");return r;};
 auto compile=[&](const std::string& source,const char* profile){ComPtr<ID3DBlob> code,error;auto hr=D3DCompile(source.data(),source.size(),"ui-validation",nullptr,nullptr,"main",profile,0,0,&code,&error);if(FAILED(hr)&&error)fprintf(stderr,"%s",static_cast<char*>(error->GetBufferPointer()));require(SUCCEEDED(hr),"compile observer");return code;};
 D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=64;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> target;require(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&target)),"LDR output");ComPtr<ID3D11RenderTargetView> rtv;require(SUCCEEDED(d->CreateRenderTargetView(target.Get(),nullptr,&rtv)),"LDR view");auto* rt=rtv.Get();c->OMSetRenderTargets(1,&rt,nullptr);D3D11_VIEWPORT viewport{0,0,64,64,0,1};c->RSSetViewports(1,&viewport);
 auto sentinel=buffer(64,D3D11_BIND_CONSTANT_BUFFER,nullptr);auto* sent=sentinel.Get();c->VSSetConstantBuffers(13,1,&sent);uint64_t frame{};
 struct Mesh {float p[4],t[4],n[3],uv[4],color[4];};
 const Mesh mesh[]={{{-.1f,-.1f,0,1},{.8f,.2f,.3f,1},{.1f,.9f,.2f},{.1f,.2f,.3f,.4f},{.6f,.7f,.2f,.8f}},{{.1f,-.1f,0,1},{.2f,.8f,.3f,-1},{.9f,.1f,.2f},{.8f,.2f,.3f,.4f},{.6f,.7f,.2f,.8f}},{{-.1f,.1f,0,1},{.8f,.2f,.3f,1},{.1f,.9f,.2f},{.1f,.7f,.3f,.4f},{.6f,.7f,.2f,.8f}},{{.1f,.1f,0,1},{.2f,.8f,.3f,-1},{.9f,.1f,.2f},{.8f,.7f,.3f,.4f},{.6f,.7f,.2f,.8f}}};
 struct Sprite {float p[4],color[4],uv[2];};
 const Sprite sprite[]={{{1200,1800,1,1},{.6f,.7f,.2f,.8f},{0,0}},{{2800,1800,1,1},{.6f,.7f,.2f,.8f},{1,0}},{{1200,2900,1,1},{.6f,.7f,.2f,.8f},{0,1}},{{2800,2900,1,1},{.6f,.7f,.2f,.8f},{1,1}}};
 for(unsigned variant=0;variant<3;++variant){
  const auto fixture=root/L"gpu-captures"/(variant?L"frame-32883":L"frame-28767");
  const wchar_t* vsNames[]={L"7c8aa06c1e5fc378",L"5132b28ca7564882",L"1fe6f6181d9219fb"};const wchar_t* psNames[]={L"1582f381218e03ae",L"14aec2e0c3461f0b",L"c7764018d343db9"};
  auto vsBytes=read(fixture/(std::wstring(L"shader-")+vsNames[variant]+L".dxbc"));auto psBytes=read(fixture/(std::wstring(L"shader-")+psNames[variant]+L".dxbc"));ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
  require(SUCCEEDED(d->CreateVertexShader(vsBytes.data(),vsBytes.size(),nullptr,&vs)),"captured native vertex");require(SUCCEEDED(d->CreatePixelShader(psBytes.data(),psBytes.size(),nullptr,&ps)),"captured native pixel");ScreenUI::RegisterShader(vs.Get(),vsBytes.data(),vsBytes.size());ScreenUI::RegisterShader(ps.Get(),psBytes.data(),psBytes.size());
  D3D11_INPUT_ELEMENT_DESC meshLayout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TANGENT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,44,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",8,DXGI_FORMAT_R32G32B32A32_FLOAT,0,60,D3D11_INPUT_PER_VERTEX_DATA,0}};
  D3D11_INPUT_ELEMENT_DESC spriteLayout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
  ComPtr<ID3D11InputLayout> layout;require(SUCCEEDED(d->CreateInputLayout(variant?meshLayout:spriteLayout,variant?5:3,vsBytes.data(),vsBytes.size(),&layout)),"native input layout");c->IASetInputLayout(layout.Get());
  auto vb=buffer(variant?sizeof(mesh):sizeof(sprite),D3D11_BIND_VERTEX_BUFFER,variant?static_cast<const void*>(mesh):sprite);auto* vertex=vb.Get();UINT stride=variant?sizeof(Mesh):sizeof(Sprite),zero=0;c->IASetVertexBuffers(0,1,&vertex,&stride,&zero);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  std::string fields="float4 p:SV_Position;";std::vector<D3D11_SO_DECLARATION_ENTRY> declarations;
  auto field=[&](const char* type,const char* name,const char* semantic,UINT index,BYTE count){fields+=std::string(type)+" "+name+":"+semantic+std::to_string(index)+";";declarations.push_back({0,semantic,index,0,count,0});};
  declarations.push_back({0,"SV_Position",0,0,4,0});
  if(variant)field("float4","previous","PREVIOUS_POSITION",0,4);
  field("float4","color","COLOR",0,4);field("float4","uv","TEXCOORD",0,4);
  if(variant==1){field("float4","t","TEXSPACE_MAT",0,4);field("float4","b","TEXSPACE_MAT",1,4);field("float4","n","TEXSPACE_MAT",2,4);}
  if(variant==2)field("float3","normal","TEXSPACE_NORMAL",0,3);
  field("float","instance","INSTANCE_COLOR",0,1);if(variant==2)field("float3","texPosition","TEXSPACE_POS",0,3);field("uint2","indexes","INSTANCE_INDEXES",0,2);
  auto gsCode=compile("struct V{"+fields+"};[maxvertexcount(1)]void main(point V input[1],inout PointStream<V> stream){stream.Append(input[0]);}","gs_5_0");
  UINT components=variant==1?31:variant==2?25:15,soStride=components*4;ComPtr<ID3D11GeometryShader> gs;require(SUCCEEDED(d->CreateGeometryShaderWithStreamOutput(gsCode->GetBufferPointer(),gsCode->GetBufferSize(),declarations.data(),UINT(declarations.size()),&soStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs)),"observe every native output");c->GSSetShader(gs.Get(),nullptr,0);
  auto so=buffer(soStride*4,D3D11_BIND_STREAM_OUTPUT,nullptr);D3D11_BUFFER_DESC sd{};sd.ByteWidth=soStride*4;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> staging;require(SUCCEEDED(d->CreateBuffer(&sd,nullptr,&staging)),"readback");
  for(unsigned eye=0;eye<2;++eye){
   unsigned sample=variant==0?(eye?301:179):variant==1?(eye?301:176):(eye?300:175);auto prefix=L"sample-"+std::to_wstring(sample)+L"-vs-cb";
   auto globalBytes=read(fixture/(prefix+L"0.bin")),objectBytes=read(fixture/(prefix+L"1.bin"));auto global=buffer(UINT(globalBytes.size()),D3D11_BIND_CONSTANT_BUFFER,globalBytes.data()),object=buffer(UINT(objectBytes.size()),D3D11_BIND_CONSTANT_BUFFER,objectBytes.data());ID3D11Buffer* native[]={global.Get(),object.Get()};c->VSSetConstantBuffers(0,2,native);
   for(float yaw:{0.f,.55f,-.55f})for(bool world:{false,true}){
    auto data=globalBytes;auto* values=reinterpret_cast<float*>(data.data());if(world)values[35*4]=321.f;c->UpdateSubresource(global.Get(),0,nullptr,data.data(),0,0);
    Transport::Tracking tracking{};tracking.valid=1;tracking.head.orientation={0,sinf(yaw/2),0,cosf(yaw/2)};tracking.head.position={.1f,.05f,-.07f};auto& e=tracking.eyes[eye];e.pose=tracking.head;auto offset=CameraMath::Rotate(tracking.head.orientation,{eye?.032f:-.032f,0,0});e.pose.position.x+=offset.x;e.pose.position.z+=offset.z;float cant=eye?.025f:-.025f;e.pose.orientation=CameraMath::Multiply(tracking.head.orientation,Transport::Quaternion{0,sinf(cant/2),0,cosf(cant/2)});e.left=-.84f;e.right=.93f;e.up=.83f;e.down=-.74f;
    auto run=[&](bool corrected,bool validViewport=true){
     c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);ScreenUI::Vertex(vs.Get());ScreenUI::Pixel(ps.Get());ScreenUI::Begin(++frame,tracking,false);ScreenUI::Eye(int(eye));auto view=viewport;if(!validViewport)view.Width=63;c->RSSetViewports(1,&view);if(corrected)ScreenUI::BeforeDraw(c.Get());ComPtr<ID3D11VertexShader> selected;c->VSGetShader(&selected,nullptr,nullptr);require((selected.Get()!=vs.Get())==(corrected&&validViewport),"exact UI draw selection");
     auto* stream=so.Get();c->SOSetTargets(1,&stream,&zero);c->Draw(4,0);ID3D11Buffer* empty{};c->SOSetTargets(1,&empty,&zero);ScreenUI::AfterDraw(c.Get());ScreenUI::End();c->VSGetShader(&selected,nullptr,nullptr);require(selected.Get()==vs.Get(),"native VS restored");ComPtr<ID3D11Buffer> restored;c->VSGetConstantBuffers(13,1,&restored);require(restored.Get()==sentinel.Get(),"native CB13 restored");ID3D11Buffer* unchanged[2]{};c->VSGetConstantBuffers(0,2,unchanged);for(unsigned i=0;i<2;++i){require(unchanged[i]==native[i],"native constants unchanged");if(unchanged[i])unchanged[i]->Release();}
     c->CopyResource(staging.Get(),so.Get());D3D11_MAPPED_SUBRESOURCE map{};require(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"read GPU vertices");std::vector<float> result(components*4);memcpy(result.data(),map.pData,result.size()*4);c->Unmap(staging.Get(),0);return result;
    };
    auto original=run(false),corrected=run(true),bypass=run(true,false);auto panel=CameraMath::ScreenPanelProjection(tracking.head,e,2.4f,2.f);
    for(unsigned i=0;i<4;++i){unsigned at=i*components;for(unsigned j=4;j<components;++j)require(std::abs(corrected[at+j]-original[at+j])<3e-5f,"all native material interpolants retained");for(unsigned j=0;j<components;++j)require(std::abs(bypass[at+j]-original[at+j])<3e-5f,"non-full viewport bypassed");
     if(world&&variant){for(unsigned j=0;j<4;++j)require(std::abs(corrected[at+j]-original[at+j])<3e-5f,"world mesh projection untouched");continue;}
     float x,y;if(!variant){x=(sprite[i].p[0]-values[66])/values[64];y=(sprite[i].p[1]-values[67])/values[65];}
     else {const auto* obj=reinterpret_cast<const float*>(objectBytes.data());std::array<float,4> p{0,0,0,mesh[i].p[3]};for(unsigned j=0;j<3;++j)for(unsigned k=0;k<4;++k)p[j]+=mesh[i].p[k]*obj[j*4+k];p[0]=-p[0];x=(p[0]*values[49*4]+p[1]*values[49*4+1])/original[at+3];y=(p[0]*values[50*4]+p[1]*values[50*4+1])/original[at+3];}
     auto expected=transform({x,y,0,1},panel);for(unsigned j:{0u,1u,3u})require(std::abs(corrected[at+j]-expected[j])<3e-5f,"both-eye UI panel positions");require(std::abs(corrected[at+2]/corrected[at+3]-original[at+2]/original[at+3])<3e-5f,"native layering depth preserved");
    }
   }
  }
 }
 require(SUCCEEDED(d->GetDeviceRemovedReason()),"device healthy");printf("PASS: %u UI GPU checks: marked HUD atlas, inventory lit mesh and silhouette; both captured eyes, asymmetric/canted lenses, head motion, all native interpolants, world/viewport bypass and state restoration.\n",checks);
}
