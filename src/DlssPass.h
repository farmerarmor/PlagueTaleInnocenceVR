#pragma once
#include <d3d11.h>
#include <filesystem>
#include "CameraMath.h"
namespace DlssPass {
void Initialize(const std::filesystem::path&,uintptr_t gameBase);
bool Busy();
void Cycle();
void Prepare(void* renderer,ID3D11DeviceContext*,bool tracked,UINT outputWidth,UINT outputHeight,bool startupReady=true);
void Begin(uint64_t frame,bool tracked,bool capture);
void Eye(int);
void Projection(CameraMath::Matrix& current,CameraMath::Matrix& previous,const CameraMath::Matrix& nativeCurrent,const CameraMath::Matrix& nativePrevious);
void BeforeDraw(ID3D11DeviceContext*);
void AfterDraw(ID3D11DeviceContext*);
void End();
void ResetHistory();
const char* Status();
bool Effective();
bool CaptureRequested();
void FlushInputAudit();
// Orderly teardown for the standalone GPU validation process. Never call from
// DllMain or while the native game is rendering.
void Shutdown();
#ifdef PLAGUE_DLSS_TEST_ACCESS
// 0 private HDR input, 1 raw SDK output, 2 separate native-draw input.
ID3D11Texture2D* TestTexture(int eye,unsigned role);
#endif
}
