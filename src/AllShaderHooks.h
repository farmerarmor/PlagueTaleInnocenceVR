#pragma once
#include <d3d11.h>
#include <filesystem>
namespace AllShaderHooks {
bool Install(ID3D11Device*,ID3D11DeviceContext*,const std::filesystem::path&);
}
