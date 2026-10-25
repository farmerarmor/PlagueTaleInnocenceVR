#include "DlssPass.h"
#include "DlssNativeInputs.h"
#include "GpuCapture.h"
#include "DlssJournal.h"
#include <MinHook.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
using Microsoft::WRL::ComPtr;
static unsigned checks{};
static void require(bool value,const char* label){++checks;if(!value){fprintf(stderr,"FAIL: %s\n",label);exit(1);}}
template<class T>T& at(void* p,size_t n){return *reinterpret_cast<T*>(static_cast<char*>(p)+n);}
int wmain(int argc,wchar_t** argv){
 require(argc==3 || argc==4,"runtime directory and captured native upscale bytecode supplied");
 bool audit=argc==4 && !wcscmp(argv[3],L"--input-audit");
 bool pipeline=argc==4 && !wcscmp(argv[3],L"--pipeline-stress");
 bool conversion=argc==4 && !wcscmp(argv[3],L"--conversion-only");
 bool discard=argc==4 && !wcscmp(argv[3],L"--evaluate-only");
 bool predicated=argc==4 && !wcscmp(argv[3],L"--predication");
 bool outputAudit=argc==4 && !wcscmp(argv[3],L"--output-audit");
 bool transitions=argc==4 && !wcscmp(argv[3],L"--transitions");
 bool legacyMode=argc==4 && !wcscmp(argv[3],L"--legacy-mode");
 bool notices=argc==4 && !wcscmp(argv[3],L"--notices");
 bool cycle=argc==4 && !wcscmp(argv[3],L"--cycle");
 bool balancedStartup=argc==4 && !wcscmp(argv[3],L"--balanced-startup");
 bool renderCycle=argc==4 && !wcscmp(argv[3],L"--cycle-render");
 bool debug=argc==4 && !wcscmp(argv[3],L"--debug-stress");
 bool hooks=audit || pipeline || conversion || discard || predicated || outputAudit || transitions || legacyMode || notices || cycle || renderCycle || (argc==4 && !wcscmp(argv[3],L"--hooks-stress"));
 bool bounded=argc==4 && !wcscmp(argv[3],L"--bounded");
 bool stress=balancedStartup || debug || bounded || (hooks && !audit) || (argc==4 && !wcscmp(argv[3],L"--stress"));
 bool fullResolution=audit || stress || (argc==4 && !wcscmp(argv[3],L"--native-resolution"));
 UINT iw=fullResolution?2667:512,ih=fullResolution?3179:512;const UINT ow=fullResolution?4000:768,oh=fullResolution?4768:768;
 if(balancedStartup){iw=2320;ih=2764;} // native even-height allocation at Balanced
 auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
 auto ini=root/L"PlagueTaleVR.ini";WritePrivateProfileStringW(L"Rendering",L"DLSS",L"1",ini.c_str());
 WritePrivateProfileStringW(L"Rendering",L"DLSSMode",balancedStartup?L"3":legacyMode?L"1":L"2",ini.c_str());
 WritePrivateProfileStringW(L"Diagnostics",L"DLSSMaxTestPairs",bounded?L"30":L"0",ini.c_str());
 WritePrivateProfileStringW(L"Diagnostics",L"DLSSInputAudit",audit?L"1":L"0",ini.c_str());
 WritePrivateProfileStringW(L"Diagnostics",L"DLSSDiagnosticMode",conversion?L"2":discard?L"3":L"0",ini.c_str());
 // Simulate only the validated native fields and instruction signatures. The
 // production pass must queue the request without calling the direct setter.
 auto* image=static_cast<char*>(VirtualAlloc(nullptr,0x1530000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 std::vector<char> renderer(0x3b060),configuration(0x200);require(image!=nullptr,"native layout fixture");
 const unsigned char setter[]={0xf3,0x0f,0x11,0x89,0xc4,0x1f,0x02,0x00},getter[]={0x8b,0x81,0xc0,0x01,0x00,0x00,0xc3},force[]={0x48,0x0b,0x0d,0x56,0x74,0xdd,0x00},compare[]={0x0f,0x2e,0x83,0xcc,0x1f,0x02,0x00,0x75},apply[]={0xf3,0x0f,0x10,0x8e,0xcc,0x1f,0x02,0x00};
 memcpy(image+0x7605b0,setter,sizeof(setter));memcpy(image+0x7ee410,getter,sizeof(getter));memcpy(image+0x757a23,force,sizeof(force));memcpy(image+0x76055b,compare,sizeof(compare));memcpy(image+0x62a822,apply,sizeof(apply));
 at<uint64_t>(image,0xfc48c0)=0x80000000ull;at<void*>(image,0x1522970)=configuration.data();
 at<float>(renderer.data(),0x21fc4)=at<float>(renderer.data(),0x21fcc)=1.f;
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,debug?D3D11_CREATE_DEVICE_DEBUG:0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)),"hardware D3D11 device");
 ComPtr<ID3D11InfoQueue> info;if(debug)require(SUCCEEDED(d.As(&info)),"D3D debug-layer queue");
 if(hooks){require(MH_Initialize()==MH_OK,"initialize full production D3D hooks");require(GpuCapture::Install(d.Get(),c.Get(),root,reinterpret_cast<uintptr_t>(image)),"install production hooks around actual NGX");require(MH_EnableHook(MH_ALL_HOOKS)==MH_OK,"enable production D3D hooks");}
 else DlssPass::Initialize(root,reinterpret_cast<uintptr_t>(image));
 DlssPass::Prepare(renderer.data(),c.Get(),true,ow,oh,false);
 require(at<float>(renderer.data(),0x21fc4)==1.f && at<float>(renderer.data(),0x21fcc)==1.f && at<int>(configuration.data(),0x1c0)==0 && !DlssPass::Effective(),"saved DLSS startup request leaves native allocation and AA unchanged before gameplay is ready");
 DlssPass::Prepare(renderer.data(),c.Get(),true,ow,oh);
 require(at<float>(renderer.data(),0x21fc4)==1.f,"applied scale remains native until frame-boundary rebuild");
 if(legacyMode)require(GetPrivateProfileIntW(L"Rendering",L"DLSSMode",-1,ini.c_str())==2,"legacy mode is migrated to persisted Quality before configuration");
 require(std::abs(at<float>(renderer.data(),0x21fcc)-float(iw)/float(ow))<.0001f,"requested scale queued for native change detection");
 require(!strcmp(DlssPass::Status(),"waiting") && !DlssPass::Effective(),"request is not advertised as effective DLSS");
 at<float>(renderer.data(),0x21fc4)=at<float>(renderer.data(),0x21fcc); // native rebuild applies the request
 auto texture=[&](UINT width,UINT height,DXGI_FORMAT format,UINT bind){D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.Format=format;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.BindFlags=bind;ComPtr<ID3D11Texture2D> t;require(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&t)),"native texture fixture");return t;};
 auto motion=texture(iw,ih,DXGI_FORMAT_R32_UINT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS),previous=texture(iw,ih,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS),depth=texture(iw,ih,DXGI_FORMAT_R32G8X24_TYPELESS,D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE),color=texture(iw,ih,DXGI_FORMAT_R11G11B10_FLOAT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE),output=texture(ow,oh,DXGI_FORMAT_R11G11B10_FLOAT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
 ComPtr<ID3D11RenderTargetView> mr,pr,cr,outputRtv;D3D11_RENDER_TARGET_VIEW_DESC rdesc{};rdesc.Format=DXGI_FORMAT_R32_UINT;rdesc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
 require(SUCCEEDED(d->CreateRenderTargetView(motion.Get(),&rdesc,&mr)) && SUCCEEDED(d->CreateRenderTargetView(previous.Get(),nullptr,&pr)) && SUCCEEDED(d->CreateRenderTargetView(color.Get(),nullptr,&cr)) && SUCCEEDED(d->CreateRenderTargetView(output.Get(),nullptr,&outputRtv)),"native motion/HDR render targets");
 D3D11_DEPTH_STENCIL_VIEW_DESC dd{};dd.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;ComPtr<ID3D11DepthStencilView> ds;require(SUCCEEDED(d->CreateDepthStencilView(depth.Get(),&dd,&ds)),"captured native 32-bit depth plus stencil");
 D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;ComPtr<ID3D11ShaderResourceView> badDepth;
 require(d->CreateShaderResourceView(depth.Get(),&sd,&badDepth)==E_INVALIDARG,"reproduce Probe37 E_INVALIDARG with the incompatible 24-bit depth view");sd.Format=DlssDepthViewFormat(DXGI_FORMAT_R32G8X24_TYPELESS);ComPtr<ID3D11ShaderResourceView> validDepth;require(SUCCEEDED(d->CreateShaderResourceView(depth.Get(),&sd,&validDepth)),"captured 32-bit depth view succeeds with exact production mapping");
 ComPtr<ID3D11Texture2D> material[4];ComPtr<ID3D11RenderTargetView> materialView[4];for(unsigned i=0;i<4;++i){material[i]=texture(iw,ih,DXGI_FORMAT_R32_UINT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);require(SUCCEEDED(d->CreateRenderTargetView(material[i].Get(),nullptr,&materialView[i])),"captured full Gbuffer target layout");}
 float zero[4]{},half[4]={.5f,.5f,.5f,.5f};c->ClearRenderTargetView(mr.Get(),zero);c->ClearRenderTargetView(pr.Get(),half);c->ClearDepthStencilView(ds.Get(),D3D11_CLEAR_DEPTH,.5f,0);
 ComPtr<ID3D11ShaderResourceView> colorSrv;require(SUCCEEDED(d->CreateShaderResourceView(color.Get(),nullptr,&colorSrv)),"low-resolution native HDR source");
 float globals[360]{};D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(globals);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA initial{globals,0,0};ComPtr<ID3D11Buffer> global;require(SUCCEEDED(d->CreateBuffer(&bd,&initial,&global)),"native 1440-byte global constants");
 std::ifstream nativeFile(std::filesystem::path(argv[2]),std::ios::binary);std::vector<char> nativeCode((std::istreambuf_iterator<char>(nativeFile)),{});require(!nativeCode.empty(),"captured native upscale bytecode");ComPtr<ID3D11PixelShader> nativePs;require(SUCCEEDED(d->CreatePixelShader(nativeCode.data(),nativeCode.size(),nullptr,&nativePs)),"actual native upscale shader");
 const GUID tag={0x85ce7341,0xf6c7,0x4e8a,{0x90,0x13,0x24,0x70,0x1a,0x55,0x40,0x91}};nativePs->SetPrivateData(tag,UINT(nativeCode.size()),nativeCode.data());
 const char* vertex="float4 main(uint id:SV_VertexID):SV_Position {return float4(id==2?3:-1,id==1?3:-1,0,1);}";ComPtr<ID3DBlob> code;require(SUCCEEDED(D3DCompile(vertex,strlen(vertex),"fixture",nullptr,nullptr,"main","vs_5_0",0,0,&code,nullptr)),"fixture output triangle");ComPtr<ID3D11VertexShader> vs;require(SUCCEEDED(d->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs)),"fixture vertex shader");
 D3D11_QUERY_DESC eventDescription{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> completed;require(SUCCEEDED(d->CreateQuery(&eventDescription,&completed)),"bounded GPU completion query");
 ComPtr<ID3D11Predicate> predicate;if(predicated){D3D11_QUERY_DESC desc{D3D11_QUERY_OCCLUSION_PREDICATE,0};require(SUCCEEDED(d->CreatePredicate(&desc,&predicate)),"native predicate fixture");c->Begin(predicate.Get());c->End(predicate.Get());}
 const unsigned frameCount=balancedStartup?120:bounded?30:renderCycle?384:stress?360:1;
 if(cycle){
  const int modes[]={3,4,5,0,2};const float scales[]={.58f,.5f,1333.f/4000.f,1.f,2667.f/4000.f};
  for(unsigned i=0;i<5;++i){DlssPass::Cycle();require(GetPrivateProfileIntW(L"Rendering",L"DLSSMode",-1,ini.c_str())==modes[i],"F3 persists every mode in cycle order");require(GetPrivateProfileIntW(L"Rendering",L"DLSS",-1,ini.c_str())==(modes[i]!=0),"Off and enabled modes persist consistently");Sleep(110);auto deadline=GetTickCount64()+2000;
   do{DlssPass::Prepare(renderer.data(),c.Get(),true,ow,oh);c->Flush();at<float>(renderer.data(),0x21fc4)=at<float>(renderer.data(),0x21fcc);if(!strcmp(DlssPass::Status(),modes[i]?"waiting":"off") && std::abs(at<float>(renderer.data(),0x21fcc)-scales[i])<.0001f)break;Sleep(2);}while(GetTickCount64()<deadline);
   require(std::abs(at<float>(renderer.data(),0x21fcc)-scales[i])<.0001f,"mode transition queues the SDK scale after a GPU fence");require(at<int>(configuration.data(),0x1c0)==(modes[i]?2:0),"Off restores native AA; selected modes prepare temporal inputs");
  }
 }
 if(transitions){
  for(unsigned i=1;i<=600;++i){DlssPass::Begin(i,true,false);DlssPass::End();}
  require(!strcmp(DlssPass::Status(),"waiting") && GetPrivateProfileIntW(L"Rendering",L"DLSS",0,ini.c_str())==1,"empty/menu intervals do not time out DLSS");
  c->ClearState();c->PSSetShader(nativePs.Get(),nullptr,0);
  for(unsigned i=601;i<=1051;++i){DlssPass::Begin(i,true,false);DlssPass::Eye(0);DlssPass::BeforeDraw(c.Get());DlssPass::End();}
  require(!strcmp(DlssPass::Status(),"failed") && GetPrivateProfileIntW(L"Rendering",L"DLSS",1,ini.c_str())==0,"actual missing inputs cause bounded recoverable fallback");
  DlssPass::Prepare(renderer.data(),c.Get(),true,ow,oh);require(at<float>(renderer.data(),0x21fcc)==1.f,"transient failure restores native scale");at<float>(renderer.data(),0x21fc4)=1.f;
  WritePrivateProfileStringW(L"Rendering",L"DLSS",L"1",ini.c_str());Sleep(210);auto retryDeadline=GetTickCount64()+2000;do{DlssPass::Prepare(renderer.data(),c.Get(),true,ow,oh);c->Flush();if(!strcmp(DlssPass::Status(),"waiting"))break;Sleep(2);}while(GetTickCount64()<retryDeadline);
  require(!strcmp(DlssPass::Status(),"waiting") && std::abs(at<float>(renderer.data(),0x21fcc)-float(iw)/ow)<.0001f,"F3 can retry transient input failure in same process");at<float>(renderer.data(),0x21fc4)=at<float>(renderer.data(),0x21fcc);
 }
 const unsigned frameBase=transitions?2000:100;
 auto noticeText=[&](){std::ifstream file(root/L"dlss-osd.txt");return std::string((std::istreambuf_iterator<char>(file)),{});};
 std::string settledNotice;std::filesystem::file_time_type settledWrite{};
 for(unsigned frameIndex=0;frameIndex<frameCount;++frameIndex){
  if(renderCycle && frameIndex && frameIndex%24==0){
   DlssPass::Cycle();Sleep(110);int selected=GetPrivateProfileIntW(L"Rendering",L"DLSSMode",-1,ini.c_str());
   const UINT widths[]={4000,4000,2667,2320,2000,1333},heights[]={4768,4768,3179,2765,2384,1588};
   float expected=float(widths[selected])/ow;auto deadline=GetTickCount64()+5000;
   do{DlssPass::Prepare(renderer.data(),c.Get(),true,ow,oh);c->Flush();at<float>(renderer.data(),0x21fc4)=at<float>(renderer.data(),0x21fcc);if(!strcmp(DlssPass::Status(),selected?"waiting":"off") && std::abs(at<float>(renderer.data(),0x21fcc)-expected)<.0001f && !DlssPass::TestTexture(0,0) && !DlssPass::TestTexture(1,1))break;Sleep(2);}while(GetTickCount64()<deadline);
   require(!strcmp(DlssPass::Status(),selected?"waiting":"off"),"real rendered mode transition completes GPU retirement before activation");
   require(std::abs(at<float>(renderer.data(),0x21fcc)-expected)<.0001f,"rendered mode uses actual native input scale");
   require(DlssPass::TestTexture(0,0)==nullptr && DlssPass::TestTexture(1,1)==nullptr,"completed mode transition retires both owned eye resources together");
   c->ClearState();iw=widths[selected];ih=heights[selected];
   mr.Reset();pr.Reset();cr.Reset();colorSrv.Reset();ds.Reset();motion.Reset();previous.Reset();depth.Reset();color.Reset();
   motion=texture(iw,ih,DXGI_FORMAT_R32_UINT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);previous=texture(iw,ih,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);depth=texture(iw,ih,DXGI_FORMAT_R32G8X24_TYPELESS,D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE);color=texture(iw,ih,DXGI_FORMAT_R11G11B10_FLOAT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
   require(SUCCEEDED(d->CreateRenderTargetView(motion.Get(),&rdesc,&mr)) && SUCCEEDED(d->CreateRenderTargetView(previous.Get(),nullptr,&pr)) && SUCCEEDED(d->CreateRenderTargetView(color.Get(),nullptr,&cr)) && SUCCEEDED(d->CreateDepthStencilView(depth.Get(),&dd,&ds)) && SUCCEEDED(d->CreateShaderResourceView(color.Get(),nullptr,&colorSrv)),"native frame-boundary rebuild recreates exact inputs");
   for(unsigned i=0;i<4;++i){materialView[i].Reset();material[i]=texture(iw,ih,DXGI_FORMAT_R32_UINT,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);require(SUCCEEDED(d->CreateRenderTargetView(material[i].Get(),nullptr,&materialView[i])),"resized Gbuffer");}
   c->ClearRenderTargetView(mr.Get(),zero);c->ClearRenderTargetView(pr.Get(),half);c->ClearDepthStencilView(ds.Get(),D3D11_CLEAR_DEPTH,.5f,0);
   printf("Rendered F3 transition: mode=%d input=%ux%u\n",selected,iw,ih);fflush(stdout);
  }
  if(renderCycle && !GetPrivateProfileIntW(L"Rendering",L"DLSS",0,ini.c_str()))continue;
  if(notices && frameIndex){settledNotice=noticeText();settledWrite=std::filesystem::last_write_time(root/L"dlss-osd.txt");DlssPass::Begin(100000+frameIndex,false,false);DlssPass::End();require(!strcmp(DlssPass::Status(),"waiting"),"lost tracking updates diagnostic status");require(noticeText()==settledNotice && std::filesystem::last_write_time(root/L"dlss-osd.txt")==settledWrite,"tracking wait does not reopen the mode panel");}
  DlssPass::Prepare(renderer.data(),c.Get(),true,ow,oh);
  const bool captureHooks=hooks && !pipeline && frameIndex==80;if(captureHooks)GpuCapture::Begin(frameBase+frameIndex,1);
  DlssPass::Begin(frameBase+frameIndex,true,outputAudit && frameIndex+1==frameCount);
  for(int eye=0;eye<2;++eye){
  c->ClearState();DlssPass::Eye(eye);if(captureHooks)GpuCapture::Eye(eye);
  ID3D11RenderTargetView* geometry[6]={materialView[0].Get(),materialView[1].Get(),materialView[2].Get(),materialView[3].Get(),mr.Get(),pr.Get()};c->OMSetRenderTargets(6,geometry,ds.Get());DlssPass::BeforeDraw(c.Get());
  ID3D11RenderTargetView* nativeOutput=outputRtv.Get();c->OMSetRenderTargets(1,&nativeOutput,nullptr);ID3D11ShaderResourceView* low=colorSrv.Get();c->PSSetShaderResources(0,1,&low);ID3D11Buffer* cb=global.Get();c->PSSetConstantBuffers(0,1,&cb);c->PSSetShader(nativePs.Get(),nullptr,0);c->VSSetShader(vs.Get(),nullptr,0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);D3D11_VIEWPORT viewport{0,0,float(ow),float(oh),0,1};c->RSSetViewports(1,&viewport);
  float eyeColor[4]={eye?0.f:1.f,eye?1.f:0.f,0,1};c->ClearRenderTargetView(cr.Get(),eyeColor);
  if(predicated && !eye)c->SetPredication(predicate.Get(),TRUE);
  DlssPass::BeforeDraw(c.Get());ComPtr<ID3D11PixelShader> replacement;c->PSGetShader(&replacement,nullptr,nullptr);
  if(predicated){ComPtr<ID3D11Predicate> restored;BOOL value{};c->GetPredication(&restored,&value);require(restored.Get()==(eye?nullptr:predicate.Get()) && (eye || value),"NGX preserves original predication");c->SetPredication(nullptr,FALSE);}
  if(audit || conversion || discard){require(replacement.Get()==nativePs.Get(),"diagnostic preserves native upscale shader");ComPtr<ID3D11ShaderResourceView> retained;c->PSGetShaderResources(0,1,&retained);require(retained.Get()==colorSrv.Get(),"diagnostic retains original native HDR source");DlssPass::AfterDraw(c.Get());if(!audit && frameIndex+1==frameCount){auto* owned=DlssPass::TestTexture(eye,0);require(owned!=nullptr,"continuous diagnostic copies each eye HDR input");D3D11_TEXTURE2D_DESC desc{};owned->GetDesc(&desc);require(desc.Width==iw && desc.Height==ih,"continuous diagnostic uses actual input dimensions");}continue;}
  require(replacement && replacement.Get()!=nativePs.Get(),"DLSS copy replaces the native spatial upscale");
  ComPtr<ID3D11ShaderResourceView> reconstructed;c->PSGetShaderResources(0,1,&reconstructed);ComPtr<ID3D11Resource> rr;reconstructed->GetResource(&rr);ComPtr<ID3D11Texture2D> rt;rr.As(&rt);D3D11_TEXTURE2D_DESC td{};rt->GetDesc(&td);require(td.Width==ow && td.Height==oh && td.Format==DXGI_FORMAT_R16G16B16A16_FLOAT,"full-resolution per-eye DLSS result is bound");
  require(rt.Get()==DlssPass::TestTexture(eye,2) && rt.Get()!=DlssPass::TestTexture(eye,1),"native pixel shader reads a separate texture from the SDK output");
  require(td.BindFlags==D3D11_BIND_SHADER_RESOURCE,"native-draw input has no unordered-access or render-target binding");
  c->Draw(3,0);DlssPass::AfterDraw(c.Get());ComPtr<ID3D11PixelShader> restored;c->PSGetShader(&restored,nullptr,nullptr);require(restored.Get()==nativePs.Get(),"native upscale PS restored");ComPtr<ID3D11ShaderResourceView> restoredColor;c->PSGetShaderResources(0,1,&restoredColor);require(restoredColor.Get()==colorSrv.Get(),"original native HDR input restored");
  if(frameIndex+1==frameCount || (renderCycle && frameIndex%24==23)){for(unsigned role=0;role<2;++role){auto* probe=DlssPass::TestTexture(eye,role);D3D11_TEXTURE2D_DESC desc{};probe->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;ComPtr<ID3D11Texture2D> staging;require(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&staging)),"inspect owned input/output resource");c->CopyResource(staging.Get(),probe);D3D11_MAPPED_SUBRESOURCE map{};require(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"inspect owned pixel");auto* pixel=static_cast<char*>(map.pData)+(desc.Height/2)*map.RowPitch+(desc.Width/2)*(role?8:4);DirectX::XMFLOAT3 value;if(role){auto* fp=reinterpret_cast<uint16_t*>(pixel);value={DirectX::PackedVector::XMConvertHalfToFloat(fp[0]),DirectX::PackedVector::XMConvertHalfToFloat(fp[1]),DirectX::PackedVector::XMConvertHalfToFloat(fp[2])};}else{DirectX::PackedVector::XMFLOAT3PK packed;memcpy(&packed,pixel,4);DirectX::XMStoreFloat3(&value,DirectX::PackedVector::XMLoadFloat3PK(&packed));}if(role)require(eye?value.y>.8f && value.x<.05f:value.x>.8f && value.y<.05f,"independent NGX output colors, across super-resolution modes");printf("Owned %s eye%d (%p) RGB=(%g,%g,%g)\n",role?"output":"input",eye,probe,value.x,value.y,value.z);fflush(stdout);c->Unmap(staging.Get(),0);}
  {output->GetDesc(&td);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> readback;require(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&readback)),"HDR output readback");c->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(c->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"read final native HDR output");DirectX::PackedVector::XMFLOAT3PK value;memcpy(&value,static_cast<char*>(mapped.pData)+(oh/2)*mapped.RowPitch+(ow/2)*4,4);DirectX::XMFLOAT3 rgb;DirectX::XMStoreFloat3(&rgb,DirectX::PackedVector::XMLoadFloat3PK(&value));c->Unmap(readback.Get(),0);printf("Final eye%d RGB=(%g,%g,%g)\n",eye,rgb.x,rgb.y,rgb.z);fflush(stdout);if(info){auto count=info->GetNumStoredMessages();printf("D3D debug messages=%llu\n",count);for(UINT64 message=0;message<std::min<UINT64>(count,80);++message){SIZE_T bytes{};info->GetMessage(message,nullptr,&bytes);std::vector<uint8_t> data(bytes);auto* m=reinterpret_cast<D3D11_MESSAGE*>(data.data());info->GetMessage(message,m,&bytes);printf("D3D %u/%u: %s\n",m->Severity,m->ID,m->pDescription);}fflush(stdout);}require(eye?rgb.y>.8f && rgb.x<.05f:rgb.x>.8f && rgb.y<.05f,"DLSS result reaches native pre-tone-map target, independent eyes");}}
  }
  DlssPass::End();if(captureHooks)GpuCapture::End(c.Get());
  if(notices && frameIndex)require(noticeText()==settledNotice && std::filesystem::last_write_time(root/L"dlss-osd.txt")==settledWrite,"tracking recovery retains the settled notice without reopening it");
  // The original fixture drained the GPU after every pair. Exercise queued
  // native-style work as well; all eye resources are reused while in flight.
  const bool drain=!pipeline || (frameIndex+1)%16==0 || frameIndex+1==frameCount;
  if(drain){c->End(completed.Get());c->Flush();BOOL ready{};auto deadline=GetTickCount64()+10000;HRESULT completion{};
  do{completion=c->GetData(completed.Get(),&ready,sizeof(ready),0);if(FAILED(completion)){fprintf(stderr,"Device removed=%08x at pair=%u\n",unsigned(d->GetDeviceRemovedReason()),frameIndex);break;}if(completion==S_FALSE || !ready)Sleep(1);}while((completion==S_FALSE || !ready) && GetTickCount64()<deadline);
  require(completion==S_OK && ready,"sustained actual per-eye evaluations complete on GPU");
  }
  if(audit){DlssPass::FlushInputAudit();for(unsigned eye=0;eye<2;++eye){auto path=root/L"dlss-input-audit"/L"frame-100"/(L"eye-"+std::to_wstring(eye));require(std::filesystem::file_size(path/L"color-r11g11b10.bin")==uint64_t(iw)*ih*4,"audit saves tightly packed actual-size HDR");require(std::filesystem::file_size(path/L"depth-r32f.bin")==uint64_t(iw)*ih*4 && std::filesystem::file_size(path/L"motion-rg16f.bin")==uint64_t(iw)*ih*4,"audit saves decoded depth and motion");}require(!DlssPass::Effective() && !strcmp(DlssPass::Status(),"input-audit-saved"),"saved input audit never advertises DLSS activation");}
  if(captureHooks){GpuCapture::Flush();require(std::filesystem::exists(root/L"gpu-captures"/(L"frame-"+std::to_wstring(frameBase+80))/L"trace.json"),"full production capture completes with real NGX hooks enabled");}
  if(outputAudit && frameIndex+1==frameCount){DlssPass::FlushInputAudit();for(unsigned eye=0;eye<2;++eye){auto path=root/L"dlss-image-audit"/(L"frame-"+std::to_wstring(frameBase+frameIndex))/(L"eye-"+std::to_wstring(eye));require(std::filesystem::file_size(path/L"grid-rgba32f.bin")==64*64*3*16,"small HDR audit byte size");require(std::filesystem::exists(path/L"metadata.json"),"HDR audit metadata saved after GPU completion");std::ifstream file(path/L"grid-rgba32f.bin",std::ios::binary);std::vector<float> grid(64*64*3*4);file.read(reinterpret_cast<char*>(grid.data()),grid.size()*4);for(unsigned image=0;image<3;++image){auto* value=grid.data()+(image*64*64+32*64+32)*4;require(eye?value[1]>.8f && value[0]<.05f:value[0]>.8f && value[1]<.05f,"HDR input, NGX output and native draw audit preserve each eye color");}}}
  if(stress && frameIndex%60==59){printf("Completed %u full-resolution native pairs\n",frameIndex+1);fflush(stdout);}
 }
 require(audit?!DlssPass::Effective():bounded?!DlssPass::Effective() && !strcmp(DlssPass::Status(),"stopping"):conversion || discard?!DlssPass::Effective() && !strcmp(DlssPass::Status(),"diagnostic"):DlssPass::Effective(),"active state and bounded automatic stop follow actual per-eye evaluations");
 if(bounded)require(GetPrivateProfileIntW(L"Rendering",L"DLSS",1,ini.c_str())==0,"bounded test clears DLSS request without user input");
 WritePrivateProfileStringW(L"Rendering",L"DLSS",L"0",ini.c_str());Sleep(210);DlssPass::Prepare(renderer.data(),c.Get(),true,ow,oh);
 require(at<float>(renderer.data(),0x21fcc)==1.f,"off queues native resolution restoration");require(at<int>(configuration.data(),0x1c0)==0 && at<uint64_t>(image,0x152ee80)==0,"off restores original AA configuration");require(!DlssPass::Effective() && !strcmp(DlssPass::Status(),"off"),"off reports effective state correctly");
 wchar_t journalName[128]{};swprintf_s(journalName,L"Local\\PlagueTaleVR-DlssJournal-%lu",GetCurrentProcessId());HANDLE shared=OpenFileMappingW(FILE_MAP_READ,FALSE,journalName);require(shared!=nullptr,"external diagnostic journal is available read-only");auto* journal=static_cast<const DlssJournal*>(MapViewOfFile(shared,FILE_MAP_READ,0,0,sizeof(DlssJournal)));require(journal && journal->abi==1 && journal->size==sizeof(DlssJournal),"versioned journal has validated size");require(journal->deviceStatus==0 && (audit?journal->evaluations[0]==0 && journal->evaluations[1]==0:journal->completedFrame==frameBase+frameCount-1) && journal->pendingQueries==0,"journal proves real GPU completion or zero-evaluation input audit");if(conversion)require(journal->evaluations[0]==0 && journal->evaluations[1]==0,"continuous conversion never evaluates NGX");if(discard)require(journal->evaluations[0]==frameCount && journal->evaluations[1]==frameCount,"evaluate-only runs both NGX features without displaying them");UnmapViewOfFile(journal);CloseHandle(shared);
 require(SUCCEEDED(d->GetDeviceRemovedReason()),"device healthy");DlssPass::Shutdown();if(hooks){MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();}VirtualFree(image,0,MEM_RELEASE);printf("Dimensions: %ux%u -> %ux%u per eye; native depth format19/view21.\n",iw,ih,ow,oh);if(audit)printf("PASS: %u input audit checks: complete native hooks, captured inputs, zero NGX evaluations, unchanged native upscale and restoration.\n",checks);else if(conversion || discard)printf("PASS: %u continuous diagnostic checks: %s, original native shader/source retained, correct status, GPU completion and restoration.\n",checks,conversion?"zero NGX features/evaluations":"both NGX features evaluated without output substitution");else printf("PASS: %u production DLSS pass checks: pending scale, exact native upscale insertion, actual NGX per eye, native HDR output and restoration.\n",checks);fflush(stdout);
}
