#pragma once
#include <d3d11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <string>
#include <vector>
class StatusOverlay {
 XrSwapchain swapchain{};std::vector<XrSwapchainImageD3D11KHR> images;
 XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
 std::wstring last;uint64_t until{};bool dirty{},ready{};
 bool Create(XrSession);
public:
 static constexpr unsigned Width=1024,Height=176;
 static std::vector<unsigned char> Rasterize(const std::wstring&);
 const XrCompositionLayerBaseHeader* Layer(XrSession,XrSpace head,ID3D11DeviceContext*,const std::wstring&,uint64_t now);
 void Reset();
};
