#pragma once
#include <d3d11_1.h>
#include <filesystem>
namespace DlssImageAudit {
void Initialize(const std::filesystem::path&);
void Capture(ID3D11DeviceContext1*,uint64_t frame,int eye,ID3D11Texture2D* input,ID3D11Texture2D* dlss,ID3D11Texture2D* nativeOutput,const float scales[4],int aaType);
void Flush(ID3D11DeviceContext1*);
void Shutdown();
}
