#include "FirstPersonHead.h"
#include "FirstPersonHeadShader.h"
#include "FirstPersonMouthMesh.h"
#include <wrl/client.h>
#include <d3d11shader.h>
#include <array>
#include <fstream>
#include <cstdio>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
static unsigned checks{};
static void require(bool v,const char* s){++checks;if(!v){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
static std::vector<char> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);std::vector<char> b{std::istreambuf_iterator<char>(f),{}};require(!b.empty(),"captured fixture");return b;}
int wmain(int argc,wchar_t** argv){
 require(argc==3,"fixture and output dirs");std::filesystem::path fixtures=argv[1],output=argv[2];std::filesystem::create_directories(output);FirstPersonHead::Initialize(output);ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;
 require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)),"hardware D3D11");
 auto buffer=[&](UINT size,const void* data){D3D11_BUFFER_DESC b{};b.ByteWidth=size;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA sd{data,0,0};ComPtr<ID3D11Buffer> r;require(SUCCEEDED(d->CreateBuffer(&b,data?&sd:nullptr,&r)),"constant buffer");return r;};
 auto compile=[&](const std::string& source){ComPtr<ID3DBlob> code,error;auto hr=D3DCompile(source.data(),source.size(),"head-test",nullptr,nullptr,"main","vs_5_0",0,0,&code,&error);if(FAILED(hr)&&error)fprintf(stderr,"%s",static_cast<char*>(error->GetBufferPointer()));require(SUCCEEDED(hr),"native-compatible geometry fixture");return code;};
 D3D11_SAMPLER_DESC samplerDesc{};samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;require(SUCCEEDED(d->CreateSamplerState(&samplerDesc,&sampler)),"sampler");auto* ss=sampler.Get();c->PSSetSamplers(0,1,&ss);
 D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=true;ComPtr<ID3D11RasterizerState> rs;require(SUCCEEDED(d->CreateRasterizerState(&raster,&rs)),"rasterizer");c->RSSetState(rs.Get());D3D11_VIEWPORT viewport{0,0,32,32,0,1};c->RSSetViewports(1,&viewport);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 auto sentinel=buffer(16,nullptr);auto* sentinelPtr=sentinel.Get();c->PSSetConstantBuffers(13,1,&sentinelPtr);uint64_t frame{};
 for(const wchar_t* name:{L"hair",L"skin",L"eyes",L"hands",L"teeth",L"mouth",L"eyelashes",L"glints-local-light",L"glints-sun",L"glints-npc-preservation",L"unmarked"})for(unsigned eye=0;eye<2;++eye){
  const bool candidate=std::wstring(name)!=L"unmarked" && std::wstring(name)!=L"hands";const auto fixture=fixtures/(std::wstring(name)+L"-eye"+std::to_wstring(eye));auto bytes=read(fixture/L"shader.dxbc");ComPtr<ID3DBlob> modified;std::string error;auto hr=FirstPersonHeadShader::Build(bytes.data(),bytes.size(),&modified,error);if(FAILED(hr))fprintf(stderr,"%s\n",error.c_str());require(SUCCEEDED(hr),"build actual captured material mask");
  FirstPersonHeadShader::Program nativeProgram,modifiedProgram;require(SUCCEEDED(FirstPersonHeadShader::Parse(bytes.data(),bytes.size(),nativeProgram)) && SUCCEEDED(FirstPersonHeadShader::Parse(modified->GetBufferPointer(),modified->GetBufferSize(),modifiedProgram)),"parse native and masked program");
  std::vector<std::vector<uint32_t>> original,changed;for(size_t i=0;i<nativeProgram.lines.size();++i)if(nativeProgram.lines[i].rfind("dcl_",0)!=0)original.push_back(nativeProgram.instructions[i]);for(size_t i=0;i<modifiedProgram.lines.size();++i)if(modifiedProgram.lines[i].rfind("dcl_",0)!=0)changed.push_back(modifiedProgram.instructions[i]);require(changed.size()>original.size(),"predicate inserted");for(size_t i=0;i<original.size();++i)require(original[i]==changed[changed.size()-original.size()+i],"every native material instruction remains byte-identical");
  ComPtr<ID3D11PixelShader> ps;require(SUCCEEDED(d->CreatePixelShader(bytes.data(),bytes.size(),nullptr,&ps)),"create captured native material");ComPtr<ID3D11PixelShader> direct;require(SUCCEEDED(d->CreatePixelShader(modified->GetBufferPointer(),modified->GetBufferSize(),nullptr,&direct)),"device accepts masked material");FirstPersonHead::RegisterShader(ps.Get(),bytes.data(),bytes.size());
  ComPtr<ID3D11ShaderReflection> reflection;require(SUCCEEDED(D3DReflect(bytes.data(),bytes.size(),IID_PPV_ARGS(&reflection))),"native material reflection");D3D11_SHADER_DESC desc{};reflection->GetDesc(&desc);
  std::string fields="float4 p:SV_Position;",assign="o.p=float4(id==2?3:-1,id==1?3:-1,.5,1);";
  for(unsigned i=0;i<desc.InputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC p{};reflection->GetInputParameterDesc(i,&p);if(p.SystemValueType!=D3D_NAME_UNDEFINED)continue;unsigned components=0;for(unsigned j=0;j<4;++j)if(p.Mask&(1<<j))components=j+1;std::string type=p.ComponentType==D3D_REGISTER_COMPONENT_UINT32?"uint":"float",field="v"+std::to_string(i);if(components>1)type+=std::to_string(components);fields+=(p.ComponentType==D3D_REGISTER_COMPONENT_UINT32?"nointerpolation ":"")+type+" "+field+":"+p.SemanticName+std::to_string(p.SemanticIndex)+";";
   std::string value=components==1?"1":components==2?type+"(.5,.5)":components==3?type+"(0,0,1)":type+"(.5,.5,1,1)";
   if(!strcmp(p.SemanticName,"TEXSPACE_MAT"))value=type+(p.SemanticIndex==0?"(1,0,0,1)":p.SemanticIndex==1?"(0,1,0,2)":"(0,0,1,3)");assign+="o."+field+"="+value+";";
  }
  auto vsCode=compile("struct V{"+fields+"};V main(uint id:SV_VertexID){V o=(V)0;"+assign+"return o;}");ComPtr<ID3D11VertexShader> vs;require(SUCCEEDED(d->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs)),"geometry shader");c->VSSetShader(vs.Get(),nullptr,0);
  std::vector<ComPtr<ID3D11Texture2D>> resources;std::vector<ComPtr<ID3D11ShaderResourceView>> views;
  for(unsigned i=0;i<desc.BoundResources;++i){D3D11_SHADER_INPUT_BIND_DESC b{};reflection->GetResourceBindingDesc(i,&b);if(b.Type!=D3D_SIT_TEXTURE)continue;
   D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=2;td.MipLevels=1;td.ArraySize=b.Dimension==D3D_SRV_DIMENSION_TEXTURECUBE?6:b.Dimension==D3D_SRV_DIMENSION_TEXTURE2DARRAY?64:1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;if(b.Dimension==D3D_SRV_DIMENSION_TEXTURECUBE)td.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
   std::array<float,16> white;white.fill(1.f);std::vector<D3D11_SUBRESOURCE_DATA> initial(td.ArraySize,{white.data(),32,0});ComPtr<ID3D11Texture2D> texture;require(SUCCEEDED(d->CreateTexture2D(&td,initial.data(),&texture)),"material texture fixture");ComPtr<ID3D11ShaderResourceView> view;require(SUCCEEDED(d->CreateShaderResourceView(texture.Get(),nullptr,&view)),"material view");auto* srv=view.Get();c->PSSetShaderResources(b.BindPoint,1,&srv);resources.push_back(texture);views.push_back(view);
  }
  std::array<ComPtr<ID3D11Buffer>,14> constants;std::array<std::vector<char>,14> constantBytes;
  for(unsigned i=0;i<14;++i){auto path=fixture/(L"cb"+std::to_wstring(i)+L".bin");if(i==13 || !std::filesystem::exists(path))continue;auto data=read(path);auto value=buffer(UINT(data.size()),data.data());auto* cb=value.Get();c->PSSetConstantBuffers(i,1,&cb);constants[i]=value;constantBytes[i]=data;}
  UINT objectSlot{};require(SUCCEEDED(FirstPersonHeadShader::ObjectSlot(bytes.data(),bytes.size(),objectSlot)),"native object register reflected");const auto* global=reinterpret_cast<const float*>(constantBytes[0].data());const auto* object=reinterpret_cast<const float*>(constantBytes[objectSlot].data());CameraMath::Matrix player=CameraMath::Identity(),camera=CameraMath::Identity();
  for(unsigned i=0;i<3;++i){player.m[12+i]=object[i*4+3]+global[140+i];camera.m[12+i]=global[140+i];for(unsigned j=0;j<3;++j){player.m[j*4+i]=object[i*4+j];camera.m[i*4+j]=global[(31+i)*4+j];}}
  UINT meshCount{},meshStart{};INT meshBase{};bool meshIndexed=false;ComPtr<ID3D11Buffer> meshVertices,meshIndices;
  if(std::filesystem::exists(fixture/L"mesh-vertex.bin")){
   auto vertices=read(fixture/L"mesh-vertex.bin"),indices=read(fixture/L"mesh-index.bin"),args=read(fixture/L"draw.bin");require(args.size()==12,"captured mesh draw args");memcpy(&meshCount,args.data(),4);memcpy(&meshStart,args.data()+4,4);memcpy(&meshBase,args.data()+8,4);meshIndexed=true;
   auto geometryBuffer=[&](const std::vector<char>& data,UINT flags){D3D11_BUFFER_DESC desc{};desc.ByteWidth=UINT(data.size());desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=flags;D3D11_SUBRESOURCE_DATA initial{data.data(),0,0};ComPtr<ID3D11Buffer> value;require(SUCCEEDED(d->CreateBuffer(&desc,&initial,&value)),"captured native mesh buffer");return value;};
   meshVertices=geometryBuffer(vertices,D3D11_BIND_VERTEX_BUFFER);meshIndices=geometryBuffer(indices,D3D11_BIND_INDEX_BUFFER);auto* vertex=meshVertices.Get();UINT stride=48,offset=0;c->IASetVertexBuffers(0,1,&vertex,&stride,&offset);c->IASetIndexBuffer(meshIndices.Get(),DXGI_FORMAT_R16_UINT,0);
   const bool oral=std::wstring(name)==L"teeth" || std::wstring(name)==L"mouth";
   bool matched=FirstPersonMouthMesh::Match(c.Get(),++frame,meshCount,meshStart,meshBase,true);c->Flush();
   for(unsigned attempt=0;oral && !matched && attempt<500;++attempt){Sleep(1);matched=FirstPersonMouthMesh::Match(c.Get(),++frame,meshCount,meshStart,meshBase,true);}
   require(matched==oral,"captured teeth/mouth identities accepted; shared-shader hands rejected");
   if(std::wstring(name)==L"teeth" && eye==0){
    require(!FirstPersonMouthMesh::Match(c.Get(),++frame,meshCount,meshStart,meshBase,false),"non-indexed command never inherits oral identity");
    require(!FirstPersonMouthMesh::Match(c.Get(),++frame,meshCount,meshStart,1,true),"different vertex base rejected");
    require(!FirstPersonMouthMesh::Match(c.Get(),++frame,3,5922,0,true),"out-of-buffer draw range rejected");
    require(!FirstPersonMouthMesh::Match(c.Get(),++frame,3,1,0,true),"non-triangle-aligned range rejected");
    UINT wrongStride=44;c->IASetVertexBuffers(0,1,&vertex,&wrongStride,&offset);require(!FirstPersonMouthMesh::Match(c.Get(),++frame,meshCount,meshStart,0,true),"different vertex layout stride rejected");c->IASetVertexBuffers(0,1,&vertex,&stride,&offset);
    for(unsigned altered=0;altered<2;++altered){
     auto corrupt=altered?indices:vertices;corrupt.back()^=1;auto changed=geometryBuffer(corrupt,altered?D3D11_BIND_INDEX_BUFFER:D3D11_BIND_VERTEX_BUFFER);
     if(altered)c->IASetIndexBuffer(changed.Get(),DXGI_FORMAT_R16_UINT,0);else{auto* pointer=changed.Get();c->IASetVertexBuffers(0,1,&pointer,&stride,&offset);}
     require(!FirstPersonMouthMesh::Match(c.Get(),++frame,meshCount,meshStart,0,true),"unverified same-size resource left native");c->Flush();bool falseMatch=false,done=false;
     const auto rejected="\"rejected\":"+std::to_string(altered+1);
     for(unsigned attempt=0;attempt<500 && !done;++attempt){Sleep(1);falseMatch|=FirstPersonMouthMesh::Match(c.Get(),++frame,meshCount,meshStart,0,true);done=FirstPersonMouthMesh::Status().find(rejected)!=std::string::npos;}
     require(!falseMatch && done,"same-size changed vertex/index contents rejected after asynchronous identity check");c->IASetVertexBuffers(0,1,&vertex,&stride,&offset);c->IASetIndexBuffer(meshIndices.Get(),DXGI_FORMAT_R16_UINT,0);
    }
    require(FirstPersonMouthMesh::Match(c.Get(),++frame,meshCount,meshStart,0,true),"original mouth identity retained after other resource rejection");
   }
  }
  std::vector<ComPtr<ID3D11Texture2D>> targets,staging;std::vector<ComPtr<ID3D11RenderTargetView>> targetViews;std::array<ID3D11RenderTargetView*,8> renderTargets{};
  for(unsigned i=0;i<desc.OutputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC p{};reflection->GetOutputParameterDesc(i,&p);if(p.SystemValueType!=D3D_NAME_TARGET || p.SemanticIndex>=8)continue;D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=32;td.ArraySize=td.MipLevels=1;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;td.Format=p.ComponentType==D3D_REGISTER_COMPONENT_UINT32?DXGI_FORMAT_R32G32B32A32_UINT:DXGI_FORMAT_R32G32B32A32_FLOAT;
   ComPtr<ID3D11Texture2D> texture,readback;require(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&texture)),"native MRT fixture");ComPtr<ID3D11RenderTargetView> view;require(SUCCEEDED(d->CreateRenderTargetView(texture.Get(),nullptr,&view)),"native MRT view");renderTargets[p.SemanticIndex]=view.Get();td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;require(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&readback)),"MRT readback");targets.push_back(texture);staging.push_back(readback);targetViews.push_back(view);
  }
  require(!targets.empty(),"material outputs captured");c->OMSetRenderTargets(8,renderTargets.data(),nullptr);
  auto run=[&](bool mask,const CameraMath::Matrix& owner,const CameraMath::Matrix& view,bool firstPerson=true){
   const float clear[4]={37,41,43,47};for(auto& rt:targetViews)c->ClearRenderTargetView(rt.Get(),clear);c->PSSetShader(ps.Get(),nullptr,0);FirstPersonHead::Pixel(ps.Get());FirstPersonHead::Begin(++frame,firstPerson,owner,view,false);FirstPersonHead::Eye(int(eye));if(mask)FirstPersonHead::BeforeDraw(c.Get(),meshCount,meshStart,meshBase,meshIndexed);ComPtr<ID3D11PixelShader> selected;c->PSGetShader(&selected,nullptr,nullptr);require((selected.Get()!=ps.Get())==(mask&&firstPerson&&candidate),"first-person-only native material selection");
   D3D11_QUERY_DESC qd{D3D11_QUERY_OCCLUSION,0};ComPtr<ID3D11Query> query;require(SUCCEEDED(d->CreateQuery(&qd,&query)),"fragment visibility query");c->Begin(query.Get());c->Draw(3,0);c->End(query.Get());FirstPersonHead::AfterDraw(c.Get());FirstPersonHead::End();c->PSGetShader(&selected,nullptr,nullptr);require(selected.Get()==ps.Get(),"native material restored");ComPtr<ID3D11Buffer> restored;c->PSGetConstantBuffers(13,1,&restored);require(restored.Get()==sentinel.Get(),"native PS b13 restored");
   for(unsigned i=0;i<14;++i){if(!constants[i])continue;ComPtr<ID3D11Buffer> current;c->PSGetConstantBuffers(i,1,&current);require(current.Get()==constants[i].Get(),"native material constants unchanged");}
   c->Flush();uint64_t pixels{};while(c->GetData(query.Get(),&pixels,sizeof(pixels),0)==S_FALSE)Sleep(1);
   std::vector<char> result;for(unsigned i=0;i<targets.size();++i){c->CopyResource(staging[i].Get(),targets[i].Get());D3D11_MAPPED_SUBRESOURCE map{};require(SUCCEEDED(c->Map(staging[i].Get(),0,D3D11_MAP_READ,0,&map)),"read material outputs");for(unsigned y=0;y<32;++y){auto* row=static_cast<char*>(map.pData)+y*map.RowPitch;result.insert(result.end(),row,row+32*16);}c->Unmap(staging[i].Get(),0);}return std::make_pair(pixels,result);
  };
  const auto native=run(false,player,camera);require(native.first>0,"native marked material draws visible fragments");const auto hidden=run(true,player,camera);require(hidden.first==(candidate?0:native.first),"only local first-person head materials discarded; hands and unmarked materials retained");if(!candidate)require(hidden.second==native.second,"local hand/unmarked native MRT outputs byte-identical with head filter active");auto npc=player;npc.m[12]+=.2f;auto otherView=camera;otherView.m[12]+=.2f;auto shadow=CameraMath::Multiply(CameraMath::Rotation({0,sinf(.3f),0,cosf(.3f)}),camera);auto rotatedNpc=player;for(unsigned i=0;i<3;++i)rotatedNpc.m[i]=-rotatedNpc.m[i];
  for(const auto& test:{run(true,npc,camera),run(true,rotatedNpc,camera),run(true,player,otherView),run(true,player,shadow),run(true,player,camera,false)}){require(test.first==native.first,"NPC/shadow/third-person coverage unchanged");require(test.second==native.second,"NPC/shadow/third-person native MRT outputs byte-identical");}
  if(std::filesystem::exists(fixture/L"actual-player.bin")){auto data=read(fixture/L"actual-player.bin");require(data.size()==64,"captured actual player matrix");auto actualPlayer=CameraMath::Load(data.data());auto npcBytes=read(fixture/L"npc-object.bin");c->UpdateSubresource(constants[objectSlot].Get(),0,nullptr,npcBytes.data(),0,0);auto nativeNpc=run(false,actualPlayer,camera);auto realOwner=run(true,actualPlayer,camera);require(realOwner.first==nativeNpc.first && realOwner.second==nativeNpc.second,"captured scaled NPC eye draw retained under actual Amicia ownership filter");c->UpdateSubresource(constants[objectSlot].Get(),0,nullptr,constantBytes[objectSlot].data(),0,0);}
  wprintf(L"Validated %ls eye %u: native %llu fragments, hidden %llu.\n",name,eye,native.first,hidden.first);
 }
 require(SUCCEEDED(d->GetDeviceRemovedReason()),"device healthy");printf("PASS: %u head-mask GPU checks: captured oral meshes hidden and shared-shader hands preserved/both eyes, unchanged native instructions/MRTs for hands, other owners and cameras, local head discard, third-person bypass and binding restoration.\n",checks);
}
