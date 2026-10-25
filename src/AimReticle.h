#pragma once
#include <d3d11.h>
#include <filesystem>
#include "CameraMath.h"

namespace AimReticle {
void Initialize(const std::filesystem::path&);
void RegisterShader(ID3D11DeviceChild*,const void*,SIZE_T);
void Vertex(ID3D11VertexShader*);
void Pixel(ID3D11PixelShader*);
void Begin(uint64_t,const Transport::Tracking&,const CameraMath::Matrix* nativeWorld,const CameraMath::Matrix* headWorld,bool capture);
void Eye(int);
void BeforeDraw(ID3D11DeviceContext*,UINT vertexCount,bool indexed=false);
void AfterDraw(ID3D11DeviceContext*);
void End();
}
