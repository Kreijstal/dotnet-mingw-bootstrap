#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>
#include "corjit.h"
#include "jiteeversionguid.h"

class Host final : public ICorJitHost
{
public:
    unsigned callbacks = 0;
    void* allocateMemory(size_t size) override { ++callbacks; return std::malloc(size); }
    void freeMemory(void* memory) override { ++callbacks; std::free(memory); }
    int getIntConfigValue(const char*, int value) override { ++callbacks; return value; }
    const char* getStringConfigValue(const char*) override { ++callbacks; return nullptr; }
    void freeStringConfigValue(const char*) override { ++callbacks; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryExW(argv[1], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) { std::printf("LoadLibraryExW failed: %lu\n", GetLastError()); return 3; }
    auto startup = reinterpret_cast<void (*)(ICorJitHost*)>(GetProcAddress(module, "jitStartup"));
    auto get = reinterpret_cast<ICorJitCompiler* (*)()>(GetProcAddress(module, "getJit"));
    if (!startup || !get) return 4;
    Host host;
    startup(&host);
    if (!host.callbacks) return 8;
    ICorJitCompiler* compiler = get();
    if (!compiler) return 5;
    GUID actual{};
    compiler->getVersionIdentifier(&actual);
    if (std::memcmp(&actual, &JITEEVersionIdentifier, sizeof(actual))) return 6;
    compiler->setTargetOS(CORINFO_WINNT);
    std::printf("PASS original source10 JIT startup, host callbacks and JIT/EE GUID %08lx-%04x-%04x\n",
        actual.Data1, actual.Data2, actual.Data3);
    std::printf("Observed host callbacks: %u\n", host.callbacks);
    if (!FreeLibrary(module)) return 7;
    return 0;
}
