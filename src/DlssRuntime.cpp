#include "DlssRuntime.h"
#include "nvsdk_ngx_helpers_d3d.h"
#include <fstream>
static NVSDK_NGX_PerfQuality_Value ngxQuality(DlssModes::Mode mode){switch(mode){case DlssModes::Balanced:return NVSDK_NGX_PerfQuality_Value_Balanced;case DlssModes::Performance:return NVSDK_NGX_PerfQuality_Value_MaxPerf;case DlssModes::UltraPerformance:return NVSDK_NGX_PerfQuality_Value_UltraPerformance;default:return NVSDK_NGX_PerfQuality_Value_MaxQuality;}}
void DlssRuntime::Log(const char* name,NVSDK_NGX_Result result){std::ofstream f(directory/L"dlss-runtime.log",std::ios::app);f<<name<<" result=0x"<<std::hex<<unsigned(result)<<'\n';}
bool DlssRuntime::Initialize(ID3D11Device* d,const std::filesystem::path& path){
 if(initialized)return device.Get()==d;
 directory=path;const auto logs=directory/L"ngx-logs";std::error_code ec;std::filesystem::create_directories(logs,ec);if(ec)return false;
 const wchar_t* paths[]={directory.c_str()};NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo={paths,1};
 auto result=NVSDK_NGX_D3D11_Init_with_ProjectID("9d2ebc51-02f7-4c4e-95aa-0124c7f345c2",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"PlagueTaleVR-1",logs.c_str(),d,&info);Log("D3D11 Initialize",result);if(NVSDK_NGX_FAILED(result))return false;
 device=d;initialized=true;result=NVSDK_NGX_D3D11_GetCapabilityParameters(&capabilities);Log("Capabilities",result);
 int available{};if(NVSDK_NGX_FAILED(result) || NVSDK_NGX_FAILED(capabilities->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&available)) || !available){Shutdown();return false;}
 return true;
}
bool DlssRuntime::Quality(UINT ow,UINT oh,UINT& w,UINT& h){
 if(!initialized || !capabilities)return false;
 capabilities->Set(NVSDK_NGX_Parameter_Width,ow);capabilities->Set(NVSDK_NGX_Parameter_Height,oh);capabilities->Set(NVSDK_NGX_Parameter_PerfQualityValue,int(ngxQuality(mode)));void* callback{};capabilities->Get(NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback,&callback);if(!callback)return false;
 if(NVSDK_NGX_FAILED(reinterpret_cast<PFN_NVSDK_NGX_DLSS_GetOptimalSettingsCallback>(callback)(capabilities)))return false;
 return NVSDK_NGX_SUCCEED(capabilities->Get(NVSDK_NGX_Parameter_OutWidth,&w)) && NVSDK_NGX_SUCCEED(capabilities->Get(NVSDK_NGX_Parameter_OutHeight,&h)) && w && h;
}
bool DlssRuntime::PrepareEyes(ID3D11DeviceContext* c,UINT w,UINT h,UINT ow,UINT oh){
 // Never destroy live histories from an eye draw. Mode/dimension changes
 // retire the entire pair at the caller's completed transition fence.
 for(auto& e:eyes)if(e.feature && (e.mode!=mode || e.width!=w || e.height!=h || e.outWidth!=ow || e.outHeight!=oh)){Log("unretired eye dimensions or mode rejected",NVSDK_NGX_Result_FAIL_InvalidParameter);return false;}
 if(!eyes[0].feature && !eyes[1].feature){
  for(auto entry=cache.begin();entry!=cache.end();++entry){const auto& e=entry->eyes[0];
   if(e.mode==mode && e.width==w && e.height==h && e.outWidth==ow && e.outHeight==oh){
    for(unsigned i=0;i<2;++i){eyes[i]=std::move(entry->eyes[i]);eyes[i].evaluations=0;}
    cache.erase(entry);Log("Reuse completed cached eye pair with history reset",NVSDK_NGX_Result_Success);break;
   }
  }
 }
 for(unsigned eye=0;eye<2;++eye){auto& e=eyes[eye];
 if(!e.feature){
  auto result=NVSDK_NGX_D3D11_AllocateParameters(&e.params);Log("Allocate eye parameters",result);if(NVSDK_NGX_FAILED(result))return false;
  e.params->Set(NVSDK_NGX_Parameter_FreeMemOnReleaseFeature,1);
  for(const char* key:{NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance})e.params->Set(key,NVSDK_NGX_DLSS_Hint_Render_Preset_K);
  NVSDK_NGX_DLSS_Create_Params create{};create.Feature={w,h,ow,oh,ngxQuality(mode)};
  create.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_MVJittered|NVSDK_NGX_DLSS_Feature_Flags_DepthInverted|NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
  result=NGX_D3D11_CREATE_DLSS_EXT(c,&e.feature,e.params,&create);Log(eye?"Create right eye":"Create left eye",result);
  if(NVSDK_NGX_FAILED(result)){NVSDK_NGX_D3D11_DestroyParameters(e.params);e={};return false;}
  e.width=w;e.height=h;e.outWidth=ow;e.outHeight=oh;e.mode=mode;
  std::ofstream f(directory/L"dlss-runtime.log",std::ios::app);f<<"eye="<<eye<<" feature="<<e.feature<<" parameters="<<e.params<<" mode="<<DlssModes::Name(mode)<<" input="<<w<<'x'<<h<<" output="<<ow<<'x'<<oh<<" freeOnRelease=1\n";
 }
 }
 if(eyes[0].feature==eyes[1].feature || eyes[0].params==eyes[1].params){Log("eye feature or parameter alias rejected",NVSDK_NGX_Result_FAIL_InvalidParameter);return false;}return true;
}
bool DlssRuntime::RetireEyes(){
 bool good=true;
 // Release both handles before allocating either replacement. NGX normally
 // retains feature memory in a shared cache; request actual disposal instead.
 for(unsigned eye=0;eye<2;++eye){auto& e=eyes[eye];if(e.feature){auto result=NVSDK_NGX_D3D11_ReleaseFeature(e.feature);Log(eye?"Retire right eye":"Retire left eye",result);good=good && NVSDK_NGX_SUCCEED(result);e.feature=nullptr;}}
 for(auto& e:eyes){if(e.params)NVSDK_NGX_D3D11_DestroyParameters(e.params);e={};}
 return good;
}
bool DlssRuntime::CacheEyes(){
 if(!eyes[0].feature && !eyes[1].feature)return true;
 if(!eyes[0].feature || !eyes[1].feature || cache.size()>=8){Log("incomplete or full eye cache rejected",NVSDK_NGX_Result_FAIL_InvalidParameter);return false;}
 CachedPair pair;for(unsigned i=0;i<2;++i){pair.eyes[i]=std::move(eyes[i]);eyes[i]={};}
 cache.push_back(std::move(pair));Log("Cache GPU-completed eye pair and referenced resources",NVSDK_NGX_Result_Success);return true;
}
bool DlssRuntime::Evaluate(ID3D11DeviceContext* c,int eye,ID3D11Resource* color,ID3D11Resource* depth,ID3D11Resource* motion,ID3D11Resource* output,UINT w,UINT h,UINT ow,UINT oh,float jx,float jy,bool reset){
 if(!initialized || !c || eye<0 || eye>1 || !color || !depth || !motion || !output || !w || !h || !ow || !oh)return false;
 if(!PrepareEyes(c,w,h,ow,oh))return false;auto& e=eyes[eye];reset=reset || !e.evaluations;
 // NGX parameters contain resource pointers. Keep these alive with suspended
 // features as well as during the active mode; later Evaluate replaces them.
 e.color=color;e.depth=depth;e.motion=motion;e.output=output;
 NVSDK_NGX_D3D11_DLSS_Eval_Params eval{};eval.Feature.pInColor=color;eval.Feature.pInOutput=output;eval.pInDepth=depth;eval.pInMotionVectors=motion;eval.InJitterOffsetX=jx;eval.InJitterOffsetY=jy;eval.InRenderSubrectDimensions={w,h};eval.InMVScaleX=eval.InMVScaleY=1;eval.InReset=reset?1:0;eval.InPreExposure=eval.InExposureScale=1;eval.InFrameTimeDeltaInMsec=11.111f;
 auto result=NGX_D3D11_EVALUATE_DLSS_EXT(c,e.feature,e.params,&eval);if(NVSDK_NGX_FAILED(result))Log("Evaluate",result);else ++e.evaluations;return NVSDK_NGX_SUCCEED(result);
}
void DlssRuntime::Shutdown(){
 RetireEyes();
 for(auto& pair:cache){for(unsigned i=0;i<2;++i){eyes[i]=std::move(pair.eyes[i]);pair.eyes[i]={};}RetireEyes();}cache.clear();
 if(capabilities)NVSDK_NGX_D3D11_DestroyParameters(capabilities);capabilities=nullptr;
 if(initialized)NVSDK_NGX_D3D11_Shutdown1(device.Get());initialized=false;device.Reset();
}
