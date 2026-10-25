#pragma once
#include <d3d11.h>
#include <filesystem>
#include <cstdint>
namespace DecalPass {
void Initialize(const std::filesystem::path&);
void RegisterShader(ID3D11DeviceChild*,const void*,SIZE_T);
void Pixel(ID3D11PixelShader*);
void Benchmark();
void Begin(uint64_t,bool,bool,ID3D11DeviceContext*,bool hunter);
void Eye(int);
bool BeforeDraw(ID3D11DeviceContext*);
void End(ID3D11DeviceContext*);
}
