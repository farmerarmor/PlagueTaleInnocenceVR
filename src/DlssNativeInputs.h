#pragma once
#include <dxgiformat.h>

// View only the depth component of a native typeless depth/stencil texture.
// The captured game scene uses R32G8X24_TYPELESS (format 19), not R24G8.
inline constexpr DXGI_FORMAT DlssDepthViewFormat(DXGI_FORMAT nativeFormat) {
 switch(nativeFormat){
 case DXGI_FORMAT_R32G8X24_TYPELESS:return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
 case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_FLOAT;
 case DXGI_FORMAT_R24G8_TYPELESS:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
 default:return DXGI_FORMAT_UNKNOWN;
 }
}
