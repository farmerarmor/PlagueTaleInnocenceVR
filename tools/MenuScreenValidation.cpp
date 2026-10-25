#include "MenuScreen.h"
#include "MenuScreenPolicy.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
static unsigned checks{};
static void require(bool value,const char* label){++checks;if(!value){fprintf(stderr,"FAIL: %s\n",label);exit(1);}}
template<class T>static void put(void* p,size_t off,T value){memcpy(static_cast<char*>(p)+off,&value,sizeof(value));}
int main(){
 std::vector<char> image(0x1800000);const auto base=reinterpret_cast<uintptr_t>(image.data());
 auto type=[&](unsigned rva,const char* name){strcpy_s(image.data()+rva+16,120,name);return rva;};
 const auto menuBase=type(0x1000,".?AVCDialog_PLG_MenuBase_G@@"),inventory=type(0x1100,".?AVCDialog_PLG_CraftUpgradeCodex_G@@"),settings=type(0x1200,".?AVCDialog_PLG_Settings_G@@"),graphics=type(0x1300,".?AVCDialog_PLG_AdvancedGraphicsMenu_G@@"),hud=type(0x1400,".?AVCDialog_PLG_HUD_G@@"),pause=type(0x1500,".?AVCDialog_PLG_Pause_G@@");
 auto vtable=[&](unsigned at,unsigned own,bool menu){
  put<uint32_t>(image.data(),at,1);put<uint32_t>(image.data(),at+12,own);put<uint32_t>(image.data(),at+16,at+0x40);put<uint32_t>(image.data(),at+20,at);
  put<uint32_t>(image.data(),at+0x48,menu?2:1);put<uint32_t>(image.data(),at+0x4c,at+0x60);put<uint32_t>(image.data(),at+0x60,at+0x80);put<uint32_t>(image.data(),at+0x80,own);
  if(menu){put<uint32_t>(image.data(),at+0x64,at+0x90);put<uint32_t>(image.data(),at+0x90,menuBase);}
  put<uintptr_t>(image.data(),at+0xb8,base+at);return base+at+0xc0;
 };
 std::array<std::array<char,0x300>,5> objects{};std::array<std::array<char,32>,5> handles{};std::array<uintptr_t,5> entries{};
 const unsigned types[]={inventory,settings,graphics,hud,pause};
 for(unsigned i=0;i<5;++i){put<uintptr_t>(objects[i].data(),0,vtable(0x2000+i*0x100,types[i],i==1 || i==2 || i==4));put<uintptr_t>(handles[i].data(),16,reinterpret_cast<uintptr_t>(objects[i].data()));entries[i]=reinterpret_cast<uintptr_t>(handles[i].data());}
 std::array<char,0x900> gui{};std::array<char,0x80> focus{};put<uintptr_t>(gui.data(),0,base+0xf64ac0);put<uintptr_t>(gui.data(),0x888,reinterpret_cast<uintptr_t>(focus.data()));put<uintptr_t>(focus.data(),0x48,reinterpret_cast<uintptr_t>(entries.data()));
 auto query=[&](bool valid,bool expected){bool menu=false;require(MenuScreen::TestQuery(base,image.size(),gui.data(),menu)==valid,"native focus query validity");if(valid)require(menu==expected,"native menu classification");};
 put<int>(focus.data(),0x44,0);query(true,false);
 for(unsigned i=0;i<5;++i){auto one=reinterpret_cast<uintptr_t>(handles[i].data());put<uintptr_t>(focus.data(),0x48,reinterpret_cast<uintptr_t>(&one));put<int>(focus.data(),0x44,1);query(true,i!=3);}
 put<uintptr_t>(focus.data(),0x48,reinterpret_cast<uintptr_t>(entries.data()));put<int>(focus.data(),0x44,5);query(true,true);
 put<int>(focus.data(),0x44,65);query(false,false);put<int>(focus.data(),0x44,-1);query(false,false);put<int>(focus.data(),0x44,1);put<uintptr_t>(focus.data(),0x48,0);query(false,false);
 put<uintptr_t>(focus.data(),0x48,reinterpret_cast<uintptr_t>(entries.data()));const auto saved=entries[0];entries[0]=1;query(false,false);entries[0]=saved;
 put<uintptr_t>(gui.data(),0,base+0xf64ac8);query(false,false);put<uintptr_t>(gui.data(),0,base+0xf64ac0);
 auto& object=objects[1];const auto vt=*reinterpret_cast<uintptr_t*>(object.data());put<uintptr_t>(object.data(),0,base+image.size()+32);auto one=reinterpret_cast<uintptr_t>(handles[1].data());put<uintptr_t>(focus.data(),0x48,reinterpret_cast<uintptr_t>(&one));query(true,false);put<uintptr_t>(object.data(),0,vt);
 MenuScreenPolicy::Gate gate;require(!gate.Update(true,false,900),"gameplay starts outside screen");require(gate.Update(true,true,1000),"inventory opens immediately");require(gate.Update(true,false,1119),"brief stack gap retains screen");require(!gate.Update(true,false,1120),"closing menu returns gameplay");require(gate.Update(true,true,1200),"options opens");require(gate.Update(true,true,1400),"submenu retains screen");require(gate.Update(false,false,1899),"transient invalid stack retains screen");require(!gate.Update(false,false,1900),"invalid stack cannot stick forever");
 for(float yaw:{0.f,.6f,2.f}){
  Transport::Pose head{};head.position={3,1.7f,-2};head.orientation=CameraMath::Multiply(Transport::Quaternion{0,sinf(yaw*.5f),0,cosf(yaw*.5f)},Transport::Quaternion{sinf(.2f),0,0,cosf(.2f)});auto anchor=MenuScreenPolicy::Anchor(head);
  const auto dx=anchor.position.x-head.position.x,dy=anchor.position.y-head.position.y,dz=anchor.position.z-head.position.z;require(std::abs(dx*dx+dy*dy+dz*dz-4)<1e-5f,"screen anchored two metres away");require(std::abs(dy)<1e-5f,"screen level at eye height");const auto up=CameraMath::Rotate(anchor.orientation,{0,1,0});require(std::abs(up.x)+std::abs(up.z)<1e-5f,"pitch does not tilt menu");
 }
 require(MenuScreenPolicy::LayerCount(3)==1 && MenuScreenPolicy::LayerCount(0)==2 && MenuScreenPolicy::LayerCount(1)==0 && MenuScreenPolicy::LayerCount(2)==0,"one binocular menu quad, native stereo fallback and world layer separation");require(std::abs(MenuScreenPolicy::Aspect-16.f/9.f)<1e-6f,"16:9 menu logical aspect");
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)),"hardware D3D11");
 constexpr unsigned w=32,h=18;std::vector<uint32_t> pixels(w*h*2);for(unsigned i=0;i<pixels.size();++i)pixels[i]=i<w*h?0xff0000ffu:0xff00ff00u;
 D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=2*h;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,0};ComPtr<ID3D11Texture2D> pair;require(SUCCEEDED(d->CreateTexture2D(&td,&data,&pair)),"distinct native eye pair");
 td.Height=h;td.ArraySize=2;ComPtr<ID3D11Texture2D> output;require(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&output)),"XR eye array fixture");td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;require(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&staging)),"eye readback fixture");
 for(unsigned mode:{0u,1u,3u})for(bool swap:{false,true}){
  for(unsigned eye=0;eye<2;++eye){unsigned source=MenuScreenPolicy::SourceEye(mode,eye,swap);D3D11_BOX box{0,source*h,0,w,(source+1)*h,1};c->CopySubresourceRegion(output.Get(),eye,0,0,0,pair.Get(),0,&box);}
  c->CopyResource(staging.Get(),output.Get());
  for(unsigned eye=0;eye<2;++eye){D3D11_MAPPED_SUBRESOURCE mapped{};require(SUCCEEDED(c->Map(staging.Get(),eye,D3D11_MAP_READ,0,&mapped)),"map submitted eye");unsigned expected=mode==3?0:swap?1-eye:eye;
   for(unsigned y=0;y<h;++y){auto* row=reinterpret_cast<const uint32_t*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch);for(unsigned x=0;x<w;++x)require(row[x]==(expected?0xff00ff00u:0xff0000ffu),mode==3?"menu eyes show same complete image":"world eyes retain distinct native images");}c->Unmap(staging.Get(),eye);
  }
 }
 require(SUCCEEDED(d->GetDeviceRemovedReason()),"device healthy");printf("PASS: %u menu checks: native inventory/Options/submenu detection, HUD exclusion, invalid-state bounds, transitions, level screen anchor, one binocular quad and actual GPU mono-menu/stereo-gameplay copies with eye-swap.\n",checks);
}
