#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { std::wcerr << L"Usage: PlagueProbeLoader <pid> <absolute-probe-dll>\n"; return 1; }
    DWORD pid{};
    try { pid = std::stoul(argv[1]); } catch (...) { return 1; }
    const auto path = std::filesystem::absolute(argv[2]);
    if (!std::filesystem::is_regular_file(path)) return 2;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) { std::wcerr << L"OpenProcess error " << GetLastError() << L"\n"; return 3; }
    wchar_t image[32768]{};
    DWORD size = _countof(image);
    if (!QueryFullProcessImageNameW(process, 0, image, &size) || _wcsicmp(std::filesystem::path(image).filename().c_str(), L"APlagueTaleInnocence_x64.exe")) {
        std::wcerr << L"Refusing a process other than APlagueTaleInnocence_x64.exe\n"; CloseHandle(process); return 4;
    }
    // Resolve LoadLibraryW relative to its actual owner (KernelBase on modern Windows).
    const auto load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(load), &owner);
    wchar_t ownerPath[32768]{};
    GetModuleFileNameW(owner, ownerPath, _countof(ownerPath));
    uintptr_t remoteOwner{};
    bool alreadyLoaded = false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32W entry{sizeof(entry)};
    if (snapshot != INVALID_HANDLE_VALUE && Module32FirstW(snapshot, &entry)) {
        do {
            if (!_wcsicmp(entry.szModule, std::filesystem::path(ownerPath).filename().c_str())) remoteOwner = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
            if (!_wcsicmp(entry.szModule, path.filename().c_str())) alreadyLoaded = true;
        } while (Module32NextW(snapshot, &entry));
    }
    if (snapshot != INVALID_HANDLE_VALUE) CloseHandle(snapshot);
    if (!remoteOwner || alreadyLoaded) {
        std::wcerr << (alreadyLoaded ? L"Probe already loaded; restart the game for another capture\n" : L"Cannot resolve remote loader\n");
        CloseHandle(process); return 5;
    }
    const std::wstring text = path.wstring();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    SIZE_T written{};
    if (!remote || !WriteProcessMemory(process, remote, text.c_str(), bytes, &written) || written != bytes) {
        if (remote) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process); return 6;
    }
    const auto address = remoteOwner + (reinterpret_cast<uintptr_t>(load) - reinterpret_cast<uintptr_t>(owner));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(address), remote, 0, nullptr);
    if (!thread) { VirtualFreeEx(process, remote, 0, MEM_RELEASE); CloseHandle(process); return 7; }
    const DWORD wait = WaitForSingleObject(thread, 10000);
    DWORD result{};
    if (wait == WAIT_OBJECT_0) { GetExitCodeThread(thread, &result); VirtualFreeEx(process, remote, 0, MEM_RELEASE); }
    CloseHandle(thread);
    CloseHandle(process);
    if (wait != WAIT_OBJECT_0 || !result) { std::wcerr << L"DLL load failed or timed out\n"; return 8; }
    std::wcout << L"Loaded module; inspect its log beside the DLL.\n";
    return 0;
}

