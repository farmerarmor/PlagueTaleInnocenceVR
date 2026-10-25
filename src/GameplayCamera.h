#pragma once
#include "CameraMath.h"
#include <filesystem>
namespace GameplayCamera {
using ReadTracking=bool(*)(Transport::Tracking&);
bool Install(uintptr_t,const std::filesystem::path&,ReadTracking,bool(*vrActive)());
void Observe(void* source,void* entity,void* handle,int generation,const Transport::Pose& reference,bool referenceValid);
CameraMath::Matrix RenderBase(void*,const CameraMath::Matrix&);
void Capture(uint64_t frame,uint64_t pose);
}
