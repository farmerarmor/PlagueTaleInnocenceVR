#pragma once
#include <filesystem>
#include <cstdint>
namespace DiagnosticControls {void Initialize(const std::filesystem::path&);bool Poll(uint64_t frame,uint64_t pose);}
