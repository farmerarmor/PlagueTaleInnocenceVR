#pragma once
#include <windows.h>
#include <cstdint>
#include <cwchar>
#include <cstdlib>
namespace HeadsetDisplay {
inline constexpr uint32_t Magic=0x44525844;
struct Settings {uint32_t magic,width,height;float refreshHz;};
static_assert(sizeof(Settings)==16);
inline bool Valid(const Settings& s) {
    return s.magic==Magic && s.width>0 && s.width<=16384 && s.height>0 && s.height<=8192;
}
inline void Name(wchar_t (&name)[96],DWORD pid) {swprintf_s(name,L"Local\\PlagueTaleVR-display-%lu",pid);}
// Keep the HD3D game's logical fullscreen mode while presenting a windowed
// mirror: the OpenXR runtime owns headset pacing, not the desktop monitor.
inline bool Active() { return true; }
inline Settings Requested(){
    Settings s{Magic,0,0,0};wchar_t value[64]{};
    if(GetEnvironmentVariableW(L"PLAGUEVR_EYE_WIDTH",value,64))s.width=wcstoul(value,nullptr,10);
    if(GetEnvironmentVariableW(L"PLAGUEVR_EYE_HEIGHT",value,64))s.height=wcstoul(value,nullptr,10);
    if(GetEnvironmentVariableW(L"PLAGUEVR_REFRESH_MILLIHZ",value,64))s.refreshHz=wcstoul(value,nullptr,10)/1000.f;
    return s;
}
}

