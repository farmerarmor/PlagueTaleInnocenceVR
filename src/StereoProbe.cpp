#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <intrin.h>
#include "PairProducer.h"
#include "StereoMirror.h"
#include "GpuCapture.h"
#include "EffectHistory.h"
#include "ScreenUI.h"
#include "MenuScreen.h"
#include "FirstPerson.h"
#include "FirstPersonHead.h"
#include "DlssStartupGate.h"
#include "MenuScreenPolicy.h"
#include "AimReticle.h"
#include "DlssPass.h"
#include "LightingTrial.h"
#include "ShaderHunter.h"
#include "LightList.h"
#include "ProjectedLightBounds.h"
#include "GameplayCamera.h"
#include "DiagnosticControls.h"
#include "DecalPass.h"
#include "CameraMath.h"
#include <DirectXPackedVector.h>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static uintptr_t base;
static std::filesystem::path dir;
static std::mutex logMutex;
static std::atomic<int> mode{0}, capture{0};
static std::atomic<int> effectsMode{0};
static std::atomic<bool> zeroAuxiliaryStereo{false};
static std::atomic<uint64_t> activeEffectMask{0};
static volatile LONG64* nativeForceOff{};
static uint64_t originalEffectBits{};
static constexpr LONG64 occlusionBit=0x4000;
struct NativeEffect {const wchar_t* key;uintptr_t maskRva;uint64_t mask;int defaultOff;};
static constexpr NativeEffect nativeEffects[]={
 {L"DisableScreenSpaceReflections",0xfc49c0,0x800,1},
 {L"DisableContactShadows",0xfc4a90,0x40000000,0},
 {L"DisableAmbientOcclusion",0xfc49a0,0x100,0},
 {L"DisableVolumetricLights",0xfc4a10,0x10000,0},
 {L"DisableVolumetricClouds",0xfc4a20,0x20000,0},
 {L"DisableReflectionProbes",0xfc49e0,0x2000,0},
 {L"DisableDepthOfField",0xfc4a30,0x1000000,1},
 {L"DisableBloom",0xfc4a40,0x2000000,1},
 {L"DisableChromaticAberration",0xfc4a50,0x4000000,1},
 {L"DisableMotionBlur",0xfc4990,0x40,1},
};
static unsigned headsetWidth{},headsetHeight{};
static float* minimumAspect{};
static float* maximumAspect{};
static bool aspectConfigured{};
using ResizeFn=void(*)(void*,int,int);
static ResizeFn nativeResize{};
static uint64_t frame=0;
static thread_local uint64_t sourceFrame=0;
static thread_local int eyeCalls=0;
static thread_local bool captureFrame=false;
static thread_local bool traceFrame=false;
static thread_local unsigned projectionCaptures[2]{};
static std::atomic<int> captureDesktop{0};
static ComPtr<ID3D11Device> device;
static ComPtr<ID3D11DeviceContext> context;
static std::mutex gpuMutex;
struct EyeSlot {
 std::atomic<int> state{0}; // free, writing, pending, consuming
 ComPtr<ID3D11Texture2D> eyes[3][2];
 ComPtr<ID3D11Query> complete;
 ComPtr<ID3D11DeviceContext> submitContext;
 uint64_t frame{};unsigned mask{};bool capture{};
 Transport::RenderInfo rendered{};
 unsigned trackedMask{};
};
static EyeSlot slots[3];
static thread_local EyeSlot* writingSlot{};
static PairProducer producer;
static std::atomic<bool> producerInitialized{false};
using BoundaryFn=int(*)(void*);
static BoundaryFn originalBoundary;
using FrameFn=void(*)(void*);
static FrameFn originalFrame;
static StereoMirror mirror;
static ComPtr<ID3D11Texture2D> mirrorEyes[2];
using ProjectionFn=void(*)(void*,void*,void*,void*,bool);
static ProjectionFn originalProjection;
struct NativeCameraSnapshot {
 void* root{};void* view{};uint8_t viewBytes[0x160]{};
 void* projections[8]{};uint8_t projectionBytes[8][0x2b0]{};unsigned count{};
 CameraMath::Matrix gameView{};
 bool valid{};
};
static thread_local NativeCameraSnapshot camera;
static thread_local Transport::Tracking renderTracking{};
static thread_local CameraMath::Matrix renderNativeWorld{},renderHeadWorld{},renderPlayerWorld{};
static thread_local bool renderAimCameraValid{};
static thread_local bool renderMenuScreen{};
static Transport::Pose trackingReference{};
static bool referenceValid{};
using PreparationFn=void(*)(void*);
using SourceCameraFn=void*(*)(void*,void*);
using ViewFn=void(*)(void*,void*,void*,uint32_t);
static PreparationFn originalPreparation;
static SourceCameraFn originalSourceCamera;
static ViewFn originalView;
struct PreparedCamera {
 void* view{};Transport::Tracking tracking{};CameraMath::Matrix neutralWorld{},headWorld{},playerWorld{};
};
static std::mutex preparedMutex;
static PreparedCamera preparedCameras[64];
static unsigned preparedCursor{};
struct Preparation {
 void* renderer{};void* descriptor{};void* source{};
 Transport::Tracking tracking{};CameraMath::Matrix neutralWorld{},restoreWorld{},originalInverse{},headWorld{},playerWorld{};
 float fov{};bool applied{};
};
static thread_local Preparation preparation;
template<class T> T& at(void* p,size_t n) {return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+n);}
using RenderFn=void(*)(void*,void*,void*);
using EyeFn=void(*)(void*,int,void*);
using FinishFn=void(*)(void*,int);
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
static RenderFn originalRender;
static EyeFn originalEye;
static FinishFn originalFinish;
static PresentFn originalPresent;
static void log(const char* fmt,...) {
 std::lock_guard lock(logMutex); FILE* f{};_wfopen_s(&f,(dir/L"PlagueStereoProbe.log").c_str(),L"ab");if(!f)return;
 va_list a;va_start(a,fmt);vfprintf(f,fmt,a);va_end(a);fputc('\n',f);fclose(f);
}
static void effectOverride(uint64_t requested){
 if(!nativeForceOff)return;
 const auto prior=activeEffectMask.load();
 const auto added=requested&~prior,removed=prior&~requested;
 if(added){
  const auto old=static_cast<uint64_t>(InterlockedOr64(nativeForceOff,static_cast<LONG64>(added)));
  originalEffectBits=(originalEffectBits&~added)|(old&added);
 }
 // Restore only bits this mod released, keeping unrelated engine state.
 if(removed){
  InterlockedAnd64(nativeForceOff,static_cast<LONG64>(~(removed&~originalEffectBits)));
  InterlockedOr64(nativeForceOff,static_cast<LONG64>(removed&originalEffectBits));
  originalEffectBits&=~removed;
 }
 if(requested)InterlockedOr64(nativeForceOff,static_cast<LONG64>(requested));
 activeEffectMask=requested;
 if(prior!=requested)log("native effect overrides mask=%llx forceOff=%llx restoredBits=%llx",requested,static_cast<uint64_t>(InterlockedCompareExchange64(nativeForceOff,0,0)),removed);
}
static bool imageLayoutAvailable() {
 __try {
  const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>1024*1024)return false;
  const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
  // This is the extent required by the native data accesses below, not a
  // version or executable fingerprint whitelist. Actual hook bytes are still
  // checked before installation on any game build that attempts startup.
  return nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 && nt->OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt->OptionalHeader.SizeOfImage>=0x172dca8;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void saveTexture(ID3D11Texture2D* texture,const std::wstring& name) {
 if(!texture || !device || !context)return;
 ComPtr<ID3D11Device> captureDevice;ComPtr<ID3D11DeviceContext> captureContext;
 texture->GetDevice(&captureDevice);captureDevice->GetImmediateContext(&captureContext);
 D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
 log("capture %ls frame=%llu %ux%u format=%u array=%u samples=%u",name.c_str(),sourceFrame,d.Width,d.Height,d.Format,d.ArraySize,d.SampleDesc.Count);
 const bool hdr=d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT || d.Format==DXGI_FORMAT_R11G11B10_FLOAT || d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT;
 if(!hdr && d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)return;
 if(d.SampleDesc.Count!=1)return;
 d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
 ComPtr<ID3D11Texture2D> staging;
 if(FAILED(captureDevice->CreateTexture2D(&d,nullptr,&staging)))return;
 captureContext->CopyResource(staging.Get(),texture);
 D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(captureContext->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)))return;
 BITMAPFILEHEADER fh{};BITMAPINFOHEADER ih{};
 ih.biSize=sizeof(ih);ih.biWidth=d.Width;ih.biHeight=-static_cast<LONG>(d.Height);ih.biPlanes=1;ih.biBitCount=32;ih.biSizeImage=d.Width*d.Height*4;
 fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(ih);fh.bfSize=fh.bfOffBits+ih.biSizeImage;
 std::ofstream out(dir/name,std::ios::binary);out.write(reinterpret_cast<char*>(&fh),sizeof(fh));out.write(reinterpret_cast<char*>(&ih),sizeof(ih));
 std::vector<uint8_t> row(d.Width*4);
 for(UINT y=0;y<d.Height;++y){
  auto* input=static_cast<uint8_t*>(m.pData)+y*m.RowPitch;
  if(hdr){for(UINT x=0;x<d.Width;++x){float rgb[3]{};
   if(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT){auto* h=reinterpret_cast<uint16_t*>(input)+x*4;for(int ch=0;ch<3;++ch)rgb[ch]=DirectX::PackedVector::XMConvertHalfToFloat(h[ch]);}
   else if(d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT){auto* f=reinterpret_cast<float*>(input)+x*4;for(int ch=0;ch<3;++ch)rgb[ch]=f[ch];}
   else {uint32_t packed=reinterpret_cast<uint32_t*>(input)[x];const auto decode=[](uint32_t v,int mantissa){uint32_t mask=(1u<<mantissa)-1;int e=int(v>>mantissa);return e==31?0.f:std::ldexp(e?1.f+float(v&mask)/float(mask+1):float(v&mask)/float(mask+1),e?e-15:-14);};rgb[0]=decode(packed&2047,6);rgb[1]=decode((packed>>11)&2047,6);rgb[2]=decode(packed>>22,5);}
   for(int ch=0;ch<3;++ch){float v=std::isfinite(rgb[ch])?std::max(0.f,rgb[ch]):0.f;v=std::pow(v/(1.f+v),1.f/2.2f);row[x*4+2-ch]=static_cast<uint8_t>(std::clamp(v*255.f,0.f,255.f));}row[x*4+3]=255;
  }}else memcpy(row.data(),input,row.size());
  if(d.Format==DXGI_FORMAT_R8G8B8A8_UNORM || d.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)for(UINT x=0;x<d.Width;++x)std::swap(row[x*4],row[x*4+2]);
  out.write(reinterpret_cast<char*>(row.data()),row.size());}
 captureContext->Unmap(staging.Get(),0);
}
static void beginPair() {
 writingSlot=nullptr;
 for(auto& s:slots){int free=0;if(s.state.compare_exchange_strong(free,1)){writingSlot=&s;s.frame=sourceFrame;s.mask=0;s.capture=captureFrame;s.trackedMask=0;s.rendered={};s.rendered.mode=renderMenuScreen?MenuScreenPolicy::Mode:2;s.rendered.eyeMask=3;s.rendered.tracking=renderTracking;break;}}
}
static CameraMath::Matrix transpose(const CameraMath::Matrix& value){auto result=value;for(int i=0;i<4;++i)for(int j=0;j<4;++j)result.m[i*4+j]=value.m[j*4+i];return result;}
static void restoreCamera(){
 if(!camera.valid)return;
 memcpy(camera.view,camera.viewBytes,sizeof(camera.viewBytes));
 for(unsigned i=0;i<camera.count;++i)memcpy(camera.projections[i],camera.projectionBytes[i],0x2b0);
}
static void preparationHook(void* renderer){
 MenuScreen::Refresh();
 preparation={};preparation.renderer=renderer;preparation.descriptor=static_cast<uint8_t*>(renderer)+0xf80;
 if(mode.load() && producerInitialized.load()){
  producer.Pace();
  if(producer.ReadTracking(preparation.tracking)){
   static bool recenterDown=false;const bool down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
   if(!referenceValid || (down && !recenterDown)){trackingReference=CameraMath::LevelReference(preparation.tracking.head);referenceValid=true;DlssPass::ResetHistory();log("upstream reference pose=%llu",preparation.tracking.id);}
   recenterDown=down;
   __try {
    auto* handle=at<void*>(preparation.descriptor,0x108);
    if(handle && at<int>(handle,8)==at<int>(preparation.descriptor,0x114)){
     auto* entity=at<void*>(handle,0x10);if(entity)preparation.source=at<void*>(entity,0x110);
    }
   }__except(EXCEPTION_EXECUTE_HANDLER){preparation.source=nullptr;}
  }
 }
 void* sourceEntity{};void* sourceHandle{};int sourceGeneration{};
 __try{sourceHandle=at<void*>(preparation.descriptor,0x108);sourceGeneration=at<int>(preparation.descriptor,0x114);if(sourceHandle && at<int>(sourceHandle,8)==sourceGeneration)sourceEntity=at<void*>(sourceHandle,0x10);}__except(EXCEPTION_EXECUTE_HANDLER){}
 GameplayCamera::Observe(preparation.source,sourceEntity,sourceHandle,sourceGeneration,trackingReference,referenceValid);
 originalPreparation(renderer);
 static uint64_t lastEffectMask=~uint64_t{0};
 const auto effectMask=activeEffectMask.load();
 if(effectMask!=lastEffectMask){
  const auto effective=at<uint64_t>(preparation.descriptor,0x3960);
  log("prepared native effects=%llx offMask=%llx occlusion=%d SSR=%d contactShadows=%d AO=%d volumetricLights=%d probes=%d",effective,effectMask,(effective&occlusionBit)?1:0,(effective&0x800)?1:0,(effective&0x40000000)?1:0,(effective&0x100)?1:0,(effective&0x10000)?1:0,(effective&0x2000)?1:0);
  lastEffectMask=effectMask;
 }
 if(preparation.applied){
  CameraMath::Store(static_cast<uint8_t*>(preparation.source)+0x550,preparation.restoreWorld);
  CameraMath::Store(static_cast<uint8_t*>(preparation.source)+0x590,preparation.originalInverse);
  at<float>(preparation.source,0x5d0)=preparation.fov;
 }
 preparation={};
}
static void* sourceCameraHook(void* source,void* entity){
 auto result=originalSourceCamera(source,entity);
 if(MenuScreen::Active() || source!=preparation.source || preparation.applied || !preparation.tracking.valid || !referenceValid || reinterpret_cast<uintptr_t>(_ReturnAddress())!=base+0x757baf)return result;
 auto world=CameraMath::Load(static_cast<uint8_t*>(source)+0x550);
 if(!CameraMath::Rigid(world))return result;
 preparation.restoreWorld=world;preparation.originalInverse=CameraMath::Load(static_cast<uint8_t*>(source)+0x590);preparation.fov=at<float>(source,0x5d0);
 world=GameplayCamera::RenderBase(source,world);preparation.neutralWorld=world;
 // This renderer-owned preparation interval is the first common source for
 // scene geometry, light-space matrices and native visibility. Restore the
 // application camera after it has built the frame's render records.
 const auto view=FirstPerson::ViewBase(entity,world,&preparation.playerWorld);
 preparation.headWorld=CameraMath::Multiply(CameraMath::Relative(trackingReference,preparation.tracking.head,1.f),view);
 CameraMath::Store(static_cast<uint8_t*>(source)+0x550,preparation.headWorld);
 CameraMath::Store(static_cast<uint8_t*>(source)+0x590,CameraMath::InverseRigid(preparation.headWorld));
 // Conservative symmetric native culling lens encloses both XR eye FOVs.
 // The rendered eye projections below retain each runtime asymmetric lens.
 const auto& l=preparation.tracking.eyes[0];const auto& r=preparation.tracking.eyes[1];
 const int width=at<int>(preparation.descriptor,0x18),height=at<int>(preparation.descriptor,0x1c);
 const float aspect=width>0 && height>0?float(width)/float(height):16.f/9.f;
 const float verticalTan=std::max({std::tan(l.up),std::tan(r.up),std::tan(-l.down),std::tan(-r.down)});
 const float horizontalTan=std::max({std::tan(-l.left),std::tan(-r.left),std::tan(l.right),std::tan(r.right)});
 float vertical=2.f*std::atan(std::max(verticalTan,horizontalTan/aspect));
 if(std::isfinite(vertical) && vertical>.1f && vertical<3.f)at<float>(source,0x5d0)=vertical;
 preparation.applied=true;
 static unsigned logged=0;if(logged++<4)log("upstream head source=%p renderer=%p pose=%llu FOV=%f->%f",source,preparation.renderer,preparation.tracking.id,preparation.fov,at<float>(source,0x5d0));
 return result;
}
static void viewHook(void* builder,void* worldA,void* worldB,uint32_t flags){
 originalView(builder,worldA,worldB,flags);
 if(preparation.applied && worldA==static_cast<uint8_t*>(preparation.descriptor)+0x2850 && reinterpret_cast<uintptr_t>(_ReturnAddress())==base+0x764888){
  void* entry=at<void*>(builder,0xb0);if(!entry)return;
  std::lock_guard lock(preparedMutex);
  auto& item=preparedCameras[preparedCursor++%_countof(preparedCameras)];item={entry,preparation.tracking,preparation.neutralWorld,preparation.headWorld,preparation.playerWorld};
 }
}
static bool preparedTracking(){
 if(!camera.valid)return false;
 std::lock_guard lock(preparedMutex);
 for(unsigned n=0;n<_countof(preparedCameras);++n){
  auto& p=preparedCameras[(preparedCursor-1-n)%_countof(preparedCameras)];if(p.view!=camera.view || !p.tracking.valid)continue;
  // Pointer reuse alone cannot associate a pose with a later native record.
  auto actualWorld=CameraMath::InverseRigid(camera.gameView);float error=0;
  for(int i=0;i<16;++i)error=std::max(error,std::abs(actualWorld.m[i]-p.headWorld.m[i]));
  const auto now=GetTickCount64();if(error>.02f || now<p.tracking.tick || now-p.tracking.tick>=250)return false;
  renderTracking=p.tracking;renderNativeWorld=p.neutralWorld;renderHeadWorld=p.headWorld;renderPlayerWorld=p.playerWorld;renderAimCameraValid=true;return true;
 }
 return false;
}
static bool snapshotCamera(void* renderer){
 camera.valid=false;camera.count=0;
 __try {
  auto* roots=at<void**>(renderer,0x28);if(!roots || !roots[0])return false;
  auto* node=at<uint8_t*>(roots[0],0x18);if(!node)return false;
  camera.root=node+0x700;int active=1-at<int>(camera.root,0);if(active<0 || active>1)return false;
  auto* state=static_cast<uint8_t*>(camera.root)+active*0x270;
  if(at<int>(state,0x90)!=1 || at<int>(state,0x80)!=1)return false;
  auto* viewBlock=at<void**>(state,0x98)[0];auto* projectionBlock=at<void**>(state,0x88)[0];
  if(at<int>(viewBlock,4)!=1)return false;
  int count=at<int>(projectionBlock,4);if(count<1 || count>8)return false;
  camera.view=at<void*>(viewBlock,8);memcpy(camera.viewBytes,camera.view,sizeof(camera.viewBytes));
  auto rotation=transpose(CameraMath::Load(static_cast<uint8_t*>(camera.view)+0x10));
  auto position=at<Transport::Vector>(camera.view,0x50);
  for(int j=0;j<3;++j)rotation.m[12+j]=-(position.x*rotation.m[j]+position.y*rotation.m[4+j]+position.z*rotation.m[8+j]);
  if(!CameraMath::Rigid(rotation))return false;
  camera.gameView=rotation;
  auto* entries=at<uint8_t*>(projectionBlock,8);
  for(int i=0;i<count;++i){camera.projections[i]=entries+size_t(i)*0x2b0;memcpy(camera.projectionBytes[i],camera.projections[i],0x2b0);}
  camera.count=count;camera.valid=true;
  if(captureFrame)log("primary camera frame=%llu root=%p view=%p projections=%u origin=%f,%f,%f",sourceFrame,camera.root,camera.view,camera.count,position.x,position.y,position.z);
  return true;
 } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool applyTracking(void* root,int eye){
 if(renderMenuScreen || !camera.valid || camera.root!=root || eye<0 || eye>1 || !renderTracking.valid || !referenceValid)return false;
 CameraMath::EyeMatrices matrices{};
 for(unsigned i=0;i<camera.count;++i){
  auto depth=transpose(CameraMath::Load(camera.projectionBytes[i]+0x10));
  auto previousDepth=transpose(CameraMath::Load(camera.projectionBytes[i]+0xd0));
  // Native near-plane/world coordinates suggest metre units. This scale
  // remains a headset-test parameter, not a validated physical calibration.
  // Keep all view-space lighting data in the prepared head-centre space.
  // Encode the actual eye's local pose in the projection, as the native
  // stereo renderer does for IPD, instead of shifting only the scene view.
  matrices.projection=CameraMath::CenteredEyeProjection(depth,renderTracking.head,renderTracking.eyes[eye],1.f);
  auto previousProjection=CameraMath::CenteredEyeProjection(previousDepth,renderTracking.head,renderTracking.eyes[eye],1.f);
  DlssPass::Projection(matrices.projection,previousProjection,depth,previousDepth);
  if(!CameraMath::Inverse(matrices.projection,matrices.inverseProjection))return false;
  CameraMath::Matrix inversePrevious{};if(!CameraMath::Inverse(previousProjection,inversePrevious))return false;
  matrices.translatedView=camera.gameView;matrices.translatedView.m[12]=matrices.translatedView.m[13]=matrices.translatedView.m[14]=0;
  matrices.translatedVP=CameraMath::Multiply(matrices.translatedView,matrices.projection);
  auto centerWorld=CameraMath::InverseRigid(camera.gameView);matrices.origin={centerWorld.m[12],centerWorld.m[13],centerWorld.m[14]};
  auto* entry=static_cast<uint8_t*>(camera.projections[i]);
  auto projection=transpose(matrices.projection),inverse=transpose(matrices.inverseProjection);
  auto previous=transpose(previousProjection),previousInverse=transpose(inversePrevious);
  CameraMath::Store(entry+0x10,projection);CameraMath::Store(entry+0x50,inverse);CameraMath::Store(entry+0x90,inverse);
  CameraMath::Store(entry+0xd0,previous);CameraMath::Store(entry+0x110,previousInverse);
  if(i==0){
   auto rotation=transpose(matrices.translatedView);
   // Native B is the preceding camera buffer (+0x770 / active*0x140),
   // not a second copy of the current head pose. Keep its origin and
   // rotation for velocity/SSR/AO, with this eye's previous projection.
   auto previousRotation=transpose(CameraMath::Load(camera.viewBytes+0x60));
   auto vp=transpose(CameraMath::Multiply(previousRotation,previousProjection));
   auto* view=static_cast<uint8_t*>(camera.view);
   CameraMath::Store(view+0x10,rotation);CameraMath::Store(view+0xb0,vp);
   at<Transport::Vector>(view,0x50)=matrices.origin;
   if(captureFrame)log("tracked camera frame=%llu eye=%d pose=%llu origin=%f,%f,%f",sourceFrame,eye,renderTracking.id,matrices.origin.x,matrices.origin.y,matrices.origin.z);
  }
 }
 if(captureFrame){const void* entries[8]{};for(unsigned i=0;i<camera.count;++i)entries[i]=camera.projections[i];GpuCapture::Camera(eye,camera.view,entries,camera.count);}
 if(writingSlot){writingSlot->trackedMask|=1u<<eye;if(writingSlot->trackedMask==3)writingSlot->rendered.mode=1;}
 return true;
}
static void copyEye(void* renderer) {
 const int eye=at<int>(renderer,0x2d2d0);
 if(eye<0 || eye>1)return;
 auto* gameContext=at<ID3D11DeviceContext*>(renderer,0x3b048);
 if(!gameContext)return;
 EffectHistory::EndEye(gameContext,eye);
 if(!writingSlot)return;
 ComPtr<ID3D11Device> gameDevice;gameContext->GetDevice(&gameDevice);
 writingSlot->submitContext=gameContext;
 if(traceFrame)log("device identities game=%p gameContext=%p present=%p presentContext=%p",gameDevice.Get(),gameContext,device.Get(),context.Get());
 for(unsigned which=0;which<3;++which){
  if(which==2 && !writingSlot->capture)continue;
  // +0x60688 is a DXGI format value, not a resource handle. The engine
  // allocates its HDR scene target into +0x64c24 in RVA 0x5f11a0.
  const int index=at<int>(renderer,which==2?0x64c24:(which?0x606a0:0x6069c));if(index<0 || index>4096)continue;
  void* table=at<void*>(renderer,0x39df8);if(!table)continue;
  auto* resource=at<ID3D11Resource*>(table,size_t(index)*0xb8+8);if(!resource)continue;
  ComPtr<ID3D11Texture2D> source;
  if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&source))))continue;
  D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
  if(traceFrame)log("eye boundary frame=%llu eye=%d source=%u handle=%d %ux%u format=%u gameContextType=%u",sourceFrame,eye,which,index,d.Width,d.Height,d.Format,gameContext->GetType());
  if(d.SampleDesc.Count!=1 || d.ArraySize!=1 || d.MipLevels!=1)continue;
  if(d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && !(which==2 && (d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT || d.Format==DXGI_FORMAT_R11G11B10_FLOAT || d.Format==DXGI_FORMAT_R32G32B32A32_FLOAT)))continue;
  auto& dest=writingSlot->eyes[which][eye];
  if(dest){D3D11_TEXTURE2D_DESC old{};dest->GetDesc(&old);if(old.Width!=d.Width || old.Height!=d.Height || old.Format!=d.Format)dest.Reset();}
  if(!dest){d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.MiscFlags=0;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
   if(FAILED(gameDevice->CreateTexture2D(&d,nullptr,&dest)))continue;}
  gameContext->CopyResource(dest.Get(),source.Get());
  if(which==0)writingSlot->mask|=1u<<eye;
 }
 if(eye==1 && writingSlot->mask==3){
  if(!writingSlot->complete){D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};gameDevice->CreateQuery(&q,&writingSlot->complete);}
  if(writingSlot->complete){gameContext->End(writingSlot->complete.Get());writingSlot->state=2;writingSlot=nullptr;}
 }
}
static int boundaryHook(void* renderer) {
 // This getter is called after the native renderer has finished one whole eye.
 // Filter its caller: other uses of the getter are not eye completion.
 if(reinterpret_cast<uintptr_t>(_ReturnAddress())==base+0x62c4a7)copyEye(renderer);
 return originalBoundary(renderer);
}
static void consumePairs() {
 if(!context)return;
 for(auto& s:slots){
  if(s.state.load()!=2)continue;
  BOOL done=FALSE;auto status=s.submitContext->GetData(s.complete.Get(),&done,sizeof(done),0);
  if(FAILED(status)){log("GPU query failed frame=%llu HRESULT=%lx deviceRemoved=%lx",s.frame,status,device->GetDeviceRemovedReason());s.state=0;continue;}
  if(status!=S_OK || !done)continue;
  int pending=2;if(!s.state.compare_exchange_strong(pending,3))continue;
  if(s.capture){
   for(unsigned source=0;source<3;++source)for(unsigned eye=0;eye<2;++eye)
    saveTexture(s.eyes[source][eye].Get(),L"source-"+std::to_wstring(source)+L"-eye-"+std::to_wstring(eye)+L".bmp");
   std::ofstream metadata(dir/L"native-pair.json");metadata<<"{\"sourceFrame\":"<<s.frame<<",\"leftFrame\":"<<s.frame<<",\"rightFrame\":"<<s.frame<<",\"eyeMask\":"<<s.mask<<",\"mode\":"<<s.rendered.mode<<",\"poseId\":"<<s.rendered.tracking.id<<",\"trackedMask\":"<<s.trackedMask<<"}";
   log("GPU-complete captured pair frame=%llu mask=%u",s.frame,s.mask);
  }
  if(s.eyes[0][0] && s.eyes[0][1]){
   D3D11_TEXTURE2D_DESC d{};s.eyes[0][0]->GetDesc(&d);
   if(!producerInitialized){
    const auto width=headsetWidth?headsetWidth:d.Width,height=headsetHeight?headsetHeight:d.Height;
    // Create the channel before gameplay loads, but publish no smaller
    // startup target. The companion waits for generation 1, which becomes
    // available only when native source 0 matches the requested XR size.
    producerInitialized=producer.Initialize(width,height);
    log("pair transport initialized=%d eye=%ux%u",producerInitialized.load()?1:0,width,height);
   }
   mirrorEyes[0]=s.eyes[0][0];mirrorEyes[1]=s.eyes[0][1];
   if(producerInitialized){
    if(producer.Left(s.eyes[0][0].Get()) && producer.Right(s.eyes[0][1].Get(),s.frame,s.rendered)){
     if(producer.Published()<4 || s.capture)log("published same-frame pair=%llu sourceFrame=%llu",producer.Published(),s.frame);
    }else producer.Cancel();
   }
  }
  s.state=0;
 }
}
static void frameHook(void* renderer){
 MenuScreen::Refresh();
 const int selected=mode.load();
 const uint64_t previous=at<uint64_t>(renderer,0x2a2b8);
 if(headsetWidth && headsetHeight && nativeResize && at<ID3D11DeviceContext*>(renderer,0x3b048)){
  // The native scene-size helpers impose 16:10 .. 16:9 letterboxing.
  // Widen only the process-local bounds needed by the requested XR size;
  // the same native helpers then allocate the full-resolution game targets.
  if(!aspectConfigured && *minimumAspect>=.1f && *maximumAspect<=10.f && *maximumAspect>=*minimumAspect){
   const float aspect=float(headsetWidth)/float(headsetHeight);
   const float originalMin=*minimumAspect,originalMax=*maximumAspect;
   *minimumAspect=std::min(originalMin,aspect);*maximumAspect=std::max(originalMax,aspect);
   aspectConfigured=true;
   log("headset aspect bounds %f..%f -> %f..%f target=%ux%u",originalMin,originalMax,*minimumAspect,*maximumAspect,headsetWidth,headsetHeight);
  }
  if(aspectConfigured && (at<unsigned>(renderer,0x21fb4)!=headsetWidth || at<unsigned>(renderer,0x21fb8)!=headsetHeight)){
   // Run resource resizing on the renderer's own serial graphics interval,
   // before enabling its eye-layout bits. Those bits would otherwise double
   // output dimensions inside the native setter.
   at<uint64_t>(renderer,0x2a2b8)=previous&~0x780000000ull&~0x800000000000ull;
   nativeResize(renderer,int(headsetWidth),int(headsetHeight));
   at<uint64_t>(renderer,0x2a2b8)=previous;
   log("native headset resize renderer=%p scene=%ux%u output=%ux%u",renderer,at<unsigned>(renderer,0x21fb4),at<unsigned>(renderer,0x21fb8),at<unsigned>(renderer,0x21fbc),at<unsigned>(renderer,0x21fc0));
   if(at<unsigned>(renderer,0x21fb4)!=headsetWidth || at<unsigned>(renderer,0x21fb8)!=headsetHeight){log("Headset resize did not reach requested dimensions; override disabled");headsetWidth=headsetHeight=0;}
  }
 }
 if(selected){
  at<uint64_t>(renderer,0x2a2b8)=(previous&~0x780000000ull&~0x800000000000ull)|(selected==2?0x100000000ull:0x80000000ull);
 }
 static DlssStartupGate dlssStartup;
 const bool startupReady=dlssStartup.Update(GetTickCount64(),selected && producerInitialized.load() && camera.valid && renderTracking.valid && !MenuScreen::Active(),!selected || MenuScreen::Active());
 DlssPass::Prepare(renderer,at<ID3D11DeviceContext*>(renderer,0x3b048),selected!=0,headsetWidth,headsetHeight,startupReady);
 originalFrame(renderer);
 if(selected)at<uint64_t>(renderer,0x2a2b8)=previous;
}
static void projectionHook(void* builder,void* projection,void* originalP,void* jitter,bool stereo){
 // The main-camera caller chooses this eligibility boolean from native
 // bits 33/34. Enable that same projection update for sequential eye mode,
 // without enabling instanced stereo shaders or changing auxiliary cameras.
 const bool mainCaller=reinterpret_cast<uintptr_t>(_ReturnAddress())==base+0x764867;
 if(mainCaller)stereo=true;
 originalProjection(builder,projection,originalP,jitter,stereo);
 static std::atomic<unsigned> samples{};
 if(mainCaller && samples.fetch_add(1)<4){
  auto p=CameraMath::Load(projection),original=CameraMath::Load(originalP);
  log("projection input builder=%p projection=%p originalProjection=%p P11=%f P14=%f P15=%f originalP11=%f originalP14=%f",builder,projection,originalP,p.m[11],p.m[14],p.m[15],original.m[11],original.m[14]);
 }
}
struct SavedFlags {void* object;uint64_t flags;};
static int prepare(void* renderer,SavedFlags* saved,int max,int selected) {
 int used=0;
 __try {
  const int count=reinterpret_cast<int(*)(void*)>(base+0x7b2660)(renderer);
  if(count<0 || count>16)return 0;
  for(int i=0;i<count;++i){void* a=reinterpret_cast<void*(*)(void*,int)>(base+0x7b2680)(renderer,i);
   int roots=at<int>(a,4);void** list=at<void**>(a,8);if(roots<0 || roots>32 || !list)continue;
   for(int j=0;j<roots && used<max;++j){void* viewport=at<void*>(list[j],0xa8);if(!viewport)continue;
    bool duplicate=false;for(int k=0;k<used;++k)if(saved[k].object==viewport)duplicate=true;
    if(duplicate)continue;
    auto& flags=at<uint64_t>(viewport,0x3d0);saved[used++]={viewport,flags};
    if(sourceFrame<4 || captureFrame)log("view %d/%d viewport=%p flags=%llx vflags=%llx",i,j,viewport,flags,at<uint64_t>(viewport,0x3b8));
    if(selected)flags=(flags&~0x780000000ull)|(selected==2?0x100000000ull:0x80000000ull);
   }
  }
 } __except(EXCEPTION_EXECUTE_HANDLER){log("invalid viewport layout; mode disabled");mode=0;}
 return used;
}
static void renderHook(void* renderer,void* p2,void* p3) {
 sourceFrame=++frame;eyeCalls=0;projectionCaptures[0]=projectionCaptures[1]=0;captureFrame=false;const int selected=mode.load();SavedFlags saved[32]{};
 renderMenuScreen=selected && MenuScreen::Active();
 static bool previousMenu{};if(renderMenuScreen!=previousMenu){DlssPass::ResetHistory();previousMenu=renderMenuScreen;log("menu screen transition frame=%llu active=%d",sourceFrame,renderMenuScreen?1:0);}
 static unsigned nativeFrames=0;if(selected)++nativeFrames;else nativeFrames=0;
 traceFrame=sourceFrame<4 || captureFrame || (selected && nativeFrames<4);
 renderTracking={};renderAimCameraValid=false;renderPlayerWorld={};
 int count=prepare(renderer,saved,32,selected);const uint64_t rendererFlags=at<uint64_t>(renderer,0x2a2b8);
 if(selected)at<uint64_t>(renderer,0x2a2b8)=(rendererFlags&~0x780000000ull)|(selected==2?0x100000000ull:0x80000000ull);
 if(selected)snapshotCamera(renderer);else camera.valid=false;
 if(selected){if(renderMenuScreen)producer.ReadLatestTracking(renderTracking);else preparedTracking();}
 ShaderHunter::Begin(sourceFrame,renderTracking.id,selected!=0);
 if(DiagnosticControls::Poll(sourceFrame,renderTracking.id))capture=1;
 if(ShaderHunter::CaptureRequested())capture=1;
 if(DlssPass::CaptureRequested())capture=1;
 // Empty native intervals must not consume the requested world-frame capture.
 if(selected && count>0 && (camera.valid || renderMenuScreen) && renderTracking.valid && capture.load()){
  captureFrame=capture.exchange(0)!=0;traceFrame=captureFrame;
  if(captureFrame){GpuCapture::Begin(sourceFrame,renderTracking.id);GameplayCamera::Capture(sourceFrame,renderTracking.id);FirstPerson::Capture(sourceFrame,renderTracking.id);log("tracked GPU capture frame=%llu pose=%llu root=%p",sourceFrame,renderTracking.id,camera.root);}
 }
 if(selected)beginPair();else writingSlot=nullptr;
 const bool tracked=selected && renderTracking.valid && !renderMenuScreen;
 // Use the player root saved with this exact prepared render camera, rather
 // than a newer simulation update's root. Match the consuming view precisely.
 const auto visibleCamera=camera.valid?CameraMath::InverseRigid(camera.gameView):CameraMath::Matrix{};
 FirstPersonHead::Begin(sourceFrame,tracked && CameraMath::Rigid(renderPlayerWorld),renderPlayerWorld,visibleCamera,captureFrame);
 // Reflection-probe removal still needs its retained-buffer clear on menus.
 EffectHistory::Begin(sourceFrame,selected && renderTracking.valid,effectsMode.load()!=0,captureFrame,(activeEffectMask.load()&0x2000)!=0);
 DlssPass::Begin(sourceFrame,tracked,captureFrame);
 auto uiTracking=renderTracking;if(renderMenuScreen)uiTracking.valid=0;
 ScreenUI::Begin(sourceFrame,uiTracking,captureFrame);
 AimReticle::Begin(sourceFrame,uiTracking,renderAimCameraValid?&renderNativeWorld:nullptr,renderAimCameraValid?&renderHeadWorld:nullptr,captureFrame);
 LightingTrial::Begin(sourceFrame,tracked,captureFrame);
 LightList::Begin(sourceFrame,tracked,captureFrame);
 ProjectedLightBounds::Begin(sourceFrame,tracked,captureFrame);
 DecalPass::Begin(sourceFrame,selected && renderTracking.valid,captureFrame,at<ID3D11DeviceContext*>(renderer,0x3b048),ShaderHunter::Enabled());
 originalRender(renderer,p2,p3);
 DecalPass::End(at<ID3D11DeviceContext*>(renderer,0x3b048));
 ShaderHunter::End();
 LightList::End();
 ProjectedLightBounds::End();
 LightingTrial::End();
 ScreenUI::End();
 DlssPass::End();
 AimReticle::End();
 FirstPersonHead::End();
 EffectHistory::End();
 if(captureFrame)GpuCapture::End(at<ID3D11DeviceContext*>(renderer,0x3b048));
 restoreCamera();
 if(writingSlot){writingSlot->state=0;writingSlot=nullptr;}
 if(selected){for(int i=0;i<count;++i)at<uint64_t>(saved[i].object,0x3d0)=saved[i].flags;at<uint64_t>(renderer,0x2a2b8)=rendererFlags;}
 if(captureFrame)captureDesktop=1;
 if(traceFrame)log("frame=%llu renderer=%p mode=%d views=%d eyeUpdates=%d renderFlags=%llx",sourceFrame,renderer,selected,count,eyeCalls,rendererFlags);
}
static void captureProjection(void* p,int eye) {
 if(!captureFrame || eye<0 || eye>1 || projectionCaptures[eye]>=4)return;
 __try {
  int active=1-at<int>(p,0);if(active<0 || active>1)return;
  auto* data=static_cast<uint8_t*>(p)+active*0x270;
  int blocks=at<int>(data,0x80);void** table=at<void**>(data,0x88);
  if(blocks<=0 || blocks>128 || !table)return;
  for(int b=0;b<blocks && projectionCaptures[eye]<4;++b){
   int count=at<int>(table[b],4);void* entries=at<void*>(table[b],8);if(count<0 || count>128 || !entries)continue;
   for(int i=0;i<count && projectionCaptures[eye]<4;++i){auto* entry=static_cast<uint8_t*>(entries)+size_t(i)*0x2b0;
    if(!at<uint8_t>(entry,0x2a0))continue;
    const auto n=projectionCaptures[eye]++;
    log("native projection frame=%llu eye=%d sample=%u root=%p entry=%p m00=%f m02=%f m03=%f m11=%f m22=%f m23=%f m32=%f m33=%f",sourceFrame,eye,n,p,entry,at<float>(entry,0x10),at<float>(entry,0x18),at<float>(entry,0x1c),at<float>(entry,0x24),at<float>(entry,0x38),at<float>(entry,0x3c),at<float>(entry,0x48),at<float>(entry,0x4c));
   }
  }
 } __except(EXCEPTION_EXECUTE_HANDLER){log("projection diagnostic layout invalid");}
}
static void eyeHook(void* p,int eye,void* config) {
 ++eyeCalls;
 EffectHistory::Eye(eye);
 ScreenUI::Eye(eye);
 FirstPersonHead::Eye(eye);
 DlssPass::Eye(eye);
 AimReticle::Eye(eye);
 LightingTrial::Eye(eye);
 ShaderHunter::Eye(eye);
 LightList::Eye(eye);
 ProjectedLightBounds::Eye(eye);
 DecalPass::Eye(eye);
 if(captureFrame)GpuCapture::Eye(eye);
 if(traceFrame && camera.valid && camera.root==p)log("native primary eye frame=%llu eye=%d config=%f,%f,%f,%f,%f",sourceFrame,eye,at<float>(config,0),at<float>(config,4),at<float>(config,8),at<float>(config,12),at<float>(config,16));
 if(camera.valid && camera.root==p)restoreCamera();
 // Reversible diagnostic: native IPD belongs to the scene, while auxiliary
 // light/shadow cameras may require their original centred projection.
 float auxiliaryConfig[5]{};
 if(renderMenuScreen || (zeroAuxiliaryStereo.load() && camera.valid && camera.root!=p && renderTracking.valid)){memcpy(auxiliaryConfig,config,sizeof(auxiliaryConfig));auxiliaryConfig[0]=0;originalEye(p,eye,auxiliaryConfig);}
 else originalEye(p,eye,config);
 applyTracking(p,eye);
 captureProjection(p,eye);
 if(captureFrame && camera.root!=p){
  __try{
   auto* state=static_cast<uint8_t*>(p)+(1-at<int>(p,0))*0x270;
   if(at<int>(state,0x90)==1 && at<int>(state,0x80)==1){
    auto* views=at<void**>(state,0x98)[0];auto* projections=at<void**>(state,0x88)[0];
    const void* entries[8]{};unsigned n{};
    if(at<int>(views,4)==1 && at<int>(projections,4)>0 && at<int>(projections,4)<=8){
     auto* first=at<uint8_t*>(projections,8);
     for(int i=0;i<at<int>(projections,4);++i)if(at<uint8_t>(first+size_t(i)*0x2b0,0x2a0))entries[n++]=first+size_t(i)*0x2b0;
     if(n)GpuCapture::Auxiliary(eye,p,at<void*>(views,8),entries,n);
    }
   }
  }__except(EXCEPTION_EXECUTE_HANDLER){log("Auxiliary camera capture invalid");}
 }
}
static void finishHook(void* renderer,int eye) {
 if(traceFrame)log("native finish frame=%llu eye=%d",sourceFrame,eye);
 if(captureFrame && context){
  ComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);
  if(target){ComPtr<ID3D11Resource> r;target->GetResource(&r);ComPtr<ID3D11Texture2D> t;r.As(&t);
   saveTexture(t.Get(),L"eye-"+std::to_wstring(eye)+L".bmp");}
 }
 originalFinish(renderer,eye);
}
static HRESULT STDMETHODCALLTYPE presentHook(IDXGISwapChain* swap,UINT sync,UINT flags) {
 ComPtr<ID3D11Device> presentDevice;swap->GetDevice(IID_PPV_ARGS(&presentDevice));
 if(presentDevice && device.Get()!=presentDevice.Get()){device=presentDevice;device->GetImmediateContext(&context);log("D3D11 Present device=%p context=%p",device.Get(),context.Get());}
 consumePairs();
 GpuCapture::Flush();
 DlssPass::FlushInputAudit();
 if(mode.load())mirror.Draw(swap,mirrorEyes[0].Get(),mirrorEyes[1].Get());
 if(captureDesktop.exchange(0)){ComPtr<ID3D11Texture2D> t;swap->GetBuffer(0,IID_PPV_ARGS(&t));saveTexture(t.Get(),L"desktop.bmp");}
 return originalPresent(swap,sync,flags);
}
static bool hook(uintptr_t rva,const void* expected,size_t n,void* replacement,void** original){
 if(memcmp(reinterpret_cast<void*>(base+rva),expected,n)){log("hook signature mismatch %llx",rva);return false;}
 auto s=MH_CreateHook(reinterpret_cast<void*>(base+rva),replacement,original);log("hook %llx status=%d",rva,s);return s==MH_OK;
}
static DWORD WINAPI start(void* module) {
 wchar_t path[32768]{};GetModuleFileNameW(static_cast<HMODULE>(module),path,_countof(path));dir=std::filesystem::path(path).parent_path();
 base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 if(!imageLayoutAvailable()){log("Incompatible process image layout; no hooks installed");return 1;}
 // Derive the engine-owned low effect force-off mask from its exact camera
 // instruction. Bit 14 is the native Occlusion Culling menu/callback mask.
 const uint8_t forceOffLoad[]={0x48,0x8b,0x05,0x3f,0x74,0xdd,0x00};
 if(memcmp(reinterpret_cast<void*>(base+0x757a2a),forceOffLoad,sizeof(forceOffLoad))){log("Occlusion control signature mismatch; no hooks installed");return 1;}
 const auto displacement=*reinterpret_cast<int32_t*>(base+0x757a2d);
 nativeForceOff=reinterpret_cast<volatile LONG64*>(base+0x757a2a+sizeof(forceOffLoad)+displacement);
 for(const auto& effect:nativeEffects){
  if(*reinterpret_cast<uint64_t*>(base+effect.maskRva)!=effect.mask || *reinterpret_cast<uint64_t*>(base+effect.maskRva+8)!=0){log("Native effect mask signature mismatch; no hooks installed");return 1;}
 }
 {
  unsigned width{},height{};std::ifstream resolution(dir/L"headset-resolution.txt");resolution>>width>>height;
  if(width && width<=16384 && height && height<=8192){
   const uint8_t aspectLoad[]={0xf3,0x0f,0x10,0x1d,0xf7,0x39,0xcd,0x00};
   const uint8_t resizePrefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x41,0x56};
   if(memcmp(reinterpret_cast<void*>(base+0x765ae1),aspectLoad,sizeof(aspectLoad)) || memcmp(reinterpret_cast<void*>(base+0x76e4b0),resizePrefix,sizeof(resizePrefix))){log("Headset resolution signature mismatch; no hooks installed");return 1;}
   const auto offset=*reinterpret_cast<int32_t*>(base+0x765ae5);
   minimumAspect=reinterpret_cast<float*>(base+0x765ae1+sizeof(aspectLoad)+offset);
   maximumAspect=minimumAspect+1;nativeResize=reinterpret_cast<ResizeFn>(base+0x76e4b0);
   headsetWidth=width;headsetHeight=height;
  }
 }
 if(MH_Initialize()!=MH_OK)return 2;
 DiagnosticControls::Initialize(dir);
 if(!MenuScreen::Initialize(base,dir)){log("native menu focus signature mismatch");return 3;}
 if(!FirstPerson::Initialize(base,dir)){log("first-person player lookup signature mismatch");return 3;}
 FirstPersonHead::Initialize(dir);
 if(!GameplayCamera::Install(base,dir,[](Transport::Tracking& value){return producerInitialized.load() && producer.ReadLatestTracking(value);},[]{return mode.load()!=0 && !MenuScreen::Active();}))return 3;
 const uint8_t a[]={0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x54,0x24,0x10};
 const uint8_t b[]={0x4c,0x8b,0xdc,0x55,0x56,0x41,0x54,0x41,0x56};
 const uint8_t c[]={0x48,0x8b,0xc4,0x55,0x57,0x41,0x57};
 const uint8_t d[]={0x8b,0x81,0x78,0xa2,0x02,0x00,0xc3};
 const uint8_t e[]={0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x54};
 const uint8_t f[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
 const uint8_t g[]={0x48,0x8b,0xc4,0x48,0x89,0x48,0x08,0x55,0x56,0x57};
 const uint8_t h[]={0x48,0x8d,0x91,0x90,0x05,0x00,0x00,0x48,0x81,0xc1,0x50,0x05,0x00,0x00};
 const uint8_t j[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10};
 if(!hook(0x62bcc0,a,sizeof(a),reinterpret_cast<void*>(renderHook),reinterpret_cast<void**>(&originalRender)) || !hook(0x5e6f70,b,sizeof(b),reinterpret_cast<void*>(eyeHook),reinterpret_cast<void**>(&originalEye)) || !hook(0x62c7b0,c,sizeof(c),reinterpret_cast<void*>(finishHook),reinterpret_cast<void**>(&originalFinish)))return 3;
 if(!hook(0x5f7770,d,sizeof(d),reinterpret_cast<void*>(boundaryHook),reinterpret_cast<void**>(&originalBoundary)))return 3;
 if(!hook(0x62afe0,e,sizeof(e),reinterpret_cast<void*>(frameHook),reinterpret_cast<void**>(&originalFrame)))return 3;
 if(!hook(0x7b12e0,f,sizeof(f),reinterpret_cast<void*>(projectionHook),reinterpret_cast<void**>(&originalProjection)))return 3;
 if(!hook(0x762f80,g,sizeof(g),reinterpret_cast<void*>(preparationHook),reinterpret_cast<void**>(&originalPreparation)) ||
    !hook(0x752660,h,sizeof(h),reinterpret_cast<void*>(sourceCameraHook),reinterpret_cast<void**>(&originalSourceCamera)) ||
    !hook(0x7b1650,j,sizeof(j),reinterpret_cast<void*>(viewHook),reinterpret_cast<void**>(&originalView)))return 3;
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=static_cast<HMODULE>(module);wc.lpszClassName=L"PlagueProbeWindow";RegisterClassW(&wc);
 HWND w=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,32,32,nullptr,nullptr,wc.hInstance,nullptr);
 DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=1;sd.BufferDesc.Width=32;sd.BufferDesc.Height=32;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=w;sd.SampleDesc.Count=1;sd.Windowed=TRUE;
 ComPtr<IDXGISwapChain> swap;ComPtr<ID3D11Device> dummy;ComPtr<ID3D11DeviceContext> dc;
 if(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&dummy,nullptr,&dc))){
  if(!GpuCapture::Install(dummy.Get(),dc.Get(),dir,base)){log("GPU capture hook installation failed; do not enable partial hooks");return 5;}
  void* present=(*reinterpret_cast<void***>(swap.Get()))[8];auto s=MH_CreateHook(present,reinterpret_cast<void*>(presentHook),reinterpret_cast<void**>(&originalPresent));log("Present hook status=%d",s);
 }
 if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK)return 4;
 log("Native layout checks passed; observe mode. control.txt: 0=flat, 1=native SBS, 2=native stacked. capture.flag requests bounded BMPs.");
 int previous=0;uint64_t nativeStart=0;
 for(;;){
  int effectRequested=0;{std::ifstream effects(dir/L"effects-control.txt");effects>>effectRequested;}effectsMode=effectRequested==1?1:0;
  int requested=0;{std::ifstream control(dir/L"control.txt");control>>requested;}if(requested<0 || requested>3)requested=0;
  if(requested!=previous){mode=requested;log("mode=%d",requested);previous=requested;nativeStart=GetTickCount64();}
  if(mode.load()>0 && mode.load()<3 && GetTickCount64()-nativeStart>3000){mode=0;log("bounded stereo test restored flat rendering");}
  if(producerInitialized.load() && producer.StopRequested())mode=0;
  int occlusionRequested=1;{std::ifstream setting(dir/L"occlusion-control.txt");if(setting)setting>>occlusionRequested;}
  uint64_t forceOff=occlusionRequested==1?static_cast<uint64_t>(occlusionBit):0;
  for(const auto& effect:nativeEffects){if(GetPrivateProfileIntW(L"Rendering",effect.key,effect.defaultOff,(dir/L"PlagueTaleVR.ini").c_str())==1)forceOff|=effect.mask;}
  effectOverride(mode.load()!=0?forceOff:0);
  const bool auxiliaryRequested=GetPrivateProfileIntW(L"Diagnostics",L"DisableAuxiliaryStereoOffsets",0,(dir/L"PlagueTaleVR.ini").c_str())==1;
  if(auxiliaryRequested!=zeroAuxiliaryStereo.exchange(auxiliaryRequested))log("auxiliary stereo offsets disabled=%d",auxiliaryRequested);
  std::error_code error;
  if(std::filesystem::exists(dir/L"capture.flag",error) && !error){
   if(std::filesystem::remove(dir/L"capture.flag",error) && !error)capture=1;
  }
  Sleep(200);
 }
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD why,void*){if(why==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(module);HANDLE t=CreateThread(nullptr,0,start,module,0,nullptr);if(t)CloseHandle(t);}return TRUE;}
