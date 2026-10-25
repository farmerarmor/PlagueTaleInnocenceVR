#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers_d3d.h"
#include <filesystem>
#include <cstdio>
using Microsoft::WRL::ComPtr;
static bool result(NVSDK_NGX_Result value,const char* operation){printf("%s: 0x%08x\n",operation,unsigned(value));return NVSDK_NGX_SUCCEED(value);}
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 1;
 std::filesystem::path dlls=std::filesystem::absolute(argv[1]),logs=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(logs);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))return 2;
 const wchar_t* paths[]={dlls.c_str()};NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo={paths,1};
 if(!result(NVSDK_NGX_D3D11_Init_with_ProjectID("9d2ebc51-02f7-4c4e-95aa-0124c7f345c2",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"PlagueTaleVR-1",logs.c_str(),device.Get(),&info),"NGX D3D11 project initialization"))return 3;
 NVSDK_NGX_Parameter* params{};if(!result(NVSDK_NGX_D3D11_GetCapabilityParameters(&params),"capability parameters"))return 4;
 int available{},driver{};params->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&available);params->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,&driver);printf("SuperResolution available=%d needsDriver=%d\n",available,driver);
 if(!available){NVSDK_NGX_D3D11_DestroyParameters(params);NVSDK_NGX_D3D11_Shutdown1(device.Get());return 5;}
 params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,NVSDK_NGX_DLSS_Hint_Render_Preset_K);
 params->Set(NVSDK_NGX_Parameter_Width,4000u);params->Set(NVSDK_NGX_Parameter_Height,4768u);params->Set(NVSDK_NGX_Parameter_PerfQualityValue,int(NVSDK_NGX_PerfQuality_Value_MaxQuality));
 void* cb{};params->Get(NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback,&cb);if(!cb)return 6;
 if(!result(reinterpret_cast<PFN_NVSDK_NGX_DLSS_GetOptimalSettingsCallback>(cb)(params),"optimal settings"))return 7;
 unsigned width{},height{};params->Get(NVSDK_NGX_Parameter_OutWidth,&width);params->Get(NVSDK_NGX_Parameter_OutHeight,&height);printf("Quality input=%ux%u output=4000x4768\n",width,height);
 NVSDK_NGX_DLSS_Create_Params create{};create.Feature={width,height,4000,4768,NVSDK_NGX_PerfQuality_Value_MaxQuality};create.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted|NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
 NVSDK_NGX_Handle* eyes[2]{};bool valid=true;
 for(unsigned eye=0;eye<2;++eye)valid=result(NGX_D3D11_CREATE_DLSS_EXT(context.Get(),&eyes[eye],params,&create),eye?"right-eye feature creation":"left-eye feature creation")&&valid;
 for(auto* eye:eyes)if(eye)NVSDK_NGX_D3D11_ReleaseFeature(eye);
 NVSDK_NGX_D3D11_DestroyParameters(params);NVSDK_NGX_D3D11_Shutdown1(device.Get());
 printf("%s: standalone NVIDIA DLSS support and distinct eye instances; no game input/output validation.\n",valid?"PASS":"FAIL");return valid?0:8;
}
