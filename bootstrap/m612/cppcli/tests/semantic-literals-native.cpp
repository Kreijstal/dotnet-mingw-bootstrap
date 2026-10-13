#include <cstring>
#include <cwchar>
#include <cstdint>
extern "C" __declspec(dllexport) int NativeLength(const char* value) { return static_cast<int>(std::strlen(value)); }
extern "C" __declspec(dllexport) int NativeWideLength(const wchar_t* value) { return static_cast<int>(std::wcslen(value)); }
static const char* saved;
extern "C" __declspec(dllexport) void NativeRetain(const char* value) { saved=value; }
extern "C" __declspec(dllexport) int NativeRetainedLength() { return static_cast<int>(std::strlen(saved)); }
extern "C" __declspec(dllexport) int NativeByte(const char* value,int index) { return static_cast<unsigned char>(value[index]); }
extern "C" __declspec(dllexport) int NativeWideUnit(const wchar_t* value,int index) { return value[index]; }
extern "C" __declspec(dllexport) int NativeWideAlignment(const wchar_t* value) { return reinterpret_cast<std::uintptr_t>(value)%alignof(wchar_t); }
