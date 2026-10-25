#pragma once
#include <d3d11.h>
#include <filesystem>
#include <cstdint>
namespace LightList {
void Initialize(const std::filesystem::path&);
void RegisterShader(ID3D11DeviceChild*,const void*,SIZE_T);
void Compute(ID3D11ComputeShader*);
void Begin(uint64_t,bool tracked,bool capture);
void Eye(int);
void BeforeDispatch(ID3D11DeviceContext*,UINT x,UINT y,UINT z);
void AfterDispatch(ID3D11DeviceContext*);
void End();
}
