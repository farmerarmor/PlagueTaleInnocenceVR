#include "DlssRuntime.h"
#include "StatusOverlay.h"
#include <DirectXPackedVector.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cmath>
using Microsoft::WRL::ComPtr;
static unsigned checks;
static void require(bool good,const char* reason){++checks;if(!good){fprintf(stderr,"FAIL: %s\n",reason);exit(1);}}
int wmain(int argc,wchar_t** argv){
 require(argc==2 || (argc==3 && !wcscmp(argv[2],L"--stress")),"runtime directory [--stress]");bool stress=argc==3;auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
 require(DlssModes::Quality==2 && DlssModes::Balanced==3 && DlssModes::Performance==4 && DlssModes::UltraPerformance==5,"existing quality mode IDs stay compatible");
 require(DlssModes::Configured(true,1)==DlssModes::Quality,"removed mode settings fall back to Quality");
 require(DlssModes::Configured(false,1)==DlssModes::Off,"disabled legacy mode stays Off");
 require(DlssModes::Configured(true,4)==DlssModes::Performance,"saved Performance remains Performance");
 auto mode=DlssModes::Off;for(unsigned i=0;i<600;++i){mode=DlssModes::Next(mode);require(mode==DlssModes::Modes[(i+1)%DlssModes::Count] && int(mode)!=1,"F3 wraps through five modes and never selects removed mode");}
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> c;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&c)),"hardware device");DlssRuntime runtime;require(runtime.Initialize(device.Get(),root),"NGX initialized");
 std::ofstream preview(root/L"osd-preview.ppm",std::ios::binary);preview<<"P6\n1024 880\n255\n";
 for(int selection=0;selection<(stress?DlssModes::Count*12:DlssModes::Count);++selection){
  mode=DlssModes::Modes[selection%DlssModes::Count];UINT fullWidth=4000,fullHeight=4768,ow=stress?4000:768,oh=stress?4768:912,iw=ow,ih=oh;
  if(mode!=DlssModes::Off){runtime.SelectMode(mode);require(runtime.Quality(4000,4768,fullWidth,fullHeight),"query actual SDK dimensions for every mode");require(fullWidth<4000 && fullHeight<4768,"every enabled mode uses super-resolution input");require(runtime.Quality(ow,oh,iw,ih),"fixture dimensions");}
  printf("%s: %u x %u -> 4000 x 4768 per eye\n",DlssModes::Name(mode),fullWidth,fullHeight);fflush(stdout);
  std::string label=std::string(mode==DlssModes::Off?"DLSS Off":DlssModes::Name(mode))+"\n"+std::to_string(fullWidth)+" x "+std::to_string(fullHeight)+" -> 4000 x 4768 per eye";
  auto image=StatusOverlay::Rasterize(std::wstring(label.begin(),label.end()));require(image.size()==StatusOverlay::Width*StatusOverlay::Height*4,"OSD bitmap dimensions");unsigned glyphs=0,panel=0,clear=0;for(size_t i=0;i<image.size();i+=4){glyphs+=image[i]>128 && image[i+3]==255;panel+=image[i+3]>0 && image[i+3]<255;clear+=image[i+3]==0;preview.write(reinterpret_cast<char*>(image.data()+i),3);}require(glyphs>300 && panel>StatusOverlay::Width*StatusOverlay::Height/2 && !clear,"readable glyphs on a translucent panel");
  require(runtime.RetireEyes(),"retire both GPU-completed eyes before selecting another mode");
  if(mode==DlssModes::Off)continue;
  auto texture=[&](UINT w,UINT h,DXGI_FORMAT format,UINT flags){D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=format;d.BindFlags=flags;ComPtr<ID3D11Texture2D> t;require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&t)),"mode texture");return t;};
  auto depth=texture(iw,ih,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET),motion=texture(iw,ih,DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);ComPtr<ID3D11RenderTargetView> z,mv;require(SUCCEEDED(device->CreateRenderTargetView(depth.Get(),nullptr,&z)) && SUCCEEDED(device->CreateRenderTargetView(motion.Get(),nullptr,&mv)),"depth/motion views");float zero[4]{},half[4]={.5f,.5f,.5f,.5f};c->ClearRenderTargetView(z.Get(),half);c->ClearRenderTargetView(mv.Get(),zero);
  ComPtr<ID3D11Texture2D> color[2],output[2];ComPtr<ID3D11RenderTargetView> rtv[2];for(int eye=0;eye<2;++eye){color[eye]=texture(iw,ih,DXGI_FORMAT_R11G11B10_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);output[eye]=texture(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);require(SUCCEEDED(device->CreateRenderTargetView(color[eye].Get(),nullptr,&rtv[eye])),"eye HDR view");float value[4]={eye?0.f:1.f,eye?1.f:0.f,0,1};c->ClearRenderTargetView(rtv[eye].Get(),value);}
  for(unsigned frame=0;frame<(stress?24u:5u);++frame)for(int eye=0;eye<2;++eye)require(runtime.Evaluate(c.Get(),eye,color[eye].Get(),depth.Get(),motion.Get(),output[eye].Get(),iw,ih,ow,oh,0,0,frame==0),"mode evaluates independently for both eyes");
  for(int eye=0;eye<2;++eye){D3D11_TEXTURE2D_DESC d{};output[eye]->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.BindFlags=0;ComPtr<ID3D11Texture2D> staging;require(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)),"mode readback");c->CopyResource(staging.Get(),output[eye].Get());D3D11_MAPPED_SUBRESOURCE map{};require(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"GPU complete before next mode can release features");auto* p=reinterpret_cast<uint16_t*>(static_cast<char*>(map.pData)+(d.Height/2)*map.RowPitch)+(d.Width/2)*4;float red=DirectX::PackedVector::XMConvertHalfToFloat(p[0]),green=DirectX::PackedVector::XMConvertHalfToFloat(p[1]);require(std::isfinite(red)&&std::isfinite(green)&&(eye?green>.8f && red<.05f:red>.8f && green<.05f),"every mode preserves separate red/green eye images");c->Unmap(staging.Get(),0);}
 }
 require(SUCCEEDED(device->GetDeviceRemovedReason()),"device healthy after every mode");runtime.Shutdown();printf("PASS: %u mode/OSD checks, all four super-resolution modes evaluated in both eyes; legacy mode migration verified. Headset behavior pending.\n",checks);
}
