#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <utility>

// F8-only mesh evidence. Copies remain asynchronous until GpuCapture's event;
// this helper never changes a pipeline binding or waits for the GPU.
namespace MeshCapture {
using Microsoft::WRL::ComPtr;
inline const GUID LayoutTag={0x71d09447,0x534a,0x44ba,{0xac,0x80,0xc6,0x2f,0x83,0x55,0xc2,0x11}};
inline void RememberLayout(ID3D11InputLayout* layout,const D3D11_INPUT_ELEMENT_DESC* entries,UINT count){
 if(!layout || !entries || count>32)return;std::ostringstream out;out<<'[';
 for(UINT i=0;i<count;++i){const auto& e=entries[i];if(i)out<<',';out<<"{\"semantic\":\"";
  // Semantic names are native identifiers; reject characters that could
  // invalidate JSON rather than treating native data as arbitrary text.
  for(unsigned j=0;e.SemanticName && e.SemanticName[j] && j<64;++j){char ch=e.SemanticName[j];out<<((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'?ch:'_');}
  out<<"\",\"index\":"<<e.SemanticIndex<<",\"format\":"<<e.Format<<",\"slot\":"<<e.InputSlot<<",\"offset\":"<<e.AlignedByteOffset<<",\"class\":"<<e.InputSlotClass<<",\"step\":"<<e.InstanceDataStepRate<<'}';
 }out<<']';auto text=out.str();layout->SetPrivateData(LayoutTag,UINT(text.size()),text.data());
}
struct Buffer {std::string kind;UINT slot{},stride{},offset{},format{},bytes{};uintptr_t source{};ComPtr<ID3D11Buffer> copy;};
struct Data {std::string layout="[]";UINT topology{},start{},instances{},firstInstance{};INT base{};bool indexed{};std::vector<Buffer> buffers;};
inline void Capture(ID3D11DeviceContext* context,bool indexed,UINT start,INT base,UINT instances,UINT firstInstance,uint64_t& totalBytes,Data& result){
 if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 result.indexed=indexed;result.start=start;result.base=base;result.instances=instances;result.firstInstance=firstInstance;
 D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);result.topology=topology;
 ComPtr<ID3D11InputLayout> layout;context->IAGetInputLayout(&layout);UINT length{};
 if(layout && SUCCEEDED(layout->GetPrivateData(LayoutTag,&length,nullptr)) && length && length<16384){std::string text(length,'\0');if(SUCCEEDED(layout->GetPrivateData(LayoutTag,&length,text.data())))result.layout=text;}
 ComPtr<ID3D11Device> device;context->GetDevice(&device);
 auto copy=[&](ID3D11Buffer* source,const char* kind,UINT slot,UINT stride,UINT offset,UINT format){
  if(!source)return;D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);
  if(!desc.ByteWidth || desc.ByteWidth>8*1024*1024 || totalBytes+desc.ByteWidth>64*1024*1024)return;
  Buffer item;item.kind=kind;item.slot=slot;item.stride=stride;item.offset=offset;item.format=format;item.bytes=desc.ByteWidth;item.source=reinterpret_cast<uintptr_t>(source);
  desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;
  if(FAILED(device->CreateBuffer(&desc,nullptr,&item.copy)))return;
  context->CopyResource(item.copy.Get(),source);totalBytes+=item.bytes;result.buffers.push_back(std::move(item));
 };
 ID3D11Buffer* raw[16]{};UINT strides[16]{},offsets[16]{};context->IAGetVertexBuffers(0,16,raw,strides,offsets);
 for(UINT slot=0;slot<16;++slot){ComPtr<ID3D11Buffer> source;source.Attach(raw[slot]);copy(source.Get(),"vertex",slot,strides[slot],offsets[slot],0);}
 if(indexed){ComPtr<ID3D11Buffer> source;DXGI_FORMAT format{};UINT offset{};context->IAGetIndexBuffer(&source,&format,&offset);copy(source.Get(),"index",0,0,offset,format);}
 ComPtr<ID3D11ShaderResourceView> bones;context->VSGetShaderResources(1,1,&bones);
 if(bones){ComPtr<ID3D11Resource> resource;bones->GetResource(&resource);ComPtr<ID3D11Buffer> source;resource.As(&source);D3D11_SHADER_RESOURCE_VIEW_DESC view{};bones->GetDesc(&view);if(source && view.ViewDimension==D3D11_SRV_DIMENSION_BUFFER){D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);copy(source.Get(),"bones",1,desc.StructureByteStride,view.Buffer.FirstElement,view.Format);}}
}
inline std::string Write(ID3D11DeviceContext* context,const std::filesystem::path& directory,const std::string& prefix,const Data& data){
 std::ostringstream out;out<<"{\"indexed\":"<<data.indexed<<",\"start\":"<<data.start<<",\"base\":"<<data.base<<",\"instances\":"<<data.instances<<",\"firstInstance\":"<<data.firstInstance<<",\"topology\":"<<data.topology<<",\"inputLayout\":"<<data.layout<<",\"buffers\":[";bool first=true;
 for(const auto& buffer:data.buffers){D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context->Map(buffer.copy.Get(),0,D3D11_MAP_READ,0,&mapped)))continue;auto file=prefix+"-mesh-"+buffer.kind+std::to_string(buffer.slot)+".bin";
  {std::ofstream stream(directory/file,std::ios::binary);stream.write(static_cast<const char*>(mapped.pData),buffer.bytes);}context->Unmap(buffer.copy.Get(),0);
  if(!first)out<<',';first=false;out<<"{\"kind\":\""<<buffer.kind<<"\",\"slot\":"<<buffer.slot<<",\"stride\":"<<buffer.stride<<",\"offset\":"<<buffer.offset<<",\"format\":"<<buffer.format<<",\"bytes\":"<<buffer.bytes<<",\"source\":"<<buffer.source<<",\"file\":\""<<file<<"\"}";
 }out<<"]}";return out.str();
}
}
