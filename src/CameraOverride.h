#pragma once
#include "CameraMath.h"
// Native mouse base and written HMD output stay separate. This same source
// feeds gameplay visibility and later render preparation without feedback.
struct CameraOverride {
 CameraMath::Matrix neutral{},neutralInverse{},written{},writtenInverse{};
 bool applied{};
 bool Matches(const CameraMath::Matrix& value)const{
  if(!applied)return false;for(int i=0;i<16;++i)if(!std::isfinite(value.m[i]) || std::abs(value.m[i]-written.m[i])>.0001f)return false;return true;
 }
 bool Build(const CameraMath::Matrix& base,const CameraMath::Matrix& inverse,const Transport::Pose& reference,const Transport::Pose& head,const CameraMath::Matrix* viewBase=nullptr){
  if(!CameraMath::Rigid(base) || !CameraMath::Rigid(inverse))return false;
  if(viewBase && !CameraMath::Rigid(*viewBase))return false;
  neutral=base;neutralInverse=inverse;written=CameraMath::Multiply(CameraMath::Relative(reference,head,1.f),viewBase?*viewBase:base);
  if(!CameraMath::Rigid(written))return false;writtenInverse=CameraMath::InverseRigid(written);applied=true;return true;
 }
 CameraMath::Matrix RenderBase(const CameraMath::Matrix& actual)const{return Matches(actual)?neutral:actual;}
};
