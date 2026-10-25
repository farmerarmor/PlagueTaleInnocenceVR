#pragma once
#include <filesystem>
#include <cstdint>
namespace MenuScreen {
bool Initialize(uintptr_t,const std::filesystem::path&);
bool Refresh();
bool Active();
#ifdef PLAGUE_MENU_TEST_ACCESS
bool TestQuery(uintptr_t image,uintptr_t size,void* gui,bool& menu);
#endif
}
