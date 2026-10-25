#include "CameraOverride.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
static unsigned checks{};
static void require(bool good,const char* text){++checks;if(!good){fprintf(stderr,"FAIL: %s\n",text);exit(1);}}
static bool matricesAgree(const CameraMath::Matrix& a,const CameraMath::Matrix& b,float tolerance=.0001f){for(int i=0;i<16;++i)if(std::abs(a.m[i]-b.m[i])>tolerance)return false;return true;}
// Exact scalar heading/input conversion consumed at game RVA 3c58cd:
// native forward (0,0,1), horizontal normalization, then native input X/Z.
static Transport::Vector nativeMovement(const CameraMath::Matrix& world,float x,float z){
 float fx=world.m[8],fz=world.m[10],length=std::sqrt(fx*fx+fz*fz);if(length>1e-6f){fx/=length;fz/=length;}
 return {fx*z-fz*x,0,fx*x+fz*z};
}
static bool vectorsAgree(Transport::Vector a,Transport::Vector b){return std::abs(a.x-b.x)<.0001f && std::abs(a.y-b.y)<.0001f && std::abs(a.z-b.z)<.0001f;}
int main(){
 Transport::Pose reference{};reference.orientation.w=1;
 for(float yaw:{0.f,.65f,-1.4f})for(bool reflected:{false,true}){
  auto base=CameraMath::Rotation({0,.3f,0,std::sqrt(.91f)});base.m[12]=17;base.m[13]=2;base.m[14]=-31;if(reflected)for(int i=0;i<3;++i)base.m[i]=-base.m[i];auto inverse=CameraMath::InverseRigid(base);
  Transport::Pose head{};head.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};head.position={.2f,.08f,-.1f};
  CameraOverride state;require(state.Build(base,inverse,reference,head),"native handedness and pose accepted");require(state.Matches(state.written),"written canonical pose recognized");require(matricesAgree(CameraMath::Multiply(state.written,state.writtenInverse),CameraMath::Identity()),"canonical world and inverse agree");
  auto renderBase=state.RenderBase(state.written);require(matricesAgree(renderBase,base),"render preparation recovers untouched mouse base");auto rendered=CameraMath::Multiply(CameraMath::Relative(reference,head,1.f),renderBase);require(matricesAgree(rendered,state.written),"HMD transform applied once when early canonical pose already written");
  auto nativeNext=base;nativeNext.m[12]+=.4f;require(!state.Matches(nativeNext),"new native output not mistaken for mod output");require(matricesAgree(state.RenderBase(nativeNext),nativeNext),"native camera changes preserved for render");
  for(int tick=0;tick<120;++tick){require(state.Build(base,inverse,reference,head),"repeated native update remains valid");require(matricesAgree(state.written,rendered),"no accumulated head rotation or translation feedback");}
  auto nan=state.written;nan.m[0]=std::numeric_limits<float>::quiet_NaN();require(!state.Matches(nan),"invalid source cannot pass restoration ownership check");
  state.applied=false;require(matricesAgree(state.RenderBase(base),base),"disabled source uses original native matrix");
 }
 constexpr float pi=3.14159265359f;
 for(float mouseYaw:{0.f,pi*.5f,-pi*.5f,pi})for(float headYaw:{0.f,pi*.5f,-pi*.5f,pi})for(float pitch:{0.f,pi*.494f,-pi*.494f})for(bool reflected:{false,true}){
  auto base=CameraMath::Rotation({0,std::sin(mouseYaw*.5f),0,std::cos(mouseYaw*.5f)});if(reflected)for(int i=0;i<3;++i)base.m[i]=-base.m[i];base.m[12]=120;base.m[13]=7;base.m[14]=-26;
  Transport::Pose head{};head.orientation=CameraMath::Multiply(Transport::Quaternion{0,std::sin(headYaw*.5f),0,std::cos(headYaw*.5f)},Transport::Quaternion{std::sin(pitch*.5f),0,0,std::cos(pitch*.5f)});head.position={.2f,.05f,-.1f};
  CameraOverride state;require(state.Build(base,CameraMath::InverseRigid(base),reference,head),"independent locomotion fixture is valid");
  const auto canonical=state.written;auto movementBase=state.RenderBase(canonical);
  for(auto input:{Transport::Vector{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{.70710678f,0,.70710678f}}){
   auto wanted=nativeMovement(base,input.x,input.z),actual=nativeMovement(movementBase,input.x,input.z);require(vectorsAgree(actual,wanted),"movement follows mouse for north/east/west/180 HMD headings and steep pitch");
  }
  require(matricesAgree(state.written,canonical),"movement read leaves canonical HMD camera intact for visibility");
  auto newNative=base;newNative.m[12]+=1;require(matricesAgree(state.RenderBase(newNative),newNative),"unowned source update cannot return stale movement base");
 }
 printf("PASS: %u camera checks: source ownership, single tracking, no feedback, and mouse-based movement independent of HMD yaw/pitch.\n",checks);return 0;
}
