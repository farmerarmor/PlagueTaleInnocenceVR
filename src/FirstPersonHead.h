#pragma once
#include "CameraMath.h"
#include <d3d11.h>
#include <filesystem>
namespace FirstPersonHead {
struct alignas(16) Control {float player[4],origin[4],playerRows[3][4],viewRows[3][4];};
static_assert(sizeof(Control)==128);
void Initialize(const std::filesystem::path&);
void RegisterShader(ID3D11DeviceChild*,const void*,SIZE_T);
void Pixel(ID3D11PixelShader*);
bool SharedSkin(ID3D11PixelShader*);
void Begin(uint64_t frame,bool firstPerson,const CameraMath::Matrix& player,const CameraMath::Matrix& view,bool trace);
void Eye(int);
void BeforeDraw(ID3D11DeviceContext*,UINT count=0,UINT start=0,INT base=0,bool indexed=false);
void AfterDraw(ID3D11DeviceContext*);
void End();
}
