#pragma once
#include "CameraMath.h"
#include <filesystem>
namespace FirstPerson {
bool Initialize(uintptr_t,const std::filesystem::path&);
CameraMath::Matrix ViewBase(void* cameraNode,const CameraMath::Matrix& native,CameraMath::Matrix* playerWorld=nullptr);
void Capture(uint64_t frame,uint64_t pose);
#ifdef PLAGUE_FIRST_PERSON_TEST_ACCESS
bool TestPlayer(uintptr_t image,uintptr_t size,void* script,void* cameraNode,CameraMath::Matrix& root);
#endif
}
