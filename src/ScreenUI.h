#pragma once
#include <d3d11.h>
#include <filesystem>
#include "SharedPair.h"
namespace ScreenUI {
void Initialize(const std::filesystem::path&);
void RegisterShader(ID3D11DeviceChild*,const void*,SIZE_T);
void Vertex(ID3D11VertexShader*);
void Pixel(ID3D11PixelShader*);
void Begin(uint64_t,const Transport::Tracking&,bool);
void Eye(int);
void BeforeDraw(ID3D11DeviceContext*);
void AfterDraw(ID3D11DeviceContext*);
void End();
}
