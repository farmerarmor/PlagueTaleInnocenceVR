#include <windows.h>
#include <d3d11.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>
using Microsoft::WRL::ComPtr;
static uintptr_t base;
static std::filesystem::path directory;
static std::atomic<int> trial{0};
static std::atomic<uint64_t> outsideDraws{0};
static thread_local uint64_t draws=0;
static thread_local bool inside=false;
static uint64_t frames=0;
static std::mutex logMutex;
using FrameFn=void(*)(void*);
using DrawFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
using IndexedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using InstancedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
using IndexedInstancedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
static FrameFn frameOriginal;
static DrawFn drawOriginal;
static IndexedFn indexedOriginal;
static InstancedFn instancedOriginal;
static IndexedInstancedFn indexedInstancedOriginal;
static void log(const char* fmt,...){std::lock_guard lock(logMutex);FILE* f{};_wfopen_s(&f,(directory/L"PlagueGpuProbe.log").c_str(),L"ab");if(!f)return;va_list a;va_start(a,fmt);vfprintf(f,fmt,a);va_end(a);fputc('\n',f);fclose(f);}
static void count(){if(inside)++draws;else ++outsideDraws;}
static void STDMETHODCALLTYPE drawHook(ID3D11DeviceContext* c,UINT a,UINT b){count();drawOriginal(c,a,b);}
static void STDMETHODCALLTYPE indexedHook(ID3D11DeviceContext* c,UINT a,UINT b,INT d){count();indexedOriginal(c,a,b,d);}
static void STDMETHODCALLTYPE instancedHook(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,UINT e){count();instancedOriginal(c,a,b,d,e);}
static void STDMETHODCALLTYPE indexedInstancedHook(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,INT e,UINT f){count();indexedInstancedOriginal(c,a,b,d,e,f);}
static void frameHook(void* renderer){
 const int selected=trial.load();auto& flags=*reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(renderer)+0x2a2b8);const uint64_t previous=flags;
 if(selected)flags=(flags&~0x780000000ull)|0x80000000ull;
 if(selected==2)flags&=~0x800000000000ull;
 inside=true;draws=0;frameOriginal(renderer);inside=false;
 const auto f=++frames;if(f<4 || (selected && f%100==0))log("frame=%llu trial=%d cpuFrameDraws=%llu otherThreadDraws=%llu renderer=%p flags=%llx",f,selected,draws,outsideDraws.load(),renderer,flags);
 if(selected)flags=previous;
}
static bool supported(){wchar_t p[32768]{};GetModuleFileNameW(nullptr,p,_countof(p));std::ifstream f(p,std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});BCRYPT_ALG_HANDLE alg{};if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return false;uint8_t h[32]{};bool ok=BCryptHash(alg,nullptr,0,b.data(),static_cast<ULONG>(b.size()),h,32)>=0;BCryptCloseAlgorithmProvider(alg,0);const uint8_t e[]={0xbc,0x9d,0x87,0xb8,0x8a,0x97,0x80,0xef,0xb5,0xe2,0xae,0x48,0xf4,0x44,0xc1,0x14,0x72,0x95,0xb5,0x9b,0x77,0x5d,0xe9,0x6f,0xe1,0x3d,0xf9,0x4e,0x7a,0x59,0x67,0xb9};return ok && !memcmp(h,e,32);}
static DWORD WINAPI start(void* module){
 wchar_t p[32768]{};GetModuleFileNameW(static_cast<HMODULE>(module),p,_countof(p));directory=std::filesystem::path(p).parent_path();base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 if(!supported()){log("unsupported executable");return 1;}
 const uint8_t prefix[]={0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54};
 if(memcmp(reinterpret_cast<void*>(base+0x62afe0),prefix,sizeof(prefix)) || MH_Initialize()!=MH_OK)return 2;
 if(MH_CreateHook(reinterpret_cast<void*>(base+0x62afe0),reinterpret_cast<void*>(frameHook),reinterpret_cast<void**>(&frameOriginal))!=MH_OK)return 3;
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 4;
 void** v=*reinterpret_cast<void***>(context.Get());
 const auto hook=[&](int index,void* replacement,void** original){auto status=MH_CreateHook(v[index],replacement,original);log("D3D11 context method %d hook=%d",index,status);return status==MH_OK;};
 if(!hook(12,reinterpret_cast<void*>(indexedHook),reinterpret_cast<void**>(&indexedOriginal)) || !hook(13,reinterpret_cast<void*>(drawHook),reinterpret_cast<void**>(&drawOriginal)) || !hook(20,reinterpret_cast<void*>(indexedInstancedHook),reinterpret_cast<void**>(&indexedInstancedOriginal)) || !hook(21,reinterpret_cast<void*>(instancedHook),reinterpret_cast<void**>(&instancedOriginal)))return 5;
 if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK)return 6;log("GPU frame observation active; gpu-control.txt 1=early native flag, 2=early flag and synchronous renderer jobs (3-second bounded trials)");
 int previous=0;uint64_t since=0;
 for(;;){int requested=0;{std::ifstream control(directory/L"gpu-control.txt");control>>requested;}if(requested<0 || requested>2)requested=0;if(requested!=previous){trial=requested;since=GetTickCount64();previous=requested;log("trial=%d",requested);}if(trial.load() && GetTickCount64()-since>3000){trial=0;log("trial restored");}Sleep(100);}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD why,void*){if(why==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(h);HANDLE t=CreateThread(nullptr,0,start,h,0,nullptr);if(t)CloseHandle(t);}return TRUE;}
