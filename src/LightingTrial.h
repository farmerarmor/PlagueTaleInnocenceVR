#pragma once
#include <d3d11.h>
#include <filesystem>
#include <cstdint>
namespace LightingTrial {
void Initialize(const std::filesystem::path&);
void RegisterShader(ID3D11DeviceChild*,const void*,SIZE_T);
void Pixel(ID3D11PixelShader*);
void Begin(uint64_t,bool,bool);
void Eye(int);
bool BeforeDraw(ID3D11DeviceContext*);
void AfterDraw(ID3D11DeviceContext*);
void End();
}
