#include "FirstPersonMouthMesh.h"
#include <wrl/client.h>
#include <vector>
#include <sstream>
using Microsoft::WRL::ComPtr;
namespace FirstPersonMouthMesh {
namespace {
constexpr UINT VertexBytes=61392,IndexBytes=11844,VertexStride=48,IndexCount=5922;
constexpr uint64_t VertexHash=0x92262c5afa7e22f1ull,IndexHash=0x6f975e50894b2ac9ull;
struct Entry {
 ComPtr<ID3D11Buffer> vertex,index,vertexCopy,indexCopy;ComPtr<ID3D11Query> finished;
 bool pending=true,accepted{};uint64_t polled=UINT64_MAX;
};
std::vector<Entry> entries;uint64_t queued{},accepted{},rejected{},matchedDraws{};
uint64_t hash(const void* data,UINT bytes){auto* p=static_cast<const uint8_t*>(data);uint64_t value=14695981039346656037ull;for(UINT i=0;i<bytes;++i){value^=p[i];value*=1099511628211ull;}return value;}
void finish(Entry& entry,bool match){entry.pending=false;entry.accepted=match;if(match)++accepted;else ++rejected;entry.vertexCopy.Reset();entry.indexCopy.Reset();entry.finished.Reset();}
void poll(ID3D11DeviceContext* context,Entry& entry,uint64_t frame){
 if(!entry.pending || entry.polled==frame)return;entry.polled=frame;
 auto hr=context->GetData(entry.finished.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
 if(hr==S_FALSE)return;if(FAILED(hr)){finish(entry,false);return;}
 D3D11_MAPPED_SUBRESOURCE vertex{},index{};
 hr=context->Map(entry.vertexCopy.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&vertex);
 if(hr==DXGI_ERROR_WAS_STILL_DRAWING)return;if(FAILED(hr)){finish(entry,false);return;}
 hr=context->Map(entry.indexCopy.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&index);
 if(FAILED(hr)){context->Unmap(entry.vertexCopy.Get(),0);if(hr!=DXGI_ERROR_WAS_STILL_DRAWING)finish(entry,false);return;}
 const bool match=hash(vertex.pData,VertexBytes)==VertexHash && hash(index.pData,IndexBytes)==IndexHash;
 context->Unmap(entry.vertexCopy.Get(),0);context->Unmap(entry.indexCopy.Get(),0);finish(entry,match);
}
}
void Reset(){entries.clear();queued=accepted=rejected=matchedDraws=0;}
bool Match(ID3D11DeviceContext* context,uint64_t frame,UINT count,UINT start,INT base,bool indexed){
 if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE || !indexed || base || !count || count%3 || start%3 || uint64_t(start)+count>IndexCount)return false;
 D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST)return false;
 ComPtr<ID3D11Buffer> vertex,index;UINT stride{},vertexOffset{},indexOffset{};DXGI_FORMAT format{};
 context->IAGetVertexBuffers(0,1,&vertex,&stride,&vertexOffset);context->IAGetIndexBuffer(&index,&format,&indexOffset);
 if(!vertex || !index || stride!=VertexStride || vertexOffset || indexOffset || format!=DXGI_FORMAT_R16_UINT)return false;
 D3D11_BUFFER_DESC vd{},id{};vertex->GetDesc(&vd);index->GetDesc(&id);
 if(vd.ByteWidth!=VertexBytes || id.ByteWidth!=IndexBytes || vd.CPUAccessFlags || id.CPUAccessFlags || vd.Usage==D3D11_USAGE_DYNAMIC || id.Usage==D3D11_USAGE_DYNAMIC)return false;
 for(auto& entry:entries)if(entry.vertex.Get()==vertex.Get() && entry.index.Get()==index.Get()){
  poll(context,entry,frame);if(entry.accepted)++matchedDraws;return entry.accepted;
 }
 // Keep COM ownership so a freed buffer address can never inherit a cached
 // classification. The cap bounds retained resources and pending readbacks.
 if(entries.size()>=16)return false;Entry entry;entry.vertex=vertex;entry.index=index;
 ComPtr<ID3D11Device> device;context->GetDevice(&device);
 auto copy=[&](ID3D11Buffer* source,UINT bytes,ComPtr<ID3D11Buffer>& target){D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;if(FAILED(device->CreateBuffer(&desc,nullptr,&target)))return false;context->CopyResource(target.Get(),source);return true;};
 if(!copy(vertex.Get(),VertexBytes,entry.vertexCopy) || !copy(index.Get(),IndexBytes,entry.indexCopy))return false;
 D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT,0};if(FAILED(device->CreateQuery(&qd,&entry.finished)))return false;
 context->End(entry.finished.Get());entries.push_back(std::move(entry));++queued;return false;
}
std::string Status(){std::ostringstream out;out<<"{\"queued\":"<<queued<<",\"accepted\":"<<accepted<<",\"rejected\":"<<rejected<<",\"matchedDraws\":"<<matchedDraws<<'}';return out.str();}
}
