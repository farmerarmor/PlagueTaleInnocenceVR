#pragma once
#include <d3d11.h>
#include <filesystem>
#include <cstdint>
namespace EffectHistory {
void Initialize(const std::filesystem::path&,uintptr_t base);
void RegisterShader(ID3D11DeviceChild*,const void*,SIZE_T);
void Pixel(ID3D11PixelShader*);
void Compute(ID3D11ComputeShader*);
void Begin(uint64_t frame,bool tracked,bool enabled,bool trace,bool reflectionProbesOff);
void Eye(int);
void BeforeDraw(ID3D11DeviceContext*);
void AfterDraw(ID3D11DeviceContext*);
void BeforeDispatch(ID3D11DeviceContext*);
void AfterDispatch(ID3D11DeviceContext*,bool executed=true);
void Copied(ID3D11Resource* destination,ID3D11Resource* source,uintptr_t caller,bool entire);
void EndEye(ID3D11DeviceContext*,int eye);
void End();
}
