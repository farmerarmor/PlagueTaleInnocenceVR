#include "DlssImageAudit.h"
#include "DlssComputeState.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>
using Microsoft::WRL::ComPtr;
namespace DlssImageAudit {namespace {
constexpr unsigned side=64,pixels=side*side,bytes=pixels*3*16;
const char* shader=R"hlsl(
Texture2D<float4> source:register(t0);RWStructuredBuffer<float4> samples:register(u0);
cbuffer Size:register(b0){uint2 dimensions;uint field;uint padding;};
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){
 uint2 at=min(((id.xy*2+1)*dimensions)/128,dimensions-1);
 samples[field*4096+id.y*64+id.x]=source.Load(int3(at,0));
})hlsl";
std::filesystem::path directory;
ComPtr<ID3D11ComputeShader> program;ComPtr<ID3D11Buffer> constants;
struct Pending {ComPtr<ID3D11Buffer> gpu,staging;ComPtr<ID3D11UnorderedAccessView> uav;ComPtr<ID3D11Query> complete;uint64_t frame{};D3D11_TEXTURE2D_DESC desc[3]{};uintptr_t resources[3]{};D3D11_VIEWPORT viewport{};float scales[4]{};int aa{};};
Pending pending[2];std::atomic<bool> queued[2]{};
bool initialize(ID3D11Device* device){
 if(program && constants)return true;program.Reset();constants.Reset();ComPtr<ID3DBlob> code;if(FAILED(D3DCompile(shader,strlen(shader),"DLSS-image-audit",nullptr,nullptr,"main","cs_5_0",0,0,&code,nullptr)) || FAILED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&program)))return false;
 D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;return SUCCEEDED(device->CreateBuffer(&cb,nullptr,&constants));
}
bool allocate(ID3D11Device* device,Pending& p){
 if(p.gpu && p.uav && p.staging && p.complete)return true;p={};D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;d.StructureByteStride=16;
 if(FAILED(device->CreateBuffer(&d,nullptr,&p.gpu)))return false;
 D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=pixels*3;if(FAILED(device->CreateUnorderedAccessView(p.gpu.Get(),&view,&p.uav)))return false;
 d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.BindFlags=d.MiscFlags=d.StructureByteStride=0;
 D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};return SUCCEEDED(device->CreateBuffer(&d,nullptr,&p.staging)) && SUCCEEDED(device->CreateQuery(&query,&p.complete));
}
}
void Initialize(const std::filesystem::path& path){directory=path;}
void Capture(ID3D11DeviceContext1* c,uint64_t frame,int eye,ID3D11Texture2D* input,ID3D11Texture2D* dlss,ID3D11Texture2D* output,const float scales[4],int aa){
 if(eye<0 || eye>1 || queued[eye].load(std::memory_order_acquire) || !input || !dlss || !output)return;
 ComPtr<ID3D11Device> device;c->GetDevice(&device);auto& p=pending[eye];if(!initialize(device.Get()) || !allocate(device.Get(),p))return;
 ID3D11Texture2D* textures[]={input,dlss,output};ComPtr<ID3D11ShaderResourceView> views[3];
 for(unsigned i=0;i<3;++i){textures[i]->GetDesc(&p.desc[i]);p.resources[i]=reinterpret_cast<uintptr_t>(textures[i]);if(FAILED(device->CreateShaderResourceView(textures[i],nullptr,&views[i])))return;}
 UINT count=1;c->RSGetViewports(&count,&p.viewport);p.frame=frame;p.aa=aa;memcpy(p.scales,scales,sizeof(p.scales));
 {DlssComputeState state(c);auto* cb=constants.Get();auto* uav=p.uav.Get();c->CSSetConstantBuffers(0,1,&cb);c->CSSetUnorderedAccessViews(0,1,&uav,nullptr);c->CSSetShader(program.Get(),nullptr,0);
  for(unsigned i=0;i<3;++i){UINT size[4]={p.desc[i].Width,p.desc[i].Height,i,0};c->UpdateSubresource(constants.Get(),0,nullptr,size,0,0);auto* srv=views[i].Get();c->CSSetShaderResources(0,1,&srv);c->Dispatch(8,8,1);}
 }
 c->CopyResource(p.staging.Get(),p.gpu.Get());c->End(p.complete.Get());queued[eye].store(true,std::memory_order_release);
}
void Flush(ID3D11DeviceContext1* c){
 for(unsigned eye=0;eye<2;++eye){if(!queued[eye].load(std::memory_order_acquire))continue;auto& p=pending[eye];BOOL ready{};auto result=c->GetData(p.complete.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(result==S_FALSE || (SUCCEEDED(result)&&!ready))continue;if(FAILED(result)){queued[eye]=false;continue;}
  auto path=directory/L"dlss-image-audit"/(L"frame-"+std::to_wstring(p.frame))/(L"eye-"+std::to_wstring(eye));std::error_code ec;std::filesystem::create_directories(path,ec);if(ec)continue;
  D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(c->Map(p.staging.Get(),0,D3D11_MAP_READ,0,&mapped)))continue;
  std::ofstream raw(path/L"grid-rgba32f.bin",std::ios::binary);raw.write(static_cast<char*>(mapped.pData),bytes);
  std::ofstream meta(path/L"metadata.json");meta<<"{\"sourceFrame\":"<<p.frame<<",\"eye\":"<<eye<<",\"grid\":[64,64],\"aaType\":"<<p.aa<<",\"scales\":["<<p.scales[0]<<','<<p.scales[1]<<','<<p.scales[2]<<','<<p.scales[3]<<"],\"viewport\":["<<p.viewport.TopLeftX<<','<<p.viewport.TopLeftY<<','<<p.viewport.Width<<','<<p.viewport.Height<<"],\"images\":[";
  const char* names[]={"hdr-input","dlss-output","native-hdr-after-draw"};const auto* values=static_cast<const float*>(mapped.pData);
  for(unsigned image=0;image<3;++image){unsigned nonfinite=0,nonblack=0;double mean[3]{};float largest=0;
   for(unsigned pixel=0;pixel<pixels;++pixel){bool lit=false;for(unsigned channel=0;channel<3;++channel){float v=values[(image*pixels+pixel)*4+channel];if(!std::isfinite(v)){++nonfinite;continue;}mean[channel]+=v;largest=std::max(largest,v);lit=lit || v>1e-6f;}nonblack+=lit;}
   if(image)meta<<',';meta<<"{\"name\":\""<<names[image]<<"\",\"resource\":"<<p.resources[image]<<",\"width\":"<<p.desc[image].Width<<",\"height\":"<<p.desc[image].Height<<",\"format\":"<<p.desc[image].Format<<",\"nonfiniteChannels\":"<<nonfinite<<",\"nonblackPixels\":"<<nonblack<<",\"maximum\":"<<largest<<",\"mean\":["<<mean[0]/pixels<<','<<mean[1]/pixels<<','<<mean[2]/pixels<<"]}";
  }
  meta<<"]}";c->Unmap(p.staging.Get(),0);queued[eye].store(false,std::memory_order_release);
 }
}
void Shutdown(){for(unsigned i=0;i<2;++i){queued[i]=false;pending[i]={};}constants.Reset();program.Reset();}
}
