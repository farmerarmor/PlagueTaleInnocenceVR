#include "DlssRuntime.h"
#include <DirectXPackedVector.h>
#include <fstream>
#include <vector>
#include <string>
#include <regex>
#include <cstdio>
#include <cmath>
#include <algorithm>
using Microsoft::WRL::ComPtr;
static void require(bool value,const char* reason){if(!value){fprintf(stderr,"FAIL: %s\n",reason);exit(1);}}
static std::vector<uint8_t> read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);require(bool(f),"input file exists");return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{});}
static unsigned number(const std::string& meta,const char* key){std::smatch match;require(std::regex_search(meta,match,std::regex(std::string("\"")+key+"\"\\s*:\\s*(\\d+)")),"required dimensions metadata");return unsigned(std::stoul(match[1].str()));}
int wmain(int argc,wchar_t** argv){
 require(argc>=3 && argc<=4,"usage: DlssInputReplay runtime-directory audit-frame-directory [--inspect]");bool inspect=argc==4 && !wcscmp(argv[3],L"--inspect");auto runtimePath=std::filesystem::absolute(argv[1]),auditPath=std::filesystem::absolute(argv[2]);
 struct Inputs {std::vector<uint8_t> data[3];unsigned width{},height{},outWidth{},outHeight{};float jitter[2]{};};Inputs inputs[2];
 for(unsigned eye=0;eye<2;++eye){auto path=auditPath/(L"eye-"+std::to_wstring(eye));auto metadata=read(path/L"metadata.json");std::string meta(metadata.begin(),metadata.end());auto& in=inputs[eye];in.width=number(meta,"inputWidth");in.height=number(meta,"inputHeight");in.outWidth=number(meta,"outputWidth");in.outHeight=number(meta,"outputHeight");require(in.width>0 && in.width<=16384 && in.height>0 && in.height<=8192 && in.outWidth<=16384 && in.outHeight<=8192,"bounded native dimensions");std::smatch jitter;if(std::regex_search(meta,jitter,std::regex("\"jitterPixels\"\\s*:\\s*\\[([^,]+),([^\\]]+)\\]"))){in.jitter[0]=std::stof(jitter[1].str());in.jitter[1]=std::stof(jitter[2].str());}
  const wchar_t* names[]={L"color-r11g11b10.bin",L"depth-r32f.bin",L"motion-rg16f.bin"};for(unsigned i=0;i<3;++i){in.data[i]=read(path/names[i]);require(in.data[i].size()==uint64_t(in.width)*in.height*4,"exact packed input byte count");}
  uint64_t badColor{},badDepth{},badMotion{},outsideMotion{};float minDepth=1,maxDepth=0,maxMotion=0;auto* color=reinterpret_cast<const uint32_t*>(in.data[0].data());auto* depth=reinterpret_cast<const float*>(in.data[1].data());auto* motion=reinterpret_cast<const uint16_t*>(in.data[2].data());
  for(uint64_t pixel=0;pixel<uint64_t(in.width)*in.height;++pixel){auto value=color[pixel];badColor+=((value>>6)&31)==31 || ((value>>17)&31)==31 || ((value>>27)&31)==31;float z=depth[pixel];if(!std::isfinite(z) || z<0 || z>1)++badDepth;else{minDepth=std::min(minDepth,z);maxDepth=std::max(maxDepth,z);}float x=DirectX::PackedVector::XMConvertHalfToFloat(motion[pixel*2]),y=DirectX::PackedVector::XMConvertHalfToFloat(motion[pixel*2+1]);if(!std::isfinite(x)||!std::isfinite(y))++badMotion;else{maxMotion=std::max({maxMotion,std::abs(x),std::abs(y)});if(std::abs(x)>in.width || std::abs(y)>in.height)++outsideMotion;}}
  printf("eye%u %ux%u -> %ux%u: color nonfinite=%llu depth invalid=%llu range=[%g,%g] motion nonfinite=%llu outside=%llu maxPixels=%g jitter=(%g,%g)\n",eye,in.width,in.height,in.outWidth,in.outHeight,badColor,badDepth,minDepth,maxDepth,badMotion,outsideMotion,maxMotion,in.jitter[0],in.jitter[1]);fflush(stdout);
 }
 if(inspect)return 0;
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"hardware device");DlssRuntime runtime;require(runtime.Initialize(device.Get(),runtimePath),"NGX initialized");
 ComPtr<ID3D11Texture2D> textures[2][3],outputs[2];DXGI_FORMAT formats[]={DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
 for(unsigned eye=0;eye<2;++eye){auto& in=inputs[eye];for(unsigned i=0;i<3;++i){D3D11_TEXTURE2D_DESC td{};td.Width=in.width;td.Height=in.height;td.Format=formats[i];td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|(i?D3D11_BIND_UNORDERED_ACCESS:D3D11_BIND_RENDER_TARGET);D3D11_SUBRESOURCE_DATA data{in.data[i].data(),in.width*4,0};require(SUCCEEDED(device->CreateTexture2D(&td,&data,&textures[eye][i])),"actual captured input uploaded");}D3D11_TEXTURE2D_DESC td{};td.Width=in.outWidth;td.Height=in.outHeight;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;require(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&outputs[eye])),"full-size eye output");}
 D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> completed;require(SUCCEEDED(device->CreateQuery(&desc,&completed)),"GPU event");
 for(unsigned frame=0;frame<360;++frame){for(unsigned eye=0;eye<2;++eye){auto& in=inputs[eye];require(runtime.Evaluate(context.Get(),eye,textures[eye][0].Get(),textures[eye][1].Get(),textures[eye][2].Get(),outputs[eye].Get(),in.width,in.height,in.outWidth,in.outHeight,in.jitter[0],in.jitter[1],frame==0),"actual captured-input DLSS evaluation");}context->End(completed.Get());context->Flush();auto deadline=GetTickCount64()+10000;BOOL ready{};HRESULT hr{};do{hr=context->GetData(completed.Get(),&ready,sizeof(ready),0);if(FAILED(hr))break;if(hr==S_FALSE || !ready)Sleep(1);}while((hr==S_FALSE || !ready) && GetTickCount64()<deadline);if(hr!=S_OK || !ready){fprintf(stderr,"GPU completion failed frame=%u hr=%08x removed=%08x\n",frame,unsigned(hr),unsigned(device->GetDeviceRemovedReason()));return 2;}if(frame%60==59){printf("Replayed %u actual-input stereo pairs\n",frame+1);fflush(stdout);}}
 runtime.Shutdown();puts("PASS: 360 actual captured-input DLSS stereo pairs completed on GPU.");
}
