#include "DlssRuntime.h"
#include <DirectXPackedVector.h>
#include <fstream>
#include <vector>
#include <regex>
#include <cstdio>
#include <cmath>
#include <iomanip>
using Microsoft::WRL::ComPtr;
static void require(bool value,const char* reason){if(!value){fprintf(stderr,"FAIL: %s\n",reason);exit(1);}}
static std::vector<uint8_t> read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);require(bool(f),"captured input file");return {(std::istreambuf_iterator<char>(f)),{}};}
static UINT number(const std::string& s,const char* key){std::smatch m;require(std::regex_search(s,m,std::regex(std::string("\"")+key+"\"\\s*:\\s*(\\d+)")),"metadata dimensions");return UINT(std::stoul(m[1].str()));}
struct Pair {ComPtr<ID3D11Texture2D> inputs[2][3];UINT width{},height{},outWidth{},outHeight{};float jitter[2][2]{};};
int wmain(int argc,wchar_t** argv){
 require(argc==6 || (argc==7 && !wcscmp(argv[6],L"--fresh-runtime")),"runtime directory plus Quality/Balanced/Performance/Ultra input-pair directories [--fresh-runtime]");
 bool fresh=argc==7;auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"hardware D3D11 device");
 Pair pairs[4];
 for(unsigned mode=0;mode<4;++mode)for(unsigned eye=0;eye<2;++eye){
  auto path=std::filesystem::path(argv[mode+2])/(L"eye-"+std::to_wstring(eye));auto raw=read(path/L"metadata.json");std::string metadata(raw.begin(),raw.end());auto& p=pairs[mode];p.width=number(metadata,"inputWidth");p.height=number(metadata,"inputHeight");p.outWidth=number(metadata,"outputWidth");p.outHeight=number(metadata,"outputHeight");
  std::smatch jitter;if(std::regex_search(metadata,jitter,std::regex("\"jitterPixels\"\\s*:\\s*\\[([^,]+),([^\\]]+)\\]"))){p.jitter[eye][0]=std::stof(jitter[1].str());p.jitter[eye][1]=std::stof(jitter[2].str());}
  const wchar_t* names[]={L"color-r11g11b10.bin",L"depth-r32f.bin",L"motion-rg16f.bin"};DXGI_FORMAT formats[]={DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
  for(unsigned role=0;role<3;++role){auto data=read(path/names[role]);require(data.size()==size_t(p.width)*p.height*4,"exact native input bytes");D3D11_TEXTURE2D_DESC d{};d.Width=p.width;d.Height=p.height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=formats[role];d.BindFlags=D3D11_BIND_SHADER_RESOURCE|(role?D3D11_BIND_UNORDERED_ACCESS:D3D11_BIND_RENDER_TARGET);D3D11_SUBRESOURCE_DATA upload{data.data(),p.width*4,0};require(SUCCEEDED(device->CreateTexture2D(&d,&upload,&p.inputs[eye][role])),"native captured input uploaded");}
 }
 ComPtr<ID3D11Texture2D> outputs[2];for(auto& output:outputs){D3D11_TEXTURE2D_DESC d{};d.Width=pairs[0].outWidth;d.Height=pairs[0].outHeight;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&output)),"persistent full-resolution eye output");}
 ComPtr<ID3D11Texture2D> staging;D3D11_TEXTURE2D_DESC tile{};tile.Width=tile.Height=16;tile.MipLevels=tile.ArraySize=tile.SampleDesc.Count=1;tile.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;tile.Usage=D3D11_USAGE_STAGING;tile.CPUAccessFlags=D3D11_CPU_ACCESS_READ;require(SUCCEEDED(device->CreateTexture2D(&tile,nullptr,&staging)),"small output sample");
 D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> complete;require(SUCCEEDED(device->CreateQuery(&q,&complete)),"GPU completion query");
 auto drain=[&](){context->End(complete.Get());context->Flush();auto deadline=GetTickCount64()+10000;BOOL done{};HRESULT hr{};do{hr=context->GetData(complete.Get(),&done,sizeof(done),0);if(FAILED(hr))break;if(hr!=S_OK || !done)Sleep(1);}while((hr!=S_OK || !done) && GetTickCount64()<deadline);if(hr!=S_OK || !done){fprintf(stderr,"GPU failure hr=%08x reason=%08x\n",unsigned(hr),unsigned(device->GetDeviceRemovedReason()));exit(2);}};
 DlssRuntime runtime;require(runtime.Initialize(device.Get(),root),"NGX initialized");unsigned evaluated{};
 for(unsigned cycle=0;cycle<8;++cycle)for(unsigned index=0;index<4;++index){
  require(runtime.RetireEyes(),"completed previous pair retired");
  if(fresh){runtime.Shutdown();require(runtime.Initialize(device.Get(),root),"fresh SDK runtime before new mode");}
  auto mode=DlssModes::Mode(index+2);runtime.SelectMode(mode);auto& p=pairs[index];UINT recommendedWidth{},recommendedHeight{};require(runtime.Quality(p.outWidth,p.outHeight,recommendedWidth,recommendedHeight),"query selected SDK mode");
  printf("Cycle %u %s actual=%ux%u recommended=%ux%u freshRuntime=%d\n",cycle,DlssModes::Name(mode),p.width,p.height,recommendedWidth,recommendedHeight,fresh);fflush(stdout);
  for(unsigned frame=0;frame<32;++frame){for(unsigned eye=0;eye<2;++eye)require(runtime.Evaluate(context.Get(),eye,p.inputs[eye][0].Get(),p.inputs[eye][1].Get(),p.inputs[eye][2].Get(),outputs[eye].Get(),p.width,p.height,p.outWidth,p.outHeight,p.jitter[eye][0],p.jitter[eye][1],frame==0),"actual captured pair evaluates");++evaluated;if(frame%8==7)drain();}
  for(unsigned eye=0;eye<2;++eye){D3D11_BOX box{p.outWidth/2,p.outHeight/2,0,p.outWidth/2+16,p.outHeight/2+16,1};context->CopySubresourceRegion(staging.Get(),0,0,0,0,outputs[eye].Get(),0,&box);D3D11_MAPPED_SUBRESOURCE map{};require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"read completed eye output sample");double sum{};unsigned nonfinite{};for(unsigned y=0;y<16;++y){auto* row=reinterpret_cast<const uint16_t*>(static_cast<const char*>(map.pData)+y*map.RowPitch);for(unsigned x=0;x<16;++x)for(unsigned ch=0;ch<3;++ch){float v=DirectX::PackedVector::XMConvertHalfToFloat(row[x*4+ch]);if(!std::isfinite(v))++nonfinite;else sum+=v;}}context->Unmap(staging.Get(),0);printf(" eye=%u mean=%g nonfinite=%u\n",eye,sum/(16*16*3),nonfinite);fflush(stdout);require(!nonfinite && sum>1e-5,"both native-input SDK outputs are finite and visible");}
 }
 drain();runtime.Shutdown();require(SUCCEEDED(device->GetDeviceRemovedReason()),"device healthy");printf("PASS: %u actual-input stereo pairs across 32 mode activations.\n",evaluated);
}
