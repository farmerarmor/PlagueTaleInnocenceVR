#include "DlssPass.h"
#include "DlssRuntime.h"
#include "DlssShaders.h"
#include "DlssNativeInputs.h"
#include "DlssJournal.h"
#include "DlssComputeState.h"
#include "DlssImageAudit.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <cmath>
#include <cstring>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstdio>
using Microsoft::WRL::ComPtr;
namespace DlssPass {
namespace {
std::filesystem::path directory;uintptr_t base{};
thread_local bool busy{},active{},trace{},substituted{};thread_local int eye=-1;thread_local uint64_t frame{};
bool requested{},available{},attempted{},configured{},failed{};uint64_t pollTick{},lastFrame[2]{},executed[2]{};
bool retryableFailure{};uint64_t rejectedFrame{};
std::atomic<unsigned> resetGeneration{};unsigned eyeGeneration[2]{};uint64_t pendingFrames{};
UINT outWidth{},outHeight{},inputWidth{},inputHeight{};float jitterNdc[2]{};
void* ownedRenderer{};void* config{};int savedType{};uint64_t savedOn{};float savedScale{},scale{};
DlssRuntime runtime;
DlssModes::Mode wantedMode=DlssModes::Off,configuredMode=DlssModes::Off;
DlssModes::Mode noticeMode=DlssModes::Off;bool noticeStarted{},noticeActive{},noticeFailed{};
ComPtr<ID3D11Query> transitionFence;bool transitionPending{};
ComPtr<ID3D11DeviceContext1> context1;
ComPtr<ID3D11ComputeShader> inputsShader;ComPtr<ID3D11Buffer> dimensions;
ComPtr<ID3D11PixelShader> outputShader,savedPixel;
std::string status="off",failureReason;
unsigned diagnosed[2]{};
uint64_t activationTick{};bool capturedInputs{};unsigned seenTargets[2]{};
uint64_t lastInputAttempt[2]{};
HANDLE journalMapping{};DlssJournal* journal{};
SRWLOCK journalLock=SRWLOCK_INIT;
unsigned trialPairs{},pairLimit{};bool automaticCapture{};
bool inputAudit{};
// 0 normal; 1 native scale/AA only; 2 convert/copy inputs, no NGX;
// 3 evaluate NGX but retain the original native upscale output.
unsigned diagnosticMode{};
struct AuditEye {ComPtr<ID3D11Texture2D> inputs[3];ComPtr<ID3D11Buffer> global;ComPtr<ID3D11Query> complete;uint64_t frame{};UINT width{},height{};float jitter[2]{};bool saved{};};
AuditEye audits[2];
std::atomic<unsigned> auditState[2]{};
struct Completion {ComPtr<ID3D11Query> query;uint64_t frame{};bool pending{};};
Completion completions[16];uint64_t completedFrame{};HRESULT deviceStatus=S_OK;
void stamp(unsigned phase){if(!journal || !TryAcquireSRWLockExclusive(&journalLock))return;InterlockedIncrement(reinterpret_cast<volatile LONG*>(&journal->sequence));journal->tick=GetTickCount64();if(frame)journal->frame=frame;journal->completedFrame=completedFrame;for(unsigned i=0;i<2;++i){journal->evaluations[i]=executed[i];journal->eyeFrames[i]=lastFrame[i];}journal->phase=phase;journal->thread=GetCurrentThreadId();journal->deviceStatus=unsigned(deviceStatus);journal->pendingQueries=0;for(auto& q:completions)if(q.pending)++journal->pendingQueries;journal->trialPairs=trialPairs;journal->pairLimit=pairLimit;journal->inputWidth=inputWidth;journal->inputHeight=inputHeight;journal->outputWidth=outWidth;journal->outputHeight=outHeight;MemoryBarrier();InterlockedIncrement(reinterpret_cast<volatile LONG*>(&journal->sequence));ReleaseSRWLockExclusive(&journalLock);}
void pollCompletion(ID3D11DeviceContext* c){bool changed=false;for(auto& q:completions)if(q.pending){BOOL ready{};auto hr=c->GetData(q.query.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(FAILED(hr)){ComPtr<ID3D11Device> d;c->GetDevice(&d);auto reason=d->GetDeviceRemovedReason();deviceStatus=FAILED(reason)?reason:hr;q.pending=false;changed=true;}else if(hr==S_OK && ready){completedFrame=std::max(completedFrame,q.frame);q.pending=false;changed=true;}}if(changed)stamp(FAILED(deviceStatus)?90:configured?6:0);}
struct Targets {ComPtr<ID3D11ShaderResourceView> packed,depth,previousDepth;ComPtr<ID3D11Texture2D> convertedMotion,convertedDepth,output,displayColor,privateColor;ComPtr<ID3D11UnorderedAccessView> motionUav,depthUav;ComPtr<ID3D11ShaderResourceView> outputSrv;UINT width{},height{};};
Targets targets[2];ComPtr<ID3D11ShaderResourceView> savedColor;
template<class T>T& at(void* p,size_t n){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+n);}
void log(const std::string& message){std::ofstream f(directory/L"dlss-pass.log",std::ios::app);f<<message<<'\n';}
DlssModes::Mode readMode(){auto ini=directory/L"PlagueTaleVR.ini";int value=GetPrivateProfileIntW(L"Rendering",L"DLSSMode",3,ini.c_str());if(value==1)WritePrivateProfileStringW(L"Rendering",L"DLSSMode",L"2",ini.c_str());return DlssModes::Configured(GetPrivateProfileIntW(L"Rendering",L"DLSS",1,ini.c_str())!=0,value);}
void notify(DlssModes::Mode mode,const std::string& detail){static std::string last;std::string text=std::string(mode==DlssModes::Off?"DLSS Off":DlssModes::Name(mode))+"\n"+detail;if(text==last)return;std::ofstream file(directory/L"dlss-osd.txt");file<<text;last=text;}
std::string nativeNotice(){return outWidth?"Native "+std::to_string(outWidth)+" x "+std::to_string(outHeight)+" per eye":"Native headset resolution";}
void announceSelection(DlssModes::Mode mode){if(noticeStarted && noticeMode==mode)return;noticeStarted=true;noticeMode=mode;noticeActive=noticeFailed=false;notify(mode,mode==DlssModes::Off?nativeNotice():"Switching...");}
void report(const char* state,const std::string& reason={}){
 status=state;failureReason=reason;
 std::ofstream f(directory/L"dlss-status.json");
 f<<"{\"state\":\""<<state<<"\",\"requested\":"<<requested<<",\"mode\":"<<int(wantedMode)<<",\"modeName\":\""<<DlssModes::Name(wantedMode)<<"\",\"effective\":"<<(status=="active")<<",\"diagnosticMode\":"<<diagnosticMode<<",\"sourceFrame\":"<<frame<<",\"input\":["<<inputWidth<<","<<inputHeight<<"],\"output\":["<<outWidth<<","<<outHeight<<"],\"eyeEvaluations\":["<<executed[0]<<","<<executed[1]<<"],\"lastEyeFrames\":["<<lastFrame[0]<<","<<lastFrame[1]<<"],\"reason\":\""<<reason<<"\"}\n";
 if(status=="off" && wantedMode==DlssModes::Off && (!noticeStarted || noticeMode==DlssModes::Off)){announceSelection(DlssModes::Off);notify(DlssModes::Off,nativeNotice());}
 else if(status=="active" && noticeMode==wantedMode && !noticeActive){notify(wantedMode,std::to_string(inputWidth)+" x "+std::to_string(inputHeight)+" -> "+std::to_string(outWidth)+" x "+std::to_string(outHeight)+" per eye");noticeActive=true;}
 else if(status=="failed" && !noticeFailed){announceSelection(wantedMode);notify(wantedMode,"Unavailable - press F3 for the next mode");noticeFailed=true;}
}
void fail(const std::string& reason,bool retry=false){failed=true;retryableFailure=retry;requested=active=false;WritePrivateProfileStringW(L"Rendering",L"DLSS",L"0",(directory/L"PlagueTaleVR.ini").c_str());log(reason);report("failed",reason);}
void reject(unsigned bit,const std::string& reason){if(rejectedFrame!=frame){rejectedFrame=frame;if(++pendingFrames>450){active=false;fail("required native inputs or full-size output unavailable; F3 can retry after native rendering restores",true);return;}}if(!(diagnosed[eye]&bit)){diagnosed[eye]|=bit;log("input check eye="+std::to_string(eye)+" "+reason);report("waiting",reason);}}
struct BusyScope {BusyScope(){busy=true;}~BusyScope(){busy=false;}};
bool initialize(ID3D11DeviceContext* c){
 if(attempted)return available;attempted=true;
 ComPtr<ID3D11Device> d;c->GetDevice(&d);ComPtr<ID3D11Device1> device1;
 if(FAILED(d.As(&device1)) || FAILED(c->QueryInterface(IID_PPV_ARGS(&context1))))return false;
 auto level=d->GetFeatureLevel();
 log("game device creation flags="+std::to_string(d->GetCreationFlags())+" featureLevel="+std::to_string(level)+" contextType="+std::to_string(c->GetType())+" initializationThread="+std::to_string(GetCurrentThreadId()));
 if(!runtime.Initialize(d.Get(),directory))return false;
 ComPtr<ID3DBlob> code,error;if(FAILED(D3DCompile(DlssInputsShader,strlen(DlssInputsShader),"PlagueVR-DLSS-motion",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error))){if(error)log(static_cast<const char*>(error->GetBufferPointer()));return false;}
 if(FAILED(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&inputsShader)))return false;
 code.Reset();error.Reset();
 if(FAILED(D3DCompile(DlssOutputShader,strlen(DlssOutputShader),"PlagueVR-DLSS-output",nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error)) || FAILED(d->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&outputShader)))return false;
 const GUID shaderCode={0x85ce7341,0xf6c7,0x4e8a,{0x90,0x13,0x24,0x70,0x1a,0x55,0x40,0x91}};outputShader->SetPrivateData(shaderCode,UINT(code->GetBufferSize()),code->GetBufferPointer());
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(FAILED(d->CreateBuffer(&bd,nullptr,&dimensions)))return false;
 available=true;return true;
}
bool texture(ID3D11Device* d,UINT w,UINT h,DXGI_FORMAT format,UINT bind,ComPtr<ID3D11Texture2D>& result){D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.Format=format;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=bind;return SUCCEEDED(d->CreateTexture2D(&td,nullptr,&result));}
bool isolateColor(ID3D11DeviceContext* c,Targets& t,ID3D11Texture2D* source){
 D3D11_TEXTURE2D_DESC native{};source->GetDesc(&native);D3D11_TEXTURE2D_DESC owned{};if(t.privateColor)t.privateColor->GetDesc(&owned);
 if(!t.privateColor || owned.Width!=native.Width || owned.Height!=native.Height || owned.Format!=native.Format || owned.MipLevels!=native.MipLevels){t.privateColor.Reset();auto desc=native;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;desc.CPUAccessFlags=desc.MiscFlags=0;ComPtr<ID3D11Device> d;c->GetDevice(&d);if(FAILED(d->CreateTexture2D(&desc,nullptr,&t.privateColor)))return false;log("allocated owned HDR input eye="+std::to_string(eye)+" mips="+std::to_string(desc.MipLevels));}
 c->CopyResource(t.privateColor.Get(),source);return true;
}
void restore(){
 if(!configured)return;
 if(config && at<int>(config,0x1c0)==2)at<int>(config,0x1c0)=savedType;
 auto* mask=reinterpret_cast<volatile LONG64*>(base+0x152ee80);InterlockedAnd64(mask,~LONG64(0x80000000ull));InterlockedOr64(mask,LONG64(savedOn&0x80000000ull));
 // Queue a scale change for the engine's frame-boundary resource rebuild.
 // Its direct setter synchronizes applied/requested values and bypasses that
 // change detection, leaving the old allocations behind.
 if(ownedRenderer && std::abs(at<float>(ownedRenderer,0x21fcc)-scale)<.00001f)at<float>(ownedRenderer,0x21fcc)=savedScale;
 configured=false;active=false;log("restored native AA type/force bit and primary scale");
 stamp(0);
 if(!failed)report("off");
}
bool allocate(ID3D11Device* d,Targets& t,UINT w,UINT h){
 if(t.output && t.displayColor && t.outputSrv && t.convertedMotion && t.convertedDepth && t.motionUav && t.depthUav && t.width==w && t.height==h)return true;
 t.output.Reset();t.displayColor.Reset();t.outputSrv.Reset();t.convertedMotion.Reset();t.convertedDepth.Reset();t.motionUav.Reset();t.depthUav.Reset();
 if(!texture(d,w,h,DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,t.convertedMotion) || !texture(d,w,h,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,t.convertedDepth) || !texture(d,outWidth,outHeight,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,t.output))return false;
 if(!texture(d,outWidth,outHeight,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE,t.displayColor))return false;
 if(FAILED(d->CreateUnorderedAccessView(t.convertedMotion.Get(),nullptr,&t.motionUav)) || FAILED(d->CreateUnorderedAccessView(t.convertedDepth.Get(),nullptr,&t.depthUav)) || FAILED(d->CreateShaderResourceView(t.displayColor.Get(),nullptr,&t.outputSrv)))return false;
 t.width=w;t.height=h;log("allocate eye="+std::to_string(eye)+" input="+std::to_string(w)+"x"+std::to_string(h)+" output="+std::to_string(outWidth)+"x"+std::to_string(outHeight));return true;
}
bool queueAudit(ID3D11DeviceContext* c,ID3D11Texture2D* color,Targets& t,ID3D11Buffer* global){
 auto& a=audits[eye];if(a.complete)return true;ComPtr<ID3D11Device> d;c->GetDevice(&d);
 ID3D11Texture2D* sources[]={color,t.convertedDepth.Get(),t.convertedMotion.Get()};
 for(unsigned i=0;i<3;++i){D3D11_TEXTURE2D_DESC td{};sources[i]->GetDesc(&td);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;td.MiscFlags=0;if(FAILED(d->CreateTexture2D(&td,nullptr,&a.inputs[i])))return false;c->CopyResource(a.inputs[i].Get(),sources[i]);}
 D3D11_BUFFER_DESC bd{};global->GetDesc(&bd);bd.BindFlags=0;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;bd.MiscFlags=0;bd.StructureByteStride=0;if(FAILED(d->CreateBuffer(&bd,nullptr,&a.global)))return false;c->CopyResource(a.global.Get(),global);
 D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};if(FAILED(d->CreateQuery(&desc,&a.complete)))return false;c->End(a.complete.Get());a.frame=frame;a.width=t.width;a.height=t.height;a.jitter[0]=jitterNdc[0]*float(t.width)*.5f;a.jitter[1]=-jitterNdc[1]*float(t.height)*.5f;
 auditState[eye].store(1,std::memory_order_release);log("queued native input audit eye="+std::to_string(eye)+" frame="+std::to_string(frame)+"; no NGX evaluation");return true;
}
}
void Initialize(const std::filesystem::path& path,uintptr_t gameBase){directory=path;base=gameBase;DlssImageAudit::Initialize(path);wchar_t name[128]{};swprintf_s(name,L"Local\\PlagueTaleVR-DlssJournal-%lu",GetCurrentProcessId());journalMapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(DlssJournal),name);if(journalMapping)journal=static_cast<DlssJournal*>(MapViewOfFile(journalMapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(DlssJournal)));if(journal){*journal={};journal->abi=1;journal->size=sizeof(DlssJournal);journal->pid=GetCurrentProcessId();}stamp(0);report("off");}
bool Busy(){return busy;}
void Cycle(){auto next=DlssModes::Next(readMode());auto ini=directory/L"PlagueTaleVR.ini";auto value=std::to_wstring(int(next));WritePrivateProfileStringW(L"Rendering",L"DLSSMode",value.c_str(),ini.c_str());WritePrivateProfileStringW(L"Rendering",L"DLSS",next==DlssModes::Off?L"0":L"1",ini.c_str());announceSelection(next);}
const char* Status(){return status.c_str();}
bool Effective(){return status=="active";}
bool CaptureRequested(){if(automaticCapture && configured && requested && !failed && !capturedInputs && GetTickCount64()-activationTick>=1500){capturedInputs=true;return true;}return false;}
void Prepare(void* renderer,ID3D11DeviceContext* c,bool vr,UINT ow,UINT oh,bool startupReady){
 auto now=GetTickCount64();if(now-pollTick>=100){pollTick=now;wantedMode=readMode();requested=wantedMode!=DlssModes::Off;announceSelection(wantedMode);}
 if(c)pollCompletion(c);
 if(!configured && !outWidth && ow && oh){outWidth=ow;outHeight=oh;if(wantedMode==DlssModes::Off)report("off");}
 if(configured && (wantedMode!=configuredMode || !vr || !requested || failed)){restore();if(c){if(!transitionFence){ComPtr<ID3D11Device> d;c->GetDevice(&d);D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};if(FAILED(d->CreateQuery(&q,&transitionFence))){fail("mode-switch GPU fence unavailable");return;}}c->End(transitionFence.Get());transitionPending=true;}if(!failed)report(wantedMode==DlssModes::Off?"off":"switching");}
 if(transitionPending){if(!c)return;BOOL ready{};auto result=c->GetData(transitionFence.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(result==S_FALSE || (SUCCEEDED(result)&&!ready))return;if(FAILED(result)){transitionPending=false;fail("mode-switch GPU fence failed");return;}transitionPending=false;BusyScope state;if(!runtime.CacheEyes()){fail("completed eye-pair cache unavailable");return;}for(auto& t:targets)t={};log("GPU-completed pair cached before mode change; NGX input/output resources retained");}
 if(failed && retryableFailure && requested && !configured && SUCCEEDED(deviceStatus)){failed=retryableFailure=false;failureReason.clear();++resetGeneration;log("retrying transient native input failure");}
 if(failed && requested){requested=false;WritePrivateProfileStringW(L"Rendering",L"DLSS",L"0",(directory/L"PlagueTaleVR.ini").c_str());report("failed",failureReason);}
 if(!vr || !requested || failed){restore();return;}
 if(!configured && !startupReady){if(status!="pending-startup")report("pending-startup");notify(wantedMode,"Waiting for tracked gameplay...");return;}
 if(!c || !ow || !oh || (configured && ownedRenderer!=renderer))return;
 if(FAILED(deviceStatus)){fail("GPU completion query reports device loss; DLSS stopped");stamp(90);return;}
 BusyScope busyScope;if(!initialize(c)){fail("NGX or input shaders unavailable; native rendering retained");return;}
 if(configured)return;
 // Exact prefixes from this executable's scale setter and AA type getter.
 const uint8_t setter[]={0xf3,0x0f,0x11,0x89,0xc4,0x1f,0x02,0x00},getter[]={0x8b,0x81,0xc0,0x01,0x00,0x00,0xc3};
 const uint8_t forceOn[]={0x48,0x0b,0x0d,0x56,0x74,0xdd,0x00};
 const uint8_t scaleCompare[]={0x0f,0x2e,0x83,0xcc,0x1f,0x02,0x00,0x75},scaleApply[]={0xf3,0x0f,0x10,0x8e,0xcc,0x1f,0x02,0x00};
 if(memcmp(reinterpret_cast<void*>(base+0x7605b0),setter,sizeof(setter)) || memcmp(reinterpret_cast<void*>(base+0x7ee410),getter,sizeof(getter)) || memcmp(reinterpret_cast<void*>(base+0x757a23),forceOn,sizeof(forceOn)) || memcmp(reinterpret_cast<void*>(base+0x76055b),scaleCompare,sizeof(scaleCompare)) || memcmp(reinterpret_cast<void*>(base+0x62a822),scaleApply,sizeof(scaleApply)) || *reinterpret_cast<uint64_t*>(base+0xfc48c0)!=0x80000000ull){fail("native scale/AA signature mismatch; disabled");return;}
 runtime.SelectMode(wantedMode);if(!runtime.Quality(ow,oh,inputWidth,inputHeight)){fail("selected mode dimensions unavailable",true);return;}
 config=*reinterpret_cast<void**>(base+0x1522970);if(!config)return;
 savedType=at<int>(config,0x1c0);savedScale=at<float>(renderer,0x21fc4);if(savedType<0 || savedType>2 || !std::isfinite(savedScale) || savedScale<.25f || savedScale>1.f || std::abs(at<float>(renderer,0x21fcc)-savedScale)>.00001f)return;
 outWidth=ow;outHeight=oh;scale=float(inputWidth)/float(outWidth);ownedRenderer=renderer;configuredMode=wantedMode;
 savedOn=uint64_t(InterlockedOr64(reinterpret_cast<volatile LONG64*>(base+0x152ee80),LONG64(0x80000000ull)));
 at<int>(config,0x1c0)=2;at<float>(renderer,0x21fcc)=scale;configured=true;lastFrame[0]=lastFrame[1]=0;pendingFrames=0;rejectedFrame=0;diagnosed[0]=diagnosed[1]=0;seenTargets[0]=seenTargets[1]=0;activationTick=now;capturedInputs=false;
 log(std::string("configured ")+DlssModes::Name(wantedMode)+" scale="+std::to_string(scale)+" requested input="+std::to_string(inputWidth)+"x"+std::to_string(inputHeight));
 report("waiting");
 pairLimit=GetPrivateProfileIntW(L"Diagnostics",L"DLSSMaxTestPairs",0,(directory/L"PlagueTaleVR.ini").c_str());automaticCapture=GetPrivateProfileIntW(L"Diagnostics",L"DLSSAutoCapture",0,(directory/L"PlagueTaleVR.ini").c_str())!=0;trialPairs=0;stamp(1);
 inputAudit=GetPrivateProfileIntW(L"Diagnostics",L"DLSSInputAudit",0,(directory/L"PlagueTaleVR.ini").c_str())!=0;if(inputAudit){for(unsigned i=0;i<2;++i){auditState[i].store(0,std::memory_order_release);audits[i]={};}log("input audit mode: NGX evaluations and output substitution disabled");}
 diagnosticMode=std::min(3u,GetPrivateProfileIntW(L"Diagnostics",L"DLSSDiagnosticMode",0,(directory/L"PlagueTaleVR.ini").c_str()));
 log("diagnostic mode="+std::to_string(diagnosticMode)+"; 0=normal, 1=scale only, 2=conversion only, 3=evaluate without output substitution");
}
void Begin(uint64_t id,bool tracked,bool capture){frame=id;eye=-1;trace=capture;active=configured && tracked && requested && !failed;if(configured && !tracked && status=="active")report("waiting","tracked native interval unavailable");if(trace)report(failed?"failed":active?(lastFrame[0]&&lastFrame[1]?(diagnosticMode?"diagnostic":"active"):"waiting"):"off",failureReason);}
void Eye(int which){if(which!=eye && which>=0 && which<2){targets[which].packed.Reset();targets[which].depth.Reset();targets[which].previousDepth.Reset();}eye=which;}
void Projection(CameraMath::Matrix& current,CameraMath::Matrix& previous,const CameraMath::Matrix& nativeCurrent,const CameraMath::Matrix& nativePrevious){
 if(!active)return;
 // The native camera is symmetric before per-eye processing. Retain its TAA
 // subpixel offsets when replacing its lens with the runtime eye projection.
 jitterNdc[0]=nativeCurrent.m[8];jitterNdc[1]=nativeCurrent.m[9];
 current.m[8]+=nativeCurrent.m[8];current.m[9]+=nativeCurrent.m[9];previous.m[8]+=nativePrevious.m[8];previous.m[9]+=nativePrevious.m[9];
}
void BeforeDraw(ID3D11DeviceContext* c){
 if(!active || busy || eye<0 || eye>1 || c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return;
 auto& t=targets[eye];
 if(diagnosticMode!=1 && !t.packed && lastInputAttempt[eye]!=frame){
  ID3D11RenderTargetView* bound[6]{};ComPtr<ID3D11DepthStencilView> depth;c->OMGetRenderTargets(6,bound,&depth);ComPtr<ID3D11RenderTargetView> views[6];for(unsigned i=0;i<6;++i)views[i].Attach(bound[i]);
  for(unsigned i=0;i<6;++i)if(views[i])seenTargets[eye]|=1u<<i;
  if(views[4] && views[5] && depth){
   ComPtr<ID3D11Resource> motionResource,depthResource;ComPtr<ID3D11Texture2D> motionTexture,depthTexture;views[4]->GetResource(&motionResource);depth->GetResource(&depthResource);motionResource.As(&motionTexture);depthResource.As(&depthTexture);
   if(motionTexture && depthTexture){D3D11_TEXTURE2D_DESC md{},dd{};motionTexture->GetDesc(&md);depthTexture->GetDesc(&dd);D3D11_RENDER_TARGET_VIEW_DESC vd{};views[4]->GetDesc(&vd);
    if(!(diagnosed[eye]&16)){diagnosed[eye]|=16;log("candidate MRT eye="+std::to_string(eye)+" motion="+std::to_string(md.Width)+"x"+std::to_string(md.Height)+" format="+std::to_string(md.Format)+" view="+std::to_string(vd.Format)+" array="+std::to_string(md.ArraySize)+" bind="+std::to_string(md.BindFlags)+" depth="+std::to_string(dd.Width)+"x"+std::to_string(dd.Height)+" format="+std::to_string(dd.Format)+" array="+std::to_string(dd.ArraySize)+" bind="+std::to_string(dd.BindFlags));}
    if(vd.Format==DXGI_FORMAT_R32_UINT && md.Width==dd.Width && md.Height==dd.Height && md.ArraySize==1 && dd.ArraySize==1 && md.SampleDesc.Count==1 && dd.SampleDesc.Count==1){ComPtr<ID3D11Device> d;c->GetDevice(&d);D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;sd.Format=DXGI_FORMAT_R32_UINT;
     lastInputAttempt[eye]=frame;HRESULT motionResult=d->CreateShaderResourceView(motionTexture.Get(),&sd,&t.packed);HRESULT depthResult=E_PENDING,previousResult=E_PENDING;if(SUCCEEDED(motionResult)){sd.Format=DlssDepthViewFormat(dd.Format);depthResult=sd.Format!=DXGI_FORMAT_UNKNOWN?d->CreateShaderResourceView(depthTexture.Get(),&sd,&t.depth):E_INVALIDARG;if(FAILED(depthResult))t.packed.Reset();
      ComPtr<ID3D11Resource> previous;views[5]->GetResource(&previous);sd.Format=DXGI_FORMAT_R32_FLOAT;previousResult=d->CreateShaderResourceView(previous.Get(),&sd,&t.previousDepth);if(FAILED(previousResult))t.packed.Reset();}
     if((FAILED(motionResult)||FAILED(depthResult)||FAILED(previousResult)) && !(diagnosed[eye]&32)){diagnosed[eye]|=32;log("native SRV creation eye="+std::to_string(eye)+" motion="+std::to_string(motionResult)+" depth="+std::to_string(depthResult)+" previous="+std::to_string(previousResult));}
    }
   }
  }
 }
 if(lastFrame[eye]==frame)return;
 // The captured native HDR spatial upscale runs BEFORE tone mapping. At the
 // tone-map draw the native input has already been enlarged to full size.
 ComPtr<ID3D11PixelShader> ps;c->PSGetShader(&ps,nullptr,nullptr);if(!ps)return;const GUID tag={0x85ce7341,0xf6c7,0x4e8a,{0x90,0x13,0x24,0x70,0x1a,0x55,0x40,0x91}};
 UINT bytes{};ps->GetPrivateData(tag,&bytes,nullptr);if(!bytes || bytes>65536)return;
 // Cache shader identity in private data; do not hash bytecode each draw.
 const GUID toneTag={0x848c9d21,0x3ea9,0x4091,{0x99,0x9a,0x0b,0x62,0x5e,0xaa,0x44,0x12}};UINT role{},roleBytes=4;
 if(FAILED(ps->GetPrivateData(toneTag,&roleBytes,&role))){std::vector<uint8_t> code(bytes);if(FAILED(ps->GetPrivateData(tag,&bytes,code.data())))return;uint64_t hash=14695981039346656037ull;for(auto b:code){hash^=b;hash*=1099511628211ull;}role=hash==0x284e696572dde4faull?1:2;ps->SetPrivateData(toneTag,4,&role);}if(role!=1)return;
 if(diagnosticMode==1 && !inputAudit){lastFrame[eye]=frame;return;}
 if(!t.packed || !t.depth || !t.previousDepth){reject(1,"native upscale missing motion or depth; observed RTV mask="+std::to_string(seenTargets[eye]));return;}
 ComPtr<ID3D11ShaderResourceView> color;c->PSGetShaderResources(0,1,&color);if(!color)return;ComPtr<ID3D11Resource> resource;color->GetResource(&resource);ComPtr<ID3D11Texture2D> colorTexture;resource.As(&colorTexture);if(!colorTexture)return;D3D11_TEXTURE2D_DESC cd{};colorTexture->GetDesc(&cd);
 if(cd.Format!=DXGI_FORMAT_R11G11B10_FLOAT || cd.ArraySize!=1 || cd.SampleDesc.Count!=1 || (cd.Width>=outWidth || cd.Height>=outHeight)){reject(2,"HDR input dimensions="+std::to_string(cd.Width)+"x"+std::to_string(cd.Height)+" format="+std::to_string(cd.Format));return;}
 ComPtr<ID3D11RenderTargetView> target;c->OMGetRenderTargets(1,&target,nullptr);if(!target)return;ComPtr<ID3D11Resource> outputResource;target->GetResource(&outputResource);ComPtr<ID3D11Texture2D> outputTexture;outputResource.As(&outputTexture);if(!outputTexture)return;D3D11_TEXTURE2D_DESC od{};outputTexture->GetDesc(&od);if(od.Width!=outWidth || od.Height!=outHeight || od.Format!=DXGI_FORMAT_R11G11B10_FLOAT){reject(4,"HDR output dimensions="+std::to_string(od.Width)+"x"+std::to_string(od.Height)+" format="+std::to_string(od.Format));return;}
 ComPtr<ID3D11Resource> packedResource;t.packed->GetResource(&packedResource);ComPtr<ID3D11Texture2D> packedTexture;packedResource.As(&packedTexture);D3D11_TEXTURE2D_DESC pd{};packedTexture->GetDesc(&pd);if(pd.Width!=cd.Width || pd.Height!=cd.Height){reject(8,"motion dimensions="+std::to_string(pd.Width)+"x"+std::to_string(pd.Height)+" differ from HDR input");return;}
 inputWidth=cd.Width;inputHeight=cd.Height;
 ComPtr<ID3D11Buffer> global;c->PSGetConstantBuffers(0,1,&global);if(!global)return;D3D11_BUFFER_DESC bd{};global->GetDesc(&bd);if(bd.ByteWidth!=1440)return;
 BusyScope busyScope;bool good=false;
 stamp(2);
 {
  DlssComputeState state(context1.Get());ComPtr<ID3D11Device> d;c->GetDevice(&d);if(!allocate(d.Get(),t,cd.Width,cd.Height)){fail("allocation failed");return;}
  UINT size[4]={cd.Width,cd.Height,0,0};c->UpdateSubresource(dimensions.Get(),0,nullptr,size,0,0);ID3D11Buffer* buffers[]={global.Get(),dimensions.Get()};c->CSSetConstantBuffers(0,2,buffers);ID3D11ShaderResourceView* sources[]={t.packed.Get(),t.depth.Get(),t.previousDepth.Get()};c->CSSetShaderResources(0,3,sources);ID3D11UnorderedAccessView* uavs[]={t.motionUav.Get(),t.depthUav.Get()};c->CSSetUnorderedAccessViews(0,2,uavs,nullptr);c->CSSetShader(inputsShader.Get(),nullptr,0);c->Dispatch((cd.Width+7)/8,(cd.Height+7)/8,1);
  if(inputAudit){if(!queueAudit(c,colorTexture.Get(),t,global.Get()))fail("native input audit staging failed");stamp(7);return;}
  // Native HDR is pooled and reused sequentially between eyes. Keep every
  // input passed to NGX in our own persistent per-eye resources, matching the
  // successful captured-input replay path.
  if(!isolateColor(c,t,colorTexture.Get())){fail("owned per-eye HDR input allocation failed");return;}
 }
 if(diagnosticMode==2){lastFrame[eye]=frame;return;}
 // NGX preserves the D3D11 immediate context itself. Invoke it in the native
 // context state, after restoring the specific conversion bindings.
 stamp(3);good=runtime.Evaluate(c,eye,t.privateColor.Get(),t.convertedDepth.Get(),t.convertedMotion.Get(),t.output.Get(),cd.Width,cd.Height,outWidth,outHeight,jitterNdc[0]*float(cd.Width)*.5f,-jitterNdc[1]*float(cd.Height)*.5f,!lastFrame[eye] || frame-lastFrame[eye]>8 || eyeGeneration[eye]!=resetGeneration.load());stamp(4);
 if(!good){fail("DLSS evaluation failed; native pass retained and scale restored next interval");return;}
 lastFrame[eye]=frame;eyeGeneration[eye]=resetGeneration.load();++executed[eye];
 if(diagnosticMode!=3){
  // Keep the SDK's UAV output out of the native pixel-shader bindings. The
  // explicit copy runs with compute/OM outputs unbound, then the native draw
  // samples a distinct per-eye SRV-only texture. No CPU/GPU wait is added.
  {DlssComputeState state(context1.Get());c->CopyResource(t.displayColor.Get(),t.output.Get());}
  savedColor=color;savedPixel=ps;auto* replacement=t.outputSrv.Get();c->PSSetShaderResources(0,1,&replacement);c->PSSetShader(outputShader.Get(),nullptr,0);substituted=true;
 }
 if(lastFrame[0]==frame && lastFrame[1]==frame && !diagnosticMode && (status!="active" || trace))report("active");
 if(trace || executed[eye]<=2)log("evaluated frame="+std::to_string(frame)+" eye="+std::to_string(eye)+" input="+std::to_string(cd.Width)+"x"+std::to_string(cd.Height)+" output="+std::to_string(outWidth)+"x"+std::to_string(outHeight)+" jitterNdc="+std::to_string(jitterNdc[0])+","+std::to_string(jitterNdc[1]));
}
void AfterDraw(ID3D11DeviceContext* c){if(!substituted)return;BusyScope busyScope;if(trace){ComPtr<ID3D11RenderTargetView> target;c->OMGetRenderTargets(1,&target,nullptr);ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> output;if(target)target->GetResource(&resource);if(resource)resource.As(&output);float scales[4]{};if(ownedRenderer)memcpy(scales,static_cast<char*>(ownedRenderer)+0x21fc4,sizeof(scales));DlssImageAudit::Capture(context1.Get(),frame,eye,targets[eye].privateColor.Get(),targets[eye].output.Get(),output.Get(),scales,config?at<int>(config,0x1c0):-1);}auto* original=savedColor.Get();c->PSSetShaderResources(0,1,&original);c->PSSetShader(savedPixel.Get(),nullptr,0);savedColor.Reset();savedPixel.Reset();substituted=false;}
void End(){
 if(active && inputAudit){
  if(audits[0].complete && audits[1].complete){requested=false;WritePrivateProfileStringW(L"Rendering",L"DLSS",L"0",(directory/L"PlagueTaleVR.ini").c_str());log("both native inputs queued; restoring full native rendering without evaluating DLSS");report("input-audit-pending");}
  active=false;eye=-1;return;
 }
 if(active){
  if(lastFrame[0]==frame && lastFrame[1]==frame){
   pendingFrames=0;++trialPairs;const char* running=diagnosticMode?"diagnostic":"active";
   if(status!=running || trace)report(running,diagnosticMode?"diagnostic only; native upscale retained":"");
   if(context1){ComPtr<ID3D11Device> d;context1->GetDevice(&d);for(auto& q:completions)if(!q.pending){if(!q.query){D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};if(FAILED(d->CreateQuery(&desc,&q.query)))break;}context1->End(q.query.Get());q.frame=frame;q.pending=true;break;}}
   if(pairLimit && trialPairs>=pairLimit){requested=false;WritePrivateProfileStringW(L"Rendering",L"DLSS",L"0",(directory/L"PlagueTaleVR.ini").c_str());log("bounded diagnostic reached "+std::to_string(trialPairs)+" pairs; restoring native rendering");report("stopping","bounded diagnostic complete");}
  }else if(status=="active" || status=="diagnostic")report("waiting","both native eye intervals were not completed");
  stamp(5);
 }
 active=false;eye=-1;
}
void FlushInputAudit(){
 if(context1){BusyScope busyScope;DlssImageAudit::Flush(context1.Get());}
 if(!context1 || !inputAudit)return;BusyScope busyScope;
 for(unsigned e=0;e<2;++e){if(auditState[e].load(std::memory_order_acquire)!=1)continue;auto& a=audits[e];BOOL ready{};auto hr=context1->GetData(a.complete.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(hr!=S_OK || !ready)continue;
  auto path=directory/L"dlss-input-audit"/(L"frame-"+std::to_wstring(a.frame))/(L"eye-"+std::to_wstring(e));std::error_code ec;std::filesystem::create_directories(path,ec);if(ec)return;
  const wchar_t* names[]={L"color-r11g11b10.bin",L"depth-r32f.bin",L"motion-rg16f.bin"};bool good=true;
  for(unsigned i=0;i<3;++i){D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context1->Map(a.inputs[i].Get(),0,D3D11_MAP_READ,0,&mapped))){good=false;break;}std::ofstream file(path/names[i],std::ios::binary);for(UINT y=0;y<a.height;++y)file.write(static_cast<const char*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(a.width)*4);good=bool(file);context1->Unmap(a.inputs[i].Get(),0);if(!good)break;}
  if(good){D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(context1->Map(a.global.Get(),0,D3D11_MAP_READ,0,&mapped)))good=false;else{std::ofstream file(path/L"global.bin",std::ios::binary);file.write(static_cast<const char*>(mapped.pData),1440);good=bool(file);context1->Unmap(a.global.Get(),0);}}
  if(!good)return;
  std::ofstream meta(path/L"metadata.json");meta<<"{\"sourceFrame\":"<<a.frame<<",\"eye\":"<<e<<",\"inputWidth\":"<<a.width<<",\"inputHeight\":"<<a.height<<",\"outputWidth\":"<<outWidth<<",\"outputHeight\":"<<outHeight<<",\"jitterPixels\":["<<a.jitter[0]<<","<<a.jitter[1]<<"],\"evaluatedDLSS\":false}\n";a.saved=true;auditState[e].store(2,std::memory_order_release);log("saved native input audit eye="+std::to_string(e)+" frame="+std::to_string(a.frame));
 }
 if(auditState[0].load(std::memory_order_acquire)==2 && auditState[1].load(std::memory_order_acquire)==2 && status!="input-audit-saved")report("input-audit-saved","inputs saved without DLSS evaluation");
}
void ResetHistory(){++resetGeneration;}
#ifdef PLAGUE_DLSS_TEST_ACCESS
ID3D11Texture2D* TestTexture(int which,unsigned role){if(which<0 || which>1)return nullptr;return role==2?targets[which].displayColor.Get():role?targets[which].output.Get():targets[which].privateColor.Get();}
#endif
void Shutdown(){restore();DlssImageAudit::Shutdown();runtime.Shutdown();transitionFence.Reset();transitionPending=false;for(auto& a:audits)a={};for(auto& q:completions)q={};for(auto& t:targets)t={};savedColor.Reset();savedPixel.Reset();inputsShader.Reset();outputShader.Reset();dimensions.Reset();context1.Reset();if(journal)UnmapViewOfFile(journal);journal=nullptr;if(journalMapping)CloseHandle(journalMapping);journalMapping=nullptr;}
}
