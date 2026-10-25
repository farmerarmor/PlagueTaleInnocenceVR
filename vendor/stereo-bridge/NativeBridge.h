#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>
#include <string>
#include "SharedPair.h"
#include "DisplaySettings.h"
namespace NativeBridge {
// Source must be the current native HD3D back buffer: L at y=0, R at y=eyeHeight.
void Present(IDXGISwapChain* chain, ID3D11Texture2D* nativePair, UINT eyeHeight, bool swapEyes);
void UseSwapchain(IDXGISwapChain* chain);
void Shutdown(); // Outside DllMain, on render thread only.
uint64_t SubmittedPairs();
void SetSourceFrame(uint64_t frame);
void SetTrackingChannel(Transport::Header* shared);
void SetRenderInfo(const Transport::RenderInfo& info);
void SetStatusText(const std::wstring& text);
void RequestCapture(); // One matched pair and its rendered-pose metadata.
bool ValidatePair(const D3D11_TEXTURE2D_DESC& desc, UINT eyeHeight);
bool QueryDisplaySettings(HeadsetDisplay::Settings& settings);
bool ReadTracking(Transport::Tracking& tracking);
struct UiSurface {D3D11_VIEWPORT viewport{};UINT width{},height{},nativeEyeHeight{};uint32_t nativePair{};uintptr_t resource{};};
// Read-only render-thread query; recognizes only the source submitted at Present.
bool ReadUiSurface(UiSurface& surface);
}
