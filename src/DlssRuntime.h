#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include "nvsdk_ngx.h"
#include "DlssModes.h"

// One runtime in the game device, with independent feature/history per eye.
class DlssRuntime {
 struct Eye {NVSDK_NGX_Handle* feature{};NVSDK_NGX_Parameter* params{};UINT width{},height{},outWidth{},outHeight{};uint64_t evaluations{};DlssModes::Mode mode=DlssModes::Off;Microsoft::WRL::ComPtr<ID3D11Resource> color,depth,motion,output;};
 struct CachedPair {Eye eyes[2];};
 Microsoft::WRL::ComPtr<ID3D11Device> device;
 NVSDK_NGX_Parameter* capabilities{};Eye eyes[2];bool initialized{};
 std::vector<CachedPair> cache;
 std::filesystem::path directory;
 DlssModes::Mode mode=DlssModes::Quality;
 void Log(const char*,NVSDK_NGX_Result);
 bool PrepareEyes(ID3D11DeviceContext*,UINT width,UINT height,UINT outputWidth,UINT outputHeight);
public:
 bool Initialize(ID3D11Device*,const std::filesystem::path&);
 bool Quality(UINT outputWidth,UINT outputHeight,UINT& inputWidth,UINT& inputHeight);
 void SelectMode(DlssModes::Mode value){mode=DlssModes::Configured(true,value);}
 // The caller must first complete all GPU work using either eye.
 bool RetireEyes();
 // Suspend a completed pair without releasing NGX histories or their last
 // referenced resources. Mode switches can reuse it with a history reset.
 bool CacheEyes();
 bool Evaluate(ID3D11DeviceContext*,int eye,ID3D11Resource* color,ID3D11Resource* depth,ID3D11Resource* motion,ID3D11Resource* output,UINT width,UINT height,UINT outWidth,UINT outHeight,float jitterX,float jitterY,bool reset);
 void Shutdown();
};
