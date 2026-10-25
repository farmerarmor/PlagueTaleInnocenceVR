#pragma once
#include <d3d11.h>
#include <filesystem>
#include <cstdint>
namespace ProjectedLightBounds {
void Initialize(const std::filesystem::path&);
void RegisterShader(ID3D11DeviceChild*,const void*,SIZE_T);
void Begin(uint64_t,bool,bool);
void Eye(int);
void BeforeDraw(ID3D11DeviceContext*);
void AfterDraw(ID3D11DeviceContext*);
void End();
}
