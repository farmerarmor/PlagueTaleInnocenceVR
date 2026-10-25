#include "GameplayCamera.h"
#include "CameraOverride.h"
#include "FirstPerson.h"
#include <windows.h>
#include <MinHook.h>
#include <mutex>
#include <fstream>
#include <string>
#include <cstring>
#include <intrin.h>
namespace GameplayCamera {
namespace {
struct Target {void* source{};void* entity{};void* handle{};int generation{};};
Target target;
std::mutex mutex;
CameraOverride overrideState;
Transport::Pose reference{};bool referenceValid{},enabled{};
ReadTracking readTracking{};bool(*vrActive)(){};
std::filesystem::path directory;
uintptr_t base{};
uint64_t settingsTick{},calls{},writes{},restores{},conflicts{},faults{},lastPose{},preparations{};
DWORD updateThread{};
using UpdateFn=uintptr_t(*)(void*,void*);
UpdateFn original{};
using WorldFn=CameraMath::Matrix*(*)(void*);
WorldFn originalWorld{};
uint64_t movementReads{},movementRedirects{},movementFallbacks{};
DWORD movementThread{};
CameraMath::Matrix lastMovementNative{},lastMovementBase{};
bool read(const Target&,CameraMath::Matrix&,CameraMath::Matrix&);
// This native getter is also used by visibility, rendering and interactions.
// Only the input-to-world movement conversion at 3c57e0 receives a copy of
// the retained mouse base. Its caller consumes the pointer synchronously.
CameraMath::Matrix* movementWorld(void* user){
 const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
 auto* native=originalWorld(user);
 if(caller!=base+0x3c58cd)return native;
 std::lock_guard lock(mutex);++movementReads;movementThread=GetCurrentThreadId();
 if(!enabled || !referenceValid || !vrActive() || !overrideState.applied)return native;
 CameraMath::Matrix world{},inverse{};
 if(reinterpret_cast<uintptr_t>(native)!=reinterpret_cast<uintptr_t>(target.source)+0x550 || !read(target,world,inverse) || !overrideState.Matches(world)){++movementFallbacks;return native;}
 // Returning thread-local storage leaves the canonical HMD matrix untouched
 // throughout movement; concurrent animation/visibility readers still see it.
 alignas(16) static thread_local CameraMath::Matrix movementBase;
 movementBase=overrideState.RenderBase(world);lastMovementNative=world;lastMovementBase=movementBase;++movementRedirects;
 return &movementBase;
}
template<class T>T& at(void* p,size_t offset){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);}
void log(const std::string& value){std::ofstream f(directory/L"gameplay-camera.log",std::ios::app);f<<value<<'\n';}
bool valid(const Target& t){
 __try{return t.source && t.entity && t.handle && at<int>(t.handle,8)==t.generation && at<void*>(t.handle,0x10)==t.entity && at<void*>(t.entity,0x110)==t.source;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool read(const Target& t,CameraMath::Matrix& world,CameraMath::Matrix& inverse){
 __try{if(!valid(t))return false;world=CameraMath::Load(static_cast<unsigned char*>(t.source)+0x550);inverse=CameraMath::Load(static_cast<unsigned char*>(t.source)+0x590);return CameraMath::Rigid(world) && CameraMath::Rigid(inverse);}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool write(const Target& t,const CameraMath::Matrix& world,const CameraMath::Matrix& inverse){
 __try{if(!valid(t))return false;CameraMath::Store(static_cast<unsigned char*>(t.source)+0x550,world);CameraMath::Store(static_cast<unsigned char*>(t.source)+0x590,inverse);return true;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void restore(){
 if(!overrideState.applied)return;CameraMath::Matrix world{},inverse{};
 if(read(target,world,inverse) && overrideState.Matches(world)){if(write(target,overrideState.neutral,overrideState.neutralInverse))++restores;else ++faults;}else ++conflicts;
 overrideState.applied=false;
}
void settings(){auto now=GetTickCount64();if(now-settingsTick<200)return;settingsTick=now;bool next=GetPrivateProfileIntW(L"Camera",L"FollowHead",0,(directory/L"PlagueTaleVR.ini").c_str())!=0;if(next!=enabled){if(!next)restore();enabled=next;log("FollowHead="+std::to_string(enabled));}}
uintptr_t update(void* source,void* entity){
 // Do not hold the mod mutex while native code runs. The original builds
 // its ordinary camera output from the untouched gameplay camera entity.
 {std::lock_guard lock(mutex);if(source==target.source && entity==target.entity){++calls;updateThread=GetCurrentThreadId();restore();}}
 const auto result=original(source,entity);
 {std::lock_guard lock(mutex);settings();if(source!=target.source || entity!=target.entity || !enabled || !referenceValid || !vrActive() || !valid(target))return result;
  Transport::Tracking tracking{};if(!readTracking(tracking))return result;
  CameraMath::Matrix world{},inverse{};if(!read(target,world,inverse)){++faults;return result;}
  const auto view=FirstPerson::ViewBase(entity,world);
  if(overrideState.Build(world,inverse,reference,tracking.head,&view) && write(target,overrideState.written,overrideState.writtenInverse)){++writes;lastPose=tracking.id;if(writes<=3)log("source head write="+std::to_string(writes)+" source="+std::to_string(reinterpret_cast<uintptr_t>(source))+" pose="+std::to_string(lastPose)+" thread="+std::to_string(updateThread));}else {overrideState.applied=false;++faults;}
 }
 return result;
}
}
bool Install(uintptr_t image,const std::filesystem::path& dir,ReadTracking reader,bool(*active)()){
 base=image;directory=dir;readTracking=reader;vrActive=active;
 const unsigned char prefix[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x81,0xec,0xa0,0,0,0};
 if(memcmp(reinterpret_cast<void*>(base+0x750580),prefix,sizeof(prefix))){log("native world update signature mismatch");return false;}
 const unsigned char getter[]={0x48,0x8b,0x91,0x08,0x01,0,0,0x48,0x85,0xd2,0x75,0x10};
 const unsigned char consumer[]={0x48,0x03,0xca,0xe8,0xd3,0xfc,0x38,0,0xf3,0x0f,0x10,0x2d,0x8f,0x34,0xfb,0};
 if(memcmp(reinterpret_cast<void*>(base+0x7555a0),getter,sizeof(getter)) || memcmp(reinterpret_cast<void*>(base+0x3c58c5),consumer,sizeof(consumer))){log("native movement camera consumer signature mismatch");return false;}
 auto status=MH_CreateHook(reinterpret_cast<void*>(base+0x750580),reinterpret_cast<void*>(update),reinterpret_cast<void**>(&original));log("native world update hook="+std::to_string(status));if(status!=MH_OK)return false;
 status=MH_CreateHook(reinterpret_cast<void*>(base+0x7555a0),reinterpret_cast<void*>(movementWorld),reinterpret_cast<void**>(&originalWorld));log("movement camera getter hook="+std::to_string(status)+" caller=3c58cd");return status==MH_OK;
}
void Observe(void* source,void* entity,void* handle,int generation,const Transport::Pose& nextReference,bool validReference){
 std::lock_guard lock(mutex);settings();++preparations;
 if(source!=target.source || entity!=target.entity || handle!=target.handle || generation!=target.generation){restore();target={source,entity,handle,generation};log("primary source="+std::to_string(reinterpret_cast<uintptr_t>(source))+" entity="+std::to_string(reinterpret_cast<uintptr_t>(entity)));}
 reference=nextReference;referenceValid=validReference;
 if(!enabled || !vrActive() || !validReference)restore();
}
CameraMath::Matrix RenderBase(void* source,const CameraMath::Matrix& actual){std::lock_guard lock(mutex);return source==target.source?overrideState.RenderBase(actual):actual;}
void Capture(uint64_t frame,uint64_t pose){
 std::lock_guard lock(mutex);std::error_code ec;auto folder=directory/L"gpu-captures"/(L"frame-"+std::to_wstring(frame));std::filesystem::create_directories(folder,ec);if(ec)return;
 std::ofstream f(folder/L"gameplay-camera.json");f<<"{\"sourceFrame\":"<<frame<<",\"renderPose\":"<<pose<<",\"enabled\":"<<(enabled?"true":"false")<<",\"applied\":"<<(overrideState.applied?"true":"false")<<",\"source\":"<<reinterpret_cast<uintptr_t>(target.source)<<",\"entity\":"<<reinterpret_cast<uintptr_t>(target.entity)<<",\"sourceUpdateCalls\":"<<calls<<",\"writes\":"<<writes<<",\"restores\":"<<restores<<",\"conflicts\":"<<conflicts<<",\"faults\":"<<faults<<",\"poseAtSourceUpdate\":"<<lastPose<<",\"updateThread\":"<<updateThread<<",\"preparations\":"<<preparations<<",\"neutralWorld\":[";
 for(int i=0;i<16;++i){if(i)f<<',';f<<overrideState.neutral.m[i];}f<<"],\"trackedWorld\":[";for(int i=0;i<16;++i){if(i)f<<',';f<<overrideState.written.m[i];}
 f<<"],\"movementReads\":"<<movementReads<<",\"movementRedirects\":"<<movementRedirects<<",\"movementFallbacks\":"<<movementFallbacks<<",\"movementThread\":"<<movementThread<<",\"movementCallerRva\":"<<0x3c58cd<<",\"lastMovementNative\":[";
 for(int i=0;i<16;++i){if(i)f<<',';f<<lastMovementNative.m[i];}f<<"],\"lastMovementBase\":[";for(int i=0;i<16;++i){if(i)f<<',';f<<lastMovementBase.m[i];}f<<"]}";
}
}
