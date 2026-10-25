#pragma once
#include <cstdint>
#include "CameraMath.h"
namespace MenuScreenPolicy {
inline constexpr uint32_t Mode=3;
inline constexpr float Aspect=16.f/9.f;
inline unsigned SourceEye(uint32_t mode,unsigned eye,bool swap){return mode==Mode?0u:swap?1u-eye:eye;}
inline bool Screen(uint32_t mode){return mode==0 || mode==Mode;}
inline unsigned LayerCount(uint32_t mode){return mode==Mode?1u:mode==0?2u:0u;}
inline Transport::Pose Anchor(const Transport::Pose& head){
 auto anchor=CameraMath::LevelReference(head);const auto forward=CameraMath::Rotate(anchor.orientation,{0,0,-2});
 anchor.position.x+=forward.x;anchor.position.y+=forward.y;anchor.position.z+=forward.z;return anchor;
}
// An empty focus-stack interval during a menu transition must not flash VR.
struct Gate {
 bool active{};uint64_t lastMenu{};
 bool Update(bool valid,bool menu,uint64_t now){
  if(valid && menu){active=true;lastMenu=now;}
  else if(active && now>=lastMenu && now-lastMenu>=(valid?120u:500u))active=false;
  return active;
 }
};
}
