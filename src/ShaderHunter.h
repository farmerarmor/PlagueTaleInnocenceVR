#pragma once
#include <d3d11.h>
#include <filesystem>
#include <cstdint>
namespace ShaderHunter {
bool Enabled();
void Initialize(const std::filesystem::path&);
void RegisterShader(ID3D11DeviceChild*,const void*,SIZE_T,UINT stage);
void Vertex(ID3D11VertexShader*);
void Pixel(ID3D11PixelShader*);
void Compute(ID3D11ComputeShader*);
void Begin(uint64_t,uint64_t,bool);
bool CaptureRequested();
void Eye(int);
bool Draw(ID3D11DeviceContext*,UINT,const char*);
bool Dispatch(ID3D11DeviceContext*);
void End();
}
