#include "FirstPerson.h"
#include "FirstPersonMath.h"
#include <windows.h>
#include <mutex>
#include <fstream>
#include <cstring>
namespace FirstPerson {
namespace {
uintptr_t base{},imageSize{};void** scriptManager{};std::filesystem::path directory;std::mutex mutex;
bool enabled{},applied{};float standing=1.5f,forward=.18f,side{};uint64_t settingsTick{},calls{},anchors{},misses{};
void* lastCamera{};void* lastPlayer{};void* lastNode{};CameraMath::Matrix lastNative{},lastRoot{},lastView{};
template<class T>T& at(uintptr_t p,size_t n=0){return *reinterpret_cast<T*>(p+n);}
uintptr_t object(uintptr_t owner,size_t offset){const auto handle=at<uintptr_t>(owner,offset);return handle && at<int>(handle,8)==at<int>(owner,offset+12)?at<uintptr_t>(handle,16):0;}
bool hasType(uintptr_t value,const char* wanted){
 const auto table=at<uintptr_t>(value);if(table<base+8 || table>=base+imageSize)return false;
 const auto locator=at<uintptr_t>(table-8);if(locator<base || locator>base+imageSize-24 || at<uint32_t>(locator)!=1 || at<uint32_t>(locator,20)!=locator-base)return false;
 const auto type=at<uint32_t>(locator,12);return type<imageSize-128 && !strcmp(reinterpret_cast<const char*>(base+type+16),wanted);
}
bool playerFor(void* cameraNode,uintptr_t& player,uintptr_t& node,CameraMath::Matrix& root){
 __try {
  if(!scriptManager || !*scriptManager)return false;const auto script=reinterpret_cast<uintptr_t>(*scriptManager);
  if(!hasType(script,".?AVScriptManager_G@@"))return false;
  const auto level=object(script,0x1400);if(!level)return false;const auto game=object(level,0xa38);if(!game || !hasType(game,".?AVGame_Z@@"))return false;
  const int count=at<int>(game,0x1d0);if(count<1 || count>8)return false;
  const auto cameras=at<uintptr_t>(game,0x210),players=at<uintptr_t>(game,0x200);if(!cameras || !players)return false;
  for(int i=0;i<count;++i){if(object(cameras,size_t(i)*16)!=reinterpret_cast<uintptr_t>(cameraNode))continue;
   player=object(players,size_t(i)*16);if(!player || !hasType(player,".?AVP_AMICIA@@"))return false;
   node=object(player,0x1c8);if(!node || !hasType(node,".?AVNode_Z@@"))return false;
   const auto transform=at<uintptr_t>(node,0x178);if(!transform)return false;root=CameraMath::Load(reinterpret_cast<const void*>(transform));return CameraMath::Rigid(root);
  }
  return false;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
float setting(const wchar_t* key,float fallback,float low,float high){wchar_t text[48]{};auto defaultText=std::to_wstring(fallback);GetPrivateProfileStringW(L"Camera",key,defaultText.c_str(),text,48,(directory/L"PlagueTaleVR.ini").c_str());auto value=wcstof(text,nullptr);return std::isfinite(value) && value>=low && value<=high?value:fallback;}
void settings(){const auto now=GetTickCount64();if(now-settingsTick<100)return;settingsTick=now;enabled=GetPrivateProfileIntW(L"Camera",L"FirstPerson",0,(directory/L"PlagueTaleVR.ini").c_str())!=0;standing=setting(L"FirstPersonHeight",1.5f,.4f,2.2f);forward=setting(L"FirstPersonForward",.18f,-.5f,.5f);side=setting(L"FirstPersonSide",0.f,-.3f,.3f);}
}
bool Initialize(uintptr_t image,const std::filesystem::path& path){
 base=image;directory=path;const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);imageSize=nt->OptionalHeader.SizeOfImage;
 // Native menu/game code resolves this ScriptManager's LevelAgent handle.
 const unsigned char prefix[]={0x48,0x8b,0x05,0xa5,0x1e,0x03,0x01,0x48,0x8b,0x90,0x00,0x14,0x00,0x00};
 if(memcmp(reinterpret_cast<const void*>(base+0x4ee7e4),prefix,sizeof(prefix)))return false;
 scriptManager=reinterpret_cast<void**>(base+0x4ee7eb+at<int32_t>(base+0x4ee7e7));return true;
}
CameraMath::Matrix ViewBase(void* cameraNode,const CameraMath::Matrix& native,CameraMath::Matrix* playerWorld){
 std::lock_guard lock(mutex);settings();++calls;lastCamera=cameraNode;lastNative=native;applied=false;
 if(playerWorld)*playerWorld={};
 if(!enabled)return native;uintptr_t player{},node{};CameraMath::Matrix root{},view{};
 if(!playerFor(cameraNode,player,node,root) || !FirstPersonMath::Anchor(native,root,standing,forward,side,view)){++misses;return native;}
 lastPlayer=reinterpret_cast<void*>(player);lastNode=reinterpret_cast<void*>(node);lastRoot=root;lastView=view;applied=true;++anchors;if(playerWorld)*playerWorld=root;return view;
}
void Capture(uint64_t frame,uint64_t pose){
 std::lock_guard lock(mutex);std::error_code error;auto path=directory/L"gpu-captures"/(L"frame-"+std::to_wstring(frame));std::filesystem::create_directories(path,error);if(error)return;
 std::ofstream f(path/L"first-person.json");f<<"{\"sourceFrame\":"<<frame<<",\"poseId\":"<<pose<<",\"enabled\":"<<enabled<<",\"applied\":"<<applied<<",\"cameraNode\":"<<reinterpret_cast<uintptr_t>(lastCamera)<<",\"player\":"<<reinterpret_cast<uintptr_t>(lastPlayer)<<",\"playerNode\":"<<reinterpret_cast<uintptr_t>(lastNode)<<",\"calls\":"<<calls<<",\"anchors\":"<<anchors<<",\"misses\":"<<misses<<",\"height\":"<<standing<<",\"forward\":"<<forward<<",\"side\":"<<side;
 const auto matrix=[&](const char* name,const CameraMath::Matrix& m){f<<",\""<<name<<"\":[";for(unsigned i=0;i<16;++i){if(i)f<<',';f<<m.m[i];}f<<']';};matrix("native",lastNative);matrix("playerWorld",lastRoot);matrix("viewBase",lastView);f<<'}';
}
#ifdef PLAGUE_FIRST_PERSON_TEST_ACCESS
bool TestPlayer(uintptr_t image,uintptr_t size,void* script,void* cameraNode,CameraMath::Matrix& root){base=image;imageSize=size;scriptManager=&script;uintptr_t player{},node{};return playerFor(cameraNode,player,node,root);}
#endif
}
