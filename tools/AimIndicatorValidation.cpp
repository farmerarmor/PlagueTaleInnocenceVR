#include "AimReticle.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <array>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
using Microsoft::WRL::ComPtr;
static unsigned checks{};
static void require(bool value,const char* label){++checks;if(!value){fprintf(stderr,"FAIL: %s\n",label);exit(1);}}
static std::vector<char> read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);std::vector<char> bytes{std::istreambuf_iterator<char>(f),{}};require(!bytes.empty(),"captured fixture available");return bytes;}
static std::array<float,4> transform(std::array<float,4> p,const CameraMath::Matrix& m){std::array<float,4> out{};for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i)out[j]+=p[i]*m.m[4*i+j];return out;}
int wmain(int argc,wchar_t** argv){
 require(argc==3,"fixture and output directories supplied");std::filesystem::path fixture=argv[1],output=argv[2];std::filesystem::create_directories(output);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> c;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&c)),"hardware device");
 AimReticle::Initialize(output);WritePrivateProfileStringW(L"UI",L"CorrectAimReticle",L"1",(output/L"PlagueTaleVR.ini").c_str());
 auto buffer=[&](UINT size,UINT flags,const void* data){D3D11_BUFFER_DESC desc{};desc.ByteWidth=size;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=flags;D3D11_SUBRESOURCE_DATA initial{data,0,0};ComPtr<ID3D11Buffer> result;require(SUCCEEDED(device->CreateBuffer(&desc,data?&initial:nullptr,&result)),"GPU buffer");return result;};
 auto psBytes=read(fixture/L"shader-4ad560d89db2c231.dxbc");ComPtr<ID3D11PixelShader> ps;require(SUCCEEDED(device->CreatePixelShader(psBytes.data(),psBytes.size(),nullptr,&ps)),"captured indicator material");AimReticle::RegisterShader(ps.Get(),psBytes.data(),psBytes.size());
 struct Vertex {float position[4],tangent[4],normal[3],uv[4],color[4];};
 const Vertex vertices[]={{{-.5f,-.6f,0,1},{1,0,0,1},{.2f,.4f,.6f},{.1f,.2f,-.04f,.08f},{.9f,.7f,.2f,.8f}},{{.5f,-.6f,0,1},{1,0,0,1},{.2f,.4f,.6f},{.8f,.2f,.03f,.08f},{.9f,.7f,.2f,.8f}},{{-.5f,.6f,0,1},{1,0,0,1},{.2f,.4f,.6f},{.1f,.7f,-.04f,-.05f},{.9f,.7f,.2f,.8f}},{{.5f,.6f,0,1},{1,0,0,1},{.2f,.4f,.6f},{.8f,.7f,.03f,-.05f},{.9f,.7f,.2f,.8f}}};
 auto vertexBuffer=buffer(sizeof(vertices),D3D11_BIND_VERTEX_BUFFER,vertices);auto* vb=vertexBuffer.Get();UINT stride=sizeof(Vertex),zero=0;c->IASetVertexBuffers(0,1,&vb,&stride,&zero);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
 const char* gsText="struct V{float4 p:SV_Position;float4 previous:PREVIOUS_POSITION;float4 color:COLOR;float4 uv:TEXCOORD0;float3 normal:TEXSPACE_NORMAL;float instance:INSTANCE_COLOR;float3 texPosition:TEXSPACE_POS;uint2 indexes:INSTANCE_INDEXES;};[maxvertexcount(1)]void main(point V input[1],inout PointStream<V> stream){stream.Append(input[0]);}";
 ComPtr<ID3DBlob> code,errors;require(SUCCEEDED(D3DCompile(gsText,strlen(gsText),"indicator-test",nullptr,nullptr,"main","gs_5_0",0,0,&code,&errors)),"compile GPU observer");
 D3D11_SO_DECLARATION_ENTRY declarations[]={{0,"SV_Position",0,0,4,0},{0,"PREVIOUS_POSITION",0,0,4,0},{0,"COLOR",0,0,4,0},{0,"TEXCOORD",0,0,4,0},{0,"TEXSPACE_NORMAL",0,0,3,0},{0,"INSTANCE_COLOR",0,0,1,0},{0,"TEXSPACE_POS",0,0,3,0},{0,"INSTANCE_INDEXES",0,0,2,0}};UINT soStride=100;ComPtr<ID3D11GeometryShader> gs;require(SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(code->GetBufferPointer(),code->GetBufferSize(),declarations,8,&soStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&gs)),"observe every native interpolant");c->GSSetShader(gs.Get(),nullptr,0);
 auto so=buffer(400,D3D11_BIND_STREAM_OUTPUT,nullptr),sentinel=buffer(64,D3D11_BIND_CONSTANT_BUFFER,nullptr);auto* sent=sentinel.Get();c->VSSetConstantBuffers(13,1,&sent);D3D11_BUFFER_DESC sd{};sd.ByteWidth=400;sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> staging;require(SUCCEEDED(device->CreateBuffer(&sd,nullptr,&staging)),"observer readback");
 D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=128;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_BC7_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;ComPtr<ID3D11Texture2D> atlas;require(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&atlas)),"captured-size atlas");ComPtr<ID3D11ShaderResourceView> srv;require(SUCCEEDED(device->CreateShaderResourceView(atlas.Get(),nullptr,&srv)),"atlas view");auto* image=srv.Get();c->PSSetShaderResources(0,1,&image);
 td.Width=td.Height=64;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target;require(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&target)),"LDR output fixture");ComPtr<ID3D11RenderTargetView> rtv;require(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)),"LDR view");auto* rt=rtv.Get();c->OMSetRenderTargets(1,&rt,nullptr);D3D11_VIEWPORT viewport{0,0,64,64,0,1};c->RSSetViewports(1,&viewport);
 uint64_t frame{};Transport::Pose reference{};reference.orientation.w=1;
 for(unsigned variant=0;variant<2;++variant){
  auto vsBytes=read(fixture/(variant?L"shader-2a61d83c081dd26b.dxbc":L"shader-1fe6f6181d9219fb.dxbc"));ComPtr<ID3D11VertexShader> vs;require(SUCCEEDED(device->CreateVertexShader(vsBytes.data(),vsBytes.size(),nullptr,&vs)),"captured indicator vertex variant");AimReticle::RegisterShader(vs.Get(),vsBytes.data(),vsBytes.size());
  D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TANGENT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,44,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",8,DXGI_FORMAT_R32G32B32A32_FLOAT,0,60,D3D11_INPUT_PER_VERTEX_DATA,0}};ComPtr<ID3D11InputLayout> layout;require(SUCCEEDED(device->CreateInputLayout(elements,5,vsBytes.data(),vsBytes.size(),&layout)),"native mesh input signature");c->IASetInputLayout(layout.Get());
  for(unsigned eye=0;eye<2;++eye){
   const unsigned sample=(eye?235:129)+variant;auto prefix=L"sample-"+std::to_wstring(sample)+L"-vs-cb";
   auto g=read(fixture/(prefix+L"0.bin")),a=read(fixture/(prefix+L"1.bin"));std::vector<char> b;if(variant)b=read(fixture/(prefix+L"2.bin"));require(g.size()==1440 && a.size()==(variant?48u:512u) && (!variant || b.size()==512),"exact captured constant layouts");
   auto global=buffer(UINT(g.size()),D3D11_BIND_CONSTANT_BUFFER,g.data()),first=buffer(UINT(a.size()),D3D11_BIND_CONSTANT_BUFFER,a.data());ComPtr<ID3D11Buffer> second;if(variant)second=buffer(UINT(b.size()),D3D11_BIND_CONSTANT_BUFFER,b.data());ID3D11Buffer* nativeBuffers[]={global.Get(),first.Get(),second.Get()};c->VSSetConstantBuffers(0,3,nativeBuffers);
   for(float yaw:{0.f,.5f,-.5f,2.1f})for(bool worldMaterial:{false,true}){
    auto localGlobal=g;if(worldMaterial){auto* values=reinterpret_cast<float*>(localGlobal.data());values[35*4]=321;values[31*4]=.91f;}c->UpdateSubresource(global.Get(),0,nullptr,localGlobal.data(),0,0);
    Transport::Tracking tracking{};tracking.valid=1;tracking.id=frame+1;tracking.head.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};tracking.head.position={.1f,.04f,-.07f};auto& e=tracking.eyes[eye];e.pose=tracking.head;auto offset=CameraMath::Rotate(tracking.head.orientation,{eye?.032f:-.032f,0,0});e.pose.position.x+=offset.x;e.pose.position.y+=offset.y;e.pose.position.z+=offset.z;const float cant=eye?.025f:-.025f;e.pose.orientation=CameraMath::Multiply(tracking.head.orientation,Transport::Quaternion{0,std::sin(cant*.5f),0,std::cos(cant*.5f)});e.left=-.84f;e.right=.93f;e.up=.83f;e.down=-.74f;
    auto nativeWorld=CameraMath::Identity();nativeWorld.m[0]=-1;nativeWorld.m[12]=403;nativeWorld.m[13]=19;nativeWorld.m[14]=-192;auto headWorld=CameraMath::Multiply(CameraMath::Relative(reference,tracking.head,1),nativeWorld);
    auto run=[&](bool corrected){
     c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);AimReticle::Vertex(vs.Get());AimReticle::Pixel(ps.Get());AimReticle::Begin(++frame,tracking,&nativeWorld,&headWorld,false);AimReticle::Eye(int(eye));if(corrected)AimReticle::BeforeDraw(c.Get(),24,true);ComPtr<ID3D11VertexShader> selected;c->VSGetShader(&selected,nullptr,nullptr);require((selected.Get()!=vs.Get())==corrected,"replacement selection");
     auto* stream=so.Get();c->SOSetTargets(1,&stream,&zero);c->Draw(4,0);ID3D11Buffer* empty{};c->SOSetTargets(1,&empty,&zero);AimReticle::AfterDraw(c.Get());AimReticle::End();c->VSGetShader(&selected,nullptr,nullptr);require(selected.Get()==vs.Get(),"native vertex restored");ComPtr<ID3D11Buffer> restored;c->VSGetConstantBuffers(13,1,&restored);require(restored.Get()==sentinel.Get(),"native CB13 restored");ID3D11Buffer* buffers[3]{};c->VSGetConstantBuffers(0,3,buffers);for(unsigned i=0;i<3;++i){require(buffers[i]==nativeBuffers[i],"object/light/global bindings unchanged");if(buffers[i])buffers[i]->Release();}
     c->CopyResource(staging.Get(),so.Get());D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"read observed vertices");std::array<float,100> result{};memcpy(result.data(),mapped.pData,sizeof(result));c->Unmap(staging.Get(),0);return result;
    };
    const auto original=run(false),corrected=run(true);const auto* values=reinterpret_cast<const float*>(localGlobal.data());CameraMath::Matrix nativeProjection;for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)nativeProjection.m[i*4+j]=values[(49+j)*4+i];CameraMath::Matrix inverseProjection;require(CameraMath::Inverse(nativeProjection,inverseProjection),"invert captured native stereo projection");const auto eyeWorld=CameraMath::Multiply(CameraMath::Relative(reference,e.pose,1),nativeWorld);const auto lens=CameraMath::Frustum(CameraMath::Matrix{},e);
    for(unsigned vertex=0;vertex<4;++vertex){const unsigned at=vertex*25;for(unsigned i=4;i<25;++i)require(std::abs(original[at+i]-corrected[at+i])<2e-5f,"all native material interpolants preserved");
     if(worldMaterial){for(unsigned i=0;i<4;++i)require(std::abs(original[at+i]-corrected[at+i])<2e-5f,"world material projection bypassed");}
     else {auto p=transform({original[at],original[at+1],original[at+2],original[at+3]},inverseProjection);p=transform(p,nativeWorld);p=transform(p,CameraMath::InverseRigid(eyeWorld));auto expected=transform(p,lens);for(unsigned i:{0u,1u,3u})require(std::abs(expected[i]-corrected[at+i])<.0003f,"corrected mesh follows independently reconstructed native aim point");require(std::abs(original[at+2]/original[at+3]-corrected[at+2]/corrected[at+3])<2e-5f,"native overlay depth retained");}
    }
   }
  }
 }
 require(SUCCEEDED(device->GetDeviceRemovedReason()),"device healthy");printf("PASS: %u indicator GPU checks: both captured mesh variants/eye constants, native interpolants, aim-camera projection, head turn/lean, world-camera bypass and state restoration.\n",checks);
}
