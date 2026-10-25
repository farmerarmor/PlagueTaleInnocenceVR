#pragma once
#include "CameraMath.h"
namespace FirstPersonMath {
inline bool Anchor(const CameraMath::Matrix& native,const CameraMath::Matrix& player,float height,float forward,float side,CameraMath::Matrix& result){
 if(!CameraMath::Rigid(native) || !CameraMath::Rigid(player) || !std::isfinite(height) || height<.4f || height>2.2f || !std::isfinite(forward) || std::abs(forward)>.5f || !std::isfinite(side) || std::abs(side)>.3f)return false;
 // Keep mouse/controller orientation. Offset the eye before applying HMD
 // rotation/lean, so turning the head never swings the player anchor around.
 float fx=native.m[8],fz=native.m[10],length=std::sqrt(fx*fx+fz*fz);
 if(length<.01f){fx=player.m[8];fz=player.m[10];length=std::sqrt(fx*fx+fz*fz);}
 if(length<.01f)return false;fx/=length;fz/=length;
 result=native;result.m[12]=player.m[12]+forward*fx+side*fz;
 result.m[13]=player.m[13]+height;result.m[14]=player.m[14]+forward*fz-side*fx;
 return CameraMath::Rigid(result);
}
}
