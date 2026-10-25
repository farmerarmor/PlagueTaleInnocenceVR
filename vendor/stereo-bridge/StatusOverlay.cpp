#include "StatusOverlay.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
std::vector<unsigned char> StatusOverlay::Rasterize(const std::wstring& text){
 std::vector<unsigned char> pixels;HDC dc=CreateCompatibleDC(nullptr);if(!dc)return pixels;
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=Width;info.bmiHeader.biHeight=-int(Height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
 void* data{};HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);if(!bitmap){DeleteDC(dc);return pixels;}
 auto oldBitmap=SelectObject(dc,bitmap);HBRUSH background=CreateSolidBrush(RGB(14,14,14));RECT area{0,0,Width,Height};FillRect(dc,&area,background);DeleteObject(background);
 HFONT font=CreateFontW(-39,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));RECT label{18,30,Width-18,Height-14};DrawTextW(dc,text.c_str(),int(text.size()),&label,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
 GdiFlush();pixels.assign(static_cast<unsigned char*>(data),static_cast<unsigned char*>(data)+Width*Height*4);for(size_t i=0;i<pixels.size();i+=4)pixels[i+3]=pixels[i]>20?255:208;
 SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);return pixels;
}
bool StatusOverlay::Create(XrSession session){
 if(swapchain)return true;uint32_t count{};if(XR_FAILED(xrEnumerateSwapchainFormats(session,0,&count,nullptr)))return false;std::vector<int64_t> formats(count);if(XR_FAILED(xrEnumerateSwapchainFormats(session,count,&count,formats.data())))return false;
 int64_t format=0;for(auto candidate:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM})if(std::find(formats.begin(),formats.end(),int64_t(candidate))!=formats.end()){format=candidate;break;}if(!format)return false;
 XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};create.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;create.format=format;create.sampleCount=create.arraySize=create.mipCount=create.faceCount=1;create.width=Width;create.height=Height;
 if(XR_FAILED(xrCreateSwapchain(session,&create,&swapchain)))return false;if(XR_FAILED(xrEnumerateSwapchainImages(swapchain,0,&count,nullptr))){Reset();return false;}images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
 if(XR_FAILED(xrEnumerateSwapchainImages(swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))){Reset();return false;}return true;
}
const XrCompositionLayerBaseHeader* StatusOverlay::Layer(XrSession session,XrSpace head,ID3D11DeviceContext* context,const std::wstring& text,uint64_t now){
 if(text.empty())return nullptr;if(text!=last){last=text;until=now+4000;dirty=true;}if(now>=until)return nullptr;if(!Create(session))return nullptr;
 if(dirty){auto pixels=Rasterize(text);if(pixels.empty())return nullptr;uint32_t index{};XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};if(XR_FAILED(xrAcquireSwapchainImage(swapchain,&acquire,&index)))return nullptr;XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
  if(XR_FAILED(xrWaitSwapchainImage(swapchain,&wait)) || index>=images.size())return nullptr;context->UpdateSubresource(images[index].texture,0,nullptr,pixels.data(),Width*4,0);context->Flush();XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};if(XR_FAILED(xrReleaseSwapchainImage(swapchain,&release)))return nullptr;dirty=false;ready=true;
 }
 if(!ready)return nullptr;quad.space=head;quad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.subImage.swapchain=swapchain;quad.subImage.imageArrayIndex=0;quad.subImage.imageRect={{0,0},{int(Width),int(Height)}};quad.pose={{0,0,0,1},{0,-.28f,-1.2f}};quad.size={1.f,float(Height)/Width};return reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
}
void StatusOverlay::Reset(){images.clear();if(swapchain)xrDestroySwapchain(swapchain);swapchain=XR_NULL_HANDLE;last.clear();until=0;dirty=ready=false;}
