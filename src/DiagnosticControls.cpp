#include "DiagnosticControls.h"
#include "DecalPass.h"
#include "DlssPass.h"
#include <windows.h>
#include <fstream>
namespace DiagnosticControls {
namespace {std::filesystem::path directory;bool pressed[7]{};uint64_t captures{};
void toggle(const wchar_t* section,const wchar_t* key,int defaultValue){auto ini=directory/L"PlagueTaleVR.ini";int prior=GetPrivateProfileIntW(section,key,defaultValue,ini.c_str());WritePrivateProfileStringW(section,key,prior?L"0":L"1",ini.c_str());}
}
void Initialize(const std::filesystem::path& path){directory=path;}
bool Poll(uint64_t frame,uint64_t pose){
 DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);bool focused=foreground==GetCurrentProcessId(),capture=false;
 int keys[]={VK_F5,VK_F6,VK_F7,VK_F8,VK_F4,VK_F3,VK_F2};
 for(unsigned i=0;i<7;++i){bool down=(GetAsyncKeyState(keys[i])&0x8000)!=0,edge=focused && down&&!pressed[i];pressed[i]=down;if(!edge)continue;
  if(i==0){if(!(GetAsyncKeyState(VK_SHIFT)&0x8000)){auto ini=directory/L"PlagueTaleVR.ini";int legacy=GetPrivateProfileIntW(L"Rendering",L"DecalMode",0,ini.c_str())==2;toggle(L"Rendering",L"DisableDecals",legacy);}DecalPass::Benchmark();}
  if(i==1)toggle(L"Camera",L"FollowHead",0);
  if(i==2){if(GetAsyncKeyState(VK_SHIFT)&0x8000)toggle(L"Diagnostics",L"LightListMode",1);else toggle(L"Diagnostics",L"ProjectedLightBoundsMode",1);}
  if(i==3){capture=true;++captures;std::ofstream host(directory/L"host-capture.flag");host<<"F8 capture "<<captures;}
  if(i==5){DlssPass::Cycle();auto ini=directory/L"PlagueTaleVR.ini";if(GetPrivateProfileIntW(L"Diagnostics",L"DLSSAutoBenchmark",0,ini.c_str()))DecalPass::Benchmark();}
  if(i==4)toggle(L"UI",L"CorrectAimReticle",1);
  if(i==6){if(GetAsyncKeyState(VK_SHIFT)&0x8000)toggle(L"Camera",L"HideFirstPersonHead",1);else toggle(L"Camera",L"FirstPerson",0);DlssPass::ResetHistory();}
  auto ini=directory/L"PlagueTaleVR.ini";std::ofstream f(directory/L"diagnostic-controls.jsonl",std::ios::app);
  f<<"{\"shift\":"<<((GetAsyncKeyState(VK_SHIFT)&0x8000)?1:0)<<",\"DLSSState\":\""<<DlssPass::Status()<<"\",\"DLSSEffective\":"<<DlssPass::Effective()<<",\"DLSSMode\":"<<GetPrivateProfileIntW(L"Rendering",L"DLSSMode",0,ini.c_str())<<",\"sourceFrame\":"<<frame<<",\"poseId\":"<<pose<<",\"key\":\"F"<<(i==6?2:i==5?3:i==4?4:i+5)<<"\",\"captureRequest\":"<<captures<<",\"DisableDecals\":"<<GetPrivateProfileIntW(L"Rendering",L"DisableDecals",GetPrivateProfileIntW(L"Rendering",L"DecalMode",0,ini.c_str())==2,ini.c_str())<<",\"CorrectAimReticle\":"<<GetPrivateProfileIntW(L"UI",L"CorrectAimReticle",1,ini.c_str())<<",\"DLSS\":"<<GetPrivateProfileIntW(L"Rendering",L"DLSS",0,ini.c_str())<<",\"FollowHead\":"<<GetPrivateProfileIntW(L"Camera",L"FollowHead",0,ini.c_str())<<",\"FirstPerson\":"<<GetPrivateProfileIntW(L"Camera",L"FirstPerson",0,ini.c_str())<<",\"HideFirstPersonHead\":"<<GetPrivateProfileIntW(L"Camera",L"HideFirstPersonHead",1,ini.c_str())<<",\"ProjectedLightBoundsMode\":"<<GetPrivateProfileIntW(L"Diagnostics",L"ProjectedLightBoundsMode",1,ini.c_str())<<",\"LightListMode\":"<<GetPrivateProfileIntW(L"Diagnostics",L"LightListMode",1,ini.c_str())<<"}\n";
 }
 return capture;
}
}
