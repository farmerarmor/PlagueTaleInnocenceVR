#include "PairProducer.h"
#include <cstring>

using Microsoft::WRL::ComPtr;
bool PairProducer::Initialize(unsigned width,unsigned height) {
    if(!width || width>16384 || !height || height>8192)return false;
    expectedWidth=width;expectedHeight=height;
    wchar_t name[96]; Transport::Name(name, GetCurrentProcessId());
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Transport::Header),name);
    if(!mapping || GetLastError()==ERROR_ALREADY_EXISTS)return false;
    header=static_cast<Transport::Header*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Transport::Header)));
    if(!header)return false;
    *header={}; header->magic=Transport::Magic; header->version=Transport::Version; header->pid=GetCurrentProcessId();
    Transport::FrameEventName(name,GetCurrentProcessId());
    frameEvent=CreateEventW(nullptr,FALSE,FALSE,name);
    if(!frameEvent)return false;
    return true;
}
void PairProducer::Pace() {
    // First image must bootstrap the companion. Once XR produces samples,
    // gate a whole native interval on xrWaitFrame, never one eye at a time.
    if(!frameEvent || !header || !header->tracking.id || StopRequested())return;
    ++pacingWaits;
    if(WaitForSingleObject(frameEvent,50)==WAIT_TIMEOUT)++pacingTimeouts;
}
bool PairProducer::Prepare(ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
    if(d.Width!=expectedWidth || d.Height!=expectedHeight || d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM ||
       d.SampleDesc.Count!=1 || d.ArraySize!=1 || d.MipLevels!=1)return false;
    if(pair)return d.Width==source.Width && d.Height==source.Height && d.Format==source.Format;
    source=d; texture->GetDevice(&device); device->GetImmediateContext(&context);
    ComPtr<IDXGIDevice> dx; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC info{};
    if(FAILED(device.As(&dx)) || FAILED(dx->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&info)))return false;
    d.Height*=2; d.Usage=D3D11_USAGE_DEFAULT; d.CPUAccessFlags=0;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    d.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&pair)) || FAILED(pair.As(&mutex)))return false;
    ComPtr<IDXGIResource> resource; HANDLE shared{};
    if(FAILED(pair.As(&resource)) || FAILED(resource->GetSharedHandle(&shared)))return false;
    header->width=d.Width; header->height=source.Height; header->format=d.Format;
    header->sharedHandle=reinterpret_cast<uint64_t>(shared); header->adapter=info.AdapterLuid;
    MemoryBarrier(); InterlockedExchange(&header->generation,1);
    return true;
}
bool PairProducer::Left(ID3D11Texture2D* texture) {
    Cancel();
    if(!texture || !Prepare(texture) || StopRequested())return false;
    // A host owns key 1; rendering never waits for the headset/compositor.
    if(mutex->AcquireSync(0,0)!=S_OK)return false;
    held=true;
    D3D11_BOX box{0,0,0,source.Width,source.Height,1};
    context->CopySubresourceRegion(pair.Get(),0,0,0,0,texture,0,&box);
    return true;
}
bool PairProducer::Right(ID3D11Texture2D* texture,uint64_t frame,const Transport::RenderInfo& rendered) {
    if(!held || !texture)return false;
    D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
    if(d.Width!=source.Width || d.Height!=source.Height || d.Format!=source.Format || d.SampleDesc.Count!=1){Cancel();return false;}
    D3D11_BOX box{0,0,0,source.Width,source.Height,1};
    context->CopySubresourceRegion(pair.Get(),0,0,source.Height,0,texture,0,&box);
    header->frameId=frame; header->rendered=rendered;
    context->Flush(); MemoryBarrier();
    held=false;
    if(FAILED(mutex->ReleaseSync(1)))return false;
    ++published; return true;
}
bool PairProducer::ReadTracking(Transport::Tracking& result){return reader.Read(header,result,GetTickCount64());}
bool PairProducer::ReadLatestTracking(Transport::Tracking& result)const{const auto now=GetTickCount64();return Transport::ReadTracking(header,result) && result.valid && now>=result.tick && now-result.tick<250;}
void PairProducer::Cancel() {
    if(held){context->Flush(); mutex->ReleaseSync(0); held=false;}
}
bool PairProducer::StopRequested() const {
    return header && InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&header->reserved2),0,0)!=0;
}
void PairProducer::Stop() {
    Cancel();
    if(header)InterlockedExchange(reinterpret_cast<volatile LONG*>(&header->reserved2),1);
    // Retain handles/resources with the dormant module until the game exits.
}
