#include "NativeBridge.h"
#include "MenuScreenPolicy.h"
#include <fstream>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <share.h>
#include <filesystem>

using Microsoft::WRL::ComPtr;
struct Connection {
    HANDLE game{},mapping{};
    Transport::Header* header{};
    ~Connection() {
        if(header){InterlockedExchange(reinterpret_cast<volatile LONG*>(&header->reserved2),1);UnmapViewOfFile(header);}
        if(mapping)CloseHandle(mapping);
        if(game)CloseHandle(game);
    }
};
int wmain(int argc,wchar_t** argv) {
    if(argc==2 && !wcscmp(argv[1],L"--display-query")) {
        HeadsetDisplay::Settings s{};
        if(!NativeBridge::QueryDisplaySettings(s))return 2;
        printf("OpenXR eye=%ux%u refresh=%.3f Hz\n",s.width,s.height,s.refreshHz);return 0;
    }
    if(argc!=2){fwprintf(stderr,L"Usage: PlagueTaleVRHost.exe <game PID>\n");return 1;}
    DWORD pid=wcstoul(argv[1],nullptr,10);
    wchar_t name[96];Transport::Name(name,pid);
    Connection connection;
    HANDLE game=connection.game=OpenProcess(SYNCHRONIZE,FALSE,pid);
    HANDLE mapping{};
    auto deadline=GetTickCount64()+5000;
    while(game && !mapping && GetTickCount64()<deadline && WaitForSingleObject(game,50)==WAIT_TIMEOUT)
        mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
    connection.mapping=mapping;
    if(!game || !mapping){printf("Game or native pair mapping unavailable: %lu\n",GetLastError());return 3;}
    auto* h=connection.header=static_cast<Transport::Header*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Transport::Header)));
    if(!h || h->magic!=Transport::Magic || h->version!=Transport::Version || h->pid!=pid)return 4;
    auto stopFlag=reinterpret_cast<volatile LONG*>(&h->reserved2);
    while(!h->generation && !InterlockedCompareExchange(stopFlag,0,0) && WaitForSingleObject(game,10)==WAIT_TIMEOUT){}
    if(!h->generation || !h->width || h->width>16384 || !h->height || h->height>8192)return 5;
    ComPtr<IDXGIFactory1> factory;ComPtr<IDXGIAdapter1> selected;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return 6;
    for(UINT i=0;;++i) {
        ComPtr<IDXGIAdapter1> a;if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;
        DXGI_ADAPTER_DESC1 d{};
        if(a && SUCCEEDED(a->GetDesc1(&d)) && !memcmp(&d.AdapterLuid,&h->adapter,sizeof(LUID))){selected=a;break;}
    }
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(!selected || FAILED(D3D11CreateDevice(selected.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 7;
    ComPtr<ID3D11Texture2D> shared,local;ComPtr<IDXGIKeyedMutex> mutex;
    if(FAILED(device->OpenSharedResource(reinterpret_cast<HANDLE>(h->sharedHandle),IID_PPV_ARGS(&shared))) || FAILED(shared.As(&mutex)))return 8;
    D3D11_TEXTURE2D_DESC d{};shared->GetDesc(&d);
    if(!NativeBridge::ValidatePair(d,h->height) || d.Width!=h->width || uint32_t(d.Format)!=h->format)return 9;
    d.MiscFlags=0;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&local)))return 10;
    NativeBridge::SetTrackingChannel(h);
    uint64_t frame{},fresh{},repeats{},lastTick=GetTickCount64(),capturePoll{},statusPoll{};Transport::RenderInfo info{};
    FILE* log=_fsopen("PlagueTaleVR-host.log","w",_SH_DENYNO);
    if(log){fprintf(log,"Native pair host PID=%lu source=%ux%u format=%u ABI=%u\n",pid,h->width,h->height,h->format,Transport::Version);fflush(log);}
    while(WaitForSingleObject(game,0)==WAIT_TIMEOUT && !InterlockedCompareExchange(stopFlag,0,0)) {
        if(GetAsyncKeyState(VK_F10)&0x8000){InterlockedExchange(stopFlag,1);break;}
        HRESULT acquired=mutex->AcquireSync(1,2);
        if(acquired==S_OK) {
            context->CopyResource(local.Get(),shared.Get());context->Flush();
            frame=h->frameId;info=h->rendered;
            mutex->ReleaseSync(0);++fresh;
        } else if(acquired!=WAIT_TIMEOUT){if(log)fprintf(log,"AcquireSync failed: %08lx\n",acquired);break;}
        else ++repeats;
        if(frame){
            NativeBridge::SetSourceFrame(frame);NativeBridge::SetRenderInfo(info);
            const auto now=GetTickCount64();
            if(now-statusPoll>=100){statusPoll=now;std::ifstream file("dlss-osd.txt");std::string text,line;for(unsigned i=0;i<2 && std::getline(file,line);++i){if(i)text+='\n';text+=line.substr(0,120);}if(!text.empty())NativeBridge::SetStatusText(std::wstring(text.begin(),text.end()));}
            if(now-capturePoll>=200 && (info.mode==1 || info.mode==MenuScreenPolicy::Mode) && info.eyeMask==3 && info.tracking.valid){
                capturePoll=now;std::error_code ec;
                if(std::filesystem::remove("host-capture.flag",ec))NativeBridge::RequestCapture();
            }
            NativeBridge::Present(nullptr,local.Get(),h->height,h->swapEyes!=0);
        }
        else Sleep(5);
        if(GetTickCount64()-lastTick>=5000) {
            if(log){fprintf(log,"freshPairs=%llu repeatedPolls=%llu submitted=%llu sourceFrame=%llu\n",fresh,repeats,NativeBridge::SubmittedPairs(),frame);fflush(log);}
            lastTick=GetTickCount64();
        }
    }
    InterlockedExchange(stopFlag,1);
    NativeBridge::SetTrackingChannel(nullptr);NativeBridge::Shutdown();
    if(log){fprintf(log,"Stopped. freshPairs=%llu repeatedPolls=%llu\n",fresh,repeats);fclose(log);}
    return 0;
}

