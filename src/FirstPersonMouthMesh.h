#pragma once
#include <d3d11.h>
#include <cstdint>
#include <string>
namespace FirstPersonMouthMesh {
void Reset();
// Only the captured oral vertex/index contents qualify. The first encounter
// queues a small asynchronous identity check and leaves the native draw on.
bool Match(ID3D11DeviceContext*,uint64_t frame,UINT count,UINT start,INT base,bool indexed);
std::string Status();
}
