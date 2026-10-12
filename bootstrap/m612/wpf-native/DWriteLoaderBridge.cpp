// SPDX-License-Identifier: MIT
#include <windows.h>
#include "dwriteloader.cpp"
extern "C" __declspec(dllexport) HMODULE WpfLoadDwrite(void** address) {
    return WPFUtils::LoadDWriteLibraryAndGetProcAddress(address);
}
extern "C" __declspec(dllexport) BOOL WpfUnloadDwrite(HMODULE module) { return FreeLibrary(module); }
