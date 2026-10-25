#include "FirstPerson.h"
#include "FirstPersonMath.h"
#include "CameraOverride.h"
#include <windows.h>
#include <array>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
static unsigned checks{};
static void require(bool value,const char* label){++checks;if(!value){fprintf(stderr,"FAIL: %s\n",label);exit(1);}}
template<class T>static void put(void* p,size_t offset,T value){memcpy(static_cast<char*>(p)+offset,&value,sizeof(value));}
static bool close(const CameraMath::Matrix& a,const CameraMath::Matrix& b){for(unsigned i=0;i<16;++i)if(std::abs(a.m[i]-b.m[i])>1e-4f)return false;return true;}
int main(){
 std::vector<char> image(0x20000);auto base=reinterpret_cast<uintptr_t>(image.data());
 auto typed=[&](void* object,unsigned offset,const char* name){put<uint32_t>(image.data(),offset,1);put<uint32_t>(image.data(),offset+12,offset+0x40);put<uint32_t>(image.data(),offset+20,offset);strcpy_s(image.data()+offset+0x50,128,name);put<uintptr_t>(image.data(),offset+0xd8,base+offset);put<uintptr_t>(object,0,base+offset+0xe0);};
 std::vector<char> script(0x1420),level(0xa60),game(0x230),player(0x1e0),node(0x190),otherPlayer(0x1e0);std::array<std::array<char,32>,5> handles{};
 typed(script.data(),0x1000,".?AVScriptManager_G@@");typed(game.data(),0x1100,".?AVGame_Z@@");typed(player.data(),0x1200,".?AVP_AMICIA@@");typed(node.data(),0x1300,".?AVNode_Z@@");typed(otherPlayer.data(),0x1400,".?AVP_HUGO@@");
 auto handle=[&](void* owner,unsigned offset,void* value,unsigned index){auto& h=handles[index];put<int>(h.data(),8,77);put<uintptr_t>(h.data(),16,reinterpret_cast<uintptr_t>(value));put<uintptr_t>(owner,offset,reinterpret_cast<uintptr_t>(h.data()));put<int>(owner,offset+12,77);};
 handle(script.data(),0x1400,level.data(),0);handle(level.data(),0xa38,game.data(),1);handle(player.data(),0x1c8,node.data(),2);
 std::array<char,48> cameras{},players{};std::array<char,32> cameraHandle{},otherCameraHandle{};std::array<char,8> camera{},otherCamera{};
 put<int>(cameraHandle.data(),8,77);put<uintptr_t>(cameraHandle.data(),16,reinterpret_cast<uintptr_t>(camera.data()));put<int>(otherCameraHandle.data(),8,77);put<uintptr_t>(otherCameraHandle.data(),16,reinterpret_cast<uintptr_t>(otherCamera.data()));
 put<uintptr_t>(cameras.data(),16,reinterpret_cast<uintptr_t>(cameraHandle.data()));put<int>(cameras.data(),28,77);put<uintptr_t>(cameras.data(),0,reinterpret_cast<uintptr_t>(otherCameraHandle.data()));put<int>(cameras.data(),12,77);
 handle(players.data(),16,player.data(),3);handle(players.data(),0,otherPlayer.data(),4);put<int>(game.data(),0x1d0,3);put<uintptr_t>(game.data(),0x200,reinterpret_cast<uintptr_t>(players.data()));put<uintptr_t>(game.data(),0x210,reinterpret_cast<uintptr_t>(cameras.data()));
 auto root=CameraMath::Identity();root.m[12]=17;root.m[13]=2;root.m[14]=-31;put<uintptr_t>(node.data(),0x178,reinterpret_cast<uintptr_t>(&root));
 CameraMath::Matrix found{};auto query=[&](void* which){return FirstPerson::TestPlayer(base,image.size(),script.data(),which,found);};
 require(query(camera.data()) && close(root,found),"camera maps to its own player slot, not the first NPC");require(!query(otherCamera.data()),"other playable character cannot reuse Amicia anchor");require(!query(reinterpret_cast<void*>(1234)),"unregistered/cinematic camera bypass");
 put<int>(cameraHandle.data(),8,78);require(!query(camera.data()),"stale camera handle rejected");put<int>(cameraHandle.data(),8,77);
 put<int>(handles[3].data(),8,78);require(!query(camera.data()),"stale player handle rejected");put<int>(handles[3].data(),8,77);
 put<int>(handles[2].data(),8,78);require(!query(camera.data()),"stale render-node handle rejected");put<int>(handles[2].data(),8,77);
 put<int>(handles[0].data(),8,78);require(!query(camera.data()),"stale level handle rejected");put<int>(handles[0].data(),8,77);
 put<int>(game.data(),0x1d0,1000);require(!query(camera.data()),"invalid player array count rejected");put<int>(game.data(),0x1d0,3);
 put<uintptr_t>(node.data(),0x178,1);require(!query(camera.data()),"invalid transform pointer handled");put<uintptr_t>(node.data(),0x178,reinterpret_cast<uintptr_t>(&root));
 root.m[0]=std::numeric_limits<float>::quiet_NaN();require(!query(camera.data()),"non-finite root rejected");root=CameraMath::Identity();root.m[12]=17;root.m[13]=2;root.m[14]=-31;
 Transport::Pose reference{};reference.orientation.w=1;
 for(float mouseYaw:{0.f,.6f,-1.5f,3.14159265f})for(float mousePitch:{0.f,.4f,-.7f,1.5707963f})for(float headYaw:{0.f,.5f,-1.4f,3.14159265f})for(bool reflected:{false,true}){
  auto native=CameraMath::Rotation(CameraMath::Multiply(Transport::Quaternion{0,sinf(mouseYaw/2),0,cosf(mouseYaw/2)},Transport::Quaternion{sinf(mousePitch/2),0,0,cosf(mousePitch/2)}));if(reflected)for(unsigned i=0;i<3;++i)native.m[i]=-native.m[i];native.m[12]=14;native.m[13]=3;native.m[14]=-30;const auto inverse=CameraMath::InverseRigid(native);
  for(float height:{.9f,1.5f,1.6f}){
   CameraMath::Matrix view{};require(FirstPersonMath::Anchor(native,root,height,.18f,0,view),"standing/crouch-height anchor accepts native reflected orientation");for(unsigned i=0;i<12;++i)require(std::abs(view.m[i]-native.m[i])<1e-6f,"mouse aim orientation unchanged");require(std::abs(view.m[13]-root.m[13]-height)<1e-5f,"eye height follows player root");float dx=view.m[12]-root.m[12],dz=view.m[14]-root.m[14];require(std::abs(dx*dx+dz*dz-.18f*.18f)<1e-5f,"horizontal eye offset has no mouse-pitch boom");
   Transport::Pose head{};head.orientation={0,sinf(headYaw/2),0,cosf(headYaw/2)};head.position={.12f,.04f,-.08f};CameraOverride state;require(state.Build(native,inverse,reference,head,&view),"canonical camera accepts distinct visual anchor");require(close(state.neutral,native) && close(state.neutralInverse,inverse),"restoration and native aim retain original third-person camera");require(close(state.RenderBase(state.written),native),"movement/render preparation recovers native mouse camera");require(close(state.written,CameraMath::Multiply(CameraMath::Relative(reference,head,1),view)),"HMD applied exactly once to first-person eye base");require(close(CameraMath::Multiply(state.written,state.writtenInverse),CameraMath::Identity()),"first-person canonical inverse correct");
   auto movedRoot=root;movedRoot.m[12]+=.5f;CameraMath::Matrix moved{};require(FirstPersonMath::Anchor(native,movedRoot,height,.18f,0,moved),"moving player anchor valid");require(std::abs(moved.m[12]-view.m[12]-.5f)<1e-5f,"player motion follows immediately without third-person damping");
  }
 }
 CameraMath::Matrix ignored{};require(!FirstPersonMath::Anchor(root,root,9,0,0,ignored),"bad eye height rejected");require(!FirstPersonMath::Anchor(root,root,1.5f,std::numeric_limits<float>::quiet_NaN(),0,ignored),"non-finite offset rejected");printf("PASS: %u first-person checks: exact player/camera slot, stale handles and fallback, player-root eye anchor, native mouse orientation and movement, separate restoration/aim camera, single HMD transform, inverse and player movement.\n",checks);
}
