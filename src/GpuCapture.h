#pragma once
#include <d3d11.h>
#include <filesystem>
#include <cstdint>

// Opt-in single native interval. Copies bound constant buffers at the actual
// draw/dispatch, then reads them after a GPU event at Present. No state edits.
namespace GpuCapture {
bool Install(ID3D11Device*,ID3D11DeviceContext*,const std::filesystem::path&,uintptr_t);
void RememberShader(ID3D11DeviceChild*,const void*,SIZE_T,UINT stage);
void RecordCommand(ID3D11DeviceContext*,const char*,UINT,uintptr_t caller);
void Begin(uint64_t frame,uint64_t pose);
void Eye(int eye);
void Camera(int eye,const void* view,const void* projections[],unsigned count);
void Auxiliary(int eye,const void* root,const void* view,const void* projections[],unsigned count);
void End(ID3D11DeviceContext*);
void Flush();
}
