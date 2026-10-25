#include "MenuScreen.h"
#include "MenuScreenPolicy.h"
#include <windows.h>
#include <atomic>
#include <fstream>
#include <cstring>
#include <mutex>
namespace MenuScreen {
namespace {
uintptr_t base{},imageSize{};void** manager{};std::filesystem::path directory;
std::atomic<bool> active{};std::mutex mutex;MenuScreenPolicy::Gate gate;
bool enabled=true;uint64_t settingsTick{};unsigned focusCount{};
template<class T>T& at(uintptr_t p,size_t n=0){return *reinterpret_cast<T*>(p+n);}
bool inside(uintptr_t p,size_t n){return p>=base && p-base<imageSize && n<=imageSize-(p-base);}
bool type(uintptr_t descriptor,const char* name){return inside(descriptor,16+strlen(name)+1) && !strcmp(reinterpret_cast<const char*>(descriptor+16),name);}
bool dialog(uintptr_t object){
 auto vtable=at<uintptr_t>(object);if(!inside(vtable-8,8))return false;
 auto locator=at<uintptr_t>(vtable-8);if(!inside(locator,24) || at<uint32_t>(locator)!=1 || at<uint32_t>(locator,20)!=locator-base)return false;
 const auto own=base+at<uint32_t>(locator,12);
 if(type(own,".?AVCDialog_PLG_CraftUpgradeCodex_G@@"))return true;
 const auto hierarchy=base+at<uint32_t>(locator,16);if(!inside(hierarchy,16))return false;
 const auto count=at<uint32_t>(hierarchy,8);const auto array=base+at<uint32_t>(hierarchy,12);if(count>32 || !inside(array,size_t(count)*4))return false;
 for(unsigned i=0;i<count;++i){auto descriptor=base+at<uint32_t>(array,i*4);if(!inside(descriptor,4))return false;
  if(type(base+at<uint32_t>(descriptor),".?AVCDialog_PLG_MenuBase_G@@"))return true;}
 return false;
}
bool query(bool& menu){
 __try {
  menu=false;const auto gui=reinterpret_cast<uintptr_t>(*manager);if(!gui)return true;
  if(at<uintptr_t>(gui)!=base+0xf64ac0)return false;
  const auto focus=at<uintptr_t>(gui,0x888);if(!focus)return true;
  const auto count=at<int>(focus,0x44);if(count<0 || count>64)return false;focusCount=unsigned(count);
  const auto entries=at<uintptr_t>(focus,0x48);if(count && !entries)return false;
  // Native focus-manager code reads these owned handle objects the same way.
  for(int i=0;i<count;++i){auto handle=at<uintptr_t>(entries,size_t(i)*8);if(!handle)continue;auto object=at<uintptr_t>(handle,16);if(object && dialog(object)){menu=true;break;}}
  return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool Initialize(uintptr_t image,const std::filesystem::path& path){
 base=image;directory=path;const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);imageSize=nt->OptionalHeader.SizeOfImage;
 const unsigned char getter[]={0x48,0x8b,0x05,0xd9,0x18,0xba,0x00,0xc3};
 const unsigned char focus[]={0x4c,0x63,0x79,0x54,0x33,0xdb,0x48,0x63,0xea,0x48,0x8b,0xf1,0x8b,0x51,0x44};
 if(memcmp(reinterpret_cast<const void*>(base+0xb8c3c0),getter,sizeof(getter)) || memcmp(reinterpret_cast<const void*>(base+0xc2dac8),focus,sizeof(focus)))return false;
 manager=reinterpret_cast<void**>(base+0xb8c3c7+at<int32_t>(base+0xb8c3c3));return true;
}
bool Refresh(){
 std::lock_guard lock(mutex);const auto now=GetTickCount64();
 if(now-settingsTick>=100){settingsTick=now;enabled=GetPrivateProfileIntW(L"UI",L"MenuVirtualScreen",1,(directory/L"PlagueTaleVR.ini").c_str())!=0;}
 bool menu{};const bool valid=manager && query(menu);const bool next=enabled && gate.Update(valid,menu,now);
 const bool previous=active.exchange(next);if(next!=previous){std::ofstream f(directory/L"menu-screen.json");f<<"{\"active\":"<<(next?"true":"false")<<",\"nativeMenu\":"<<(menu?"true":"false")<<",\"valid\":"<<(valid?"true":"false")<<",\"focusCount\":"<<focusCount<<",\"tick\":"<<now<<"}";}
 return next;
}
bool Active(){return active.load();}
#ifdef PLAGUE_MENU_TEST_ACCESS
bool TestQuery(uintptr_t image,uintptr_t size,void* gui,bool& menu){base=image;imageSize=size;manager=&gui;return query(menu);}
#endif
}
