#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <intrin.h>
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <filesystem>
using Microsoft::WRL::ComPtr;
using GetDataFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Asynchronous*,void*,UINT,UINT);
static GetDataFn originalGetData;
static uintptr_t probeBegin{},probeEnd{};
static std::filesystem::path directory;
static std::atomic<unsigned> calls{};
static HRESULT STDMETHODCALLTYPE getDataHook(ID3D11DeviceContext* context,ID3D11Asynchronous* query,void* data,UINT size,UINT flags){
 const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
 const bool probe=caller>=probeBegin && caller<probeEnd;
 if(probe)flags=0; // Let the native D3D11 query API submit pending commands.
 const auto status=originalGetData(context,query,data,size,flags);
 if(probe){const auto n=++calls;if(n<12 || (n%1000==0 && status!=S_OK)){FILE* f{};_wfopen_s(&f,(directory/L"PlagueQueryProbe.log").c_str(),L"ab");if(f){fprintf(f,"probe query=%u context=%p caller=%llx result=%lx done=%u\n",n,context,caller-probeBegin,status,(size==4 && data)?*static_cast<unsigned*>(data):0);fclose(f);}}}
 return status;
}
static DWORD WINAPI start(void* module){
 wchar_t path[32768]{};GetModuleFileNameW(static_cast<HMODULE>(module),path,_countof(path));directory=std::filesystem::path(path).parent_path();
 auto* game=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
 auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(game+reinterpret_cast<IMAGE_DOS_HEADER*>(game)->e_lfanew);
 // The renderer probe already performs the full executable hash guard.
 if(nt->OptionalHeader.SizeOfImage!=0x181e000)return 1;
 auto* probe=reinterpret_cast<uint8_t*>(GetModuleHandleW(L"PlagueStereoProbe07.dll"));if(!probe)return 2;
 auto* pnt=reinterpret_cast<IMAGE_NT_HEADERS64*>(probe+reinterpret_cast<IMAGE_DOS_HEADER*>(probe)->e_lfanew);
 probeBegin=reinterpret_cast<uintptr_t>(probe);probeEnd=probeBegin+pnt->OptionalHeader.SizeOfImage;
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 3;
 if(MH_Initialize()!=MH_OK)return 4;
 void* target=(*reinterpret_cast<void***>(context.Get()))[29];
 if(MH_CreateHook(target,reinterpret_cast<void*>(getDataHook),reinterpret_cast<void**>(&originalGetData))!=MH_OK)return 5;
 if(MH_EnableHook(target)!=MH_OK)return 6;
 return 0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD why,void*){if(why==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(module);HANDLE t=CreateThread(nullptr,0,start,module,0,nullptr);if(t)CloseHandle(t);}return TRUE;}
