#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
class StereoMirror {
public:
 bool Draw(IDXGISwapChain*,ID3D11Texture2D*,ID3D11Texture2D*);
private:
 bool Initialize(ID3D11Device*);
 Microsoft::WRL::ComPtr<ID3D11Device> device;
 Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;
 Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
 Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
 Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;
 Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;
};
