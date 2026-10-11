// SPDX-License-Identifier: MIT
// Win32 portion of the source bootstrap's PresentationNative replacement.
// Text/paragraph layout exports are deliberately absent until implemented.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define EXPORT extern "C" __declspec(dllexport)
#define CLEAR_ERROR() SetLastError(ERROR_SUCCESS)

EXPORT BOOL WINAPI EnableWindowWrapper(HWND window, BOOL enabled) {
    CLEAR_ERROR(); return EnableWindow(window, enabled);
}
EXPORT LONG WINAPI GetWindowLongWrapper(HWND window, int index) {
    CLEAR_ERROR(); return GetWindowLongW(window, index);
}
EXPORT LONG_PTR WINAPI GetWindowLongPtrWrapper(HWND window, int index) {
    CLEAR_ERROR(); return GetWindowLongPtrW(window, index);
}
EXPORT LONG WINAPI SetWindowLongWrapper(HWND window, int index, LONG value) {
    CLEAR_ERROR(); return SetWindowLongW(window, index, value);
}
EXPORT LONG_PTR WINAPI SetWindowLongPtrWrapper(HWND window, int index, LONG_PTR value) {
    CLEAR_ERROR(); return SetWindowLongPtrW(window, index, value);
}
EXPORT ATOM WINAPI GlobalDeleteAtomWrapper(ATOM atom) {
    CLEAR_ERROR(); return GlobalDeleteAtom(atom);
}
EXPORT BOOL WINAPI GetMenuBarInfoWrapper(HWND window, LONG object, LONG item, MENUBARINFO *info) {
    CLEAR_ERROR(); return GetMenuBarInfo(window, object, item, info);
}
EXPORT HWND WINAPI GetWindowWrapper(HWND window, UINT command) {
    CLEAR_ERROR(); return GetWindow(window, command);
}
EXPORT int WINAPI MapWindowPointsWrapper(HWND from, HWND to, POINT *points, UINT count) {
    CLEAR_ERROR(); return MapWindowPoints(from, to, points, count);
}
EXPORT HWND WINAPI GetAncestorWrapper(HWND window, UINT flags) {
    CLEAR_ERROR(); return GetAncestor(window, flags);
}
EXPORT HWND WINAPI FindWindowExWrapper(HWND parent, HWND after, LPCWSTR class_name, LPCWSTR name) {
    CLEAR_ERROR(); return FindWindowExW(parent, after, class_name, name);
}
EXPORT BOOL WINAPI GetTextExtentPoint32Wrapper(HDC dc, LPCWSTR text, int length, SIZE *size) {
    CLEAR_ERROR(); return GetTextExtentPoint32W(dc, text, length, size);
}
EXPORT int WINAPI GetWindowTextWrapper(HWND window, LPWSTR text, int count) {
    CLEAR_ERROR(); return GetWindowTextW(window, text, count);
}
EXPORT int WINAPI GetWindowTextLengthWrapper(HWND window) {
    CLEAR_ERROR(); return GetWindowTextLengthW(window);
}
EXPORT int WINAPI SetScrollPosWrapper(HWND window, int bar, int position, BOOL redraw) {
    CLEAR_ERROR(); return SetScrollPos(window, bar, position, redraw);
}
EXPORT int WINAPI GetKeyboardLayoutListWrapper(int count, HKL *layouts) {
    CLEAR_ERROR(); return GetKeyboardLayoutList(count, layouts);
}
EXPORT HWND WINAPI GetParentWrapper(HWND window) {
    CLEAR_ERROR(); return GetParent(window);
}
EXPORT HWND WINAPI SetFocusWrapper(HWND window) {
    CLEAR_ERROR(); return SetFocus(window);
}

static bool version_at_least(DWORD major, DWORD minor, WORD service_pack = 0, DWORD build = 0) {
    using RtlGetVersionFn = LONG (WINAPI *)(OSVERSIONINFOEXW *);
    const auto query = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOEXW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!query || query(&version) != 0) return false;
    if (version.dwMajorVersion != major) return version.dwMajorVersion > major;
    if (version.dwMinorVersion != minor) return version.dwMinorVersion > minor;
    if (version.wServicePackMajor != service_pack) return version.wServicePackMajor > service_pack;
    return version.dwBuildNumber >= build;
}

#define VERSION_EXPORT(name, major, minor, service_pack, build) \
    EXPORT bool __cdecl name() { return version_at_least(major, minor, service_pack, build); }
VERSION_EXPORT(IsWindowsXPOrGreater, 5, 1, 0, 0)
VERSION_EXPORT(IsWindowsXPSP1OrGreater, 5, 1, 1, 0)
VERSION_EXPORT(IsWindowsXPSP2OrGreater, 5, 1, 2, 0)
VERSION_EXPORT(IsWindowsXPSP3OrGreater, 5, 1, 3, 0)
VERSION_EXPORT(IsWindowsVistaOrGreater, 6, 0, 0, 0)
VERSION_EXPORT(IsWindowsVistaSP1OrGreater, 6, 0, 1, 0)
VERSION_EXPORT(IsWindowsVistaSP2OrGreater, 6, 0, 2, 0)
VERSION_EXPORT(IsWindows7OrGreater, 6, 1, 0, 0)
VERSION_EXPORT(IsWindows7SP1OrGreater, 6, 1, 1, 0)
VERSION_EXPORT(IsWindows8OrGreater, 6, 2, 0, 0)
VERSION_EXPORT(IsWindows8Point1OrGreater, 6, 3, 0, 0)
VERSION_EXPORT(IsWindows10OrGreater, 10, 0, 0, 0)
VERSION_EXPORT(IsWindows10TH1OrGreater, 10, 0, 0, 10240)
VERSION_EXPORT(IsWindows10TH2OrGreater, 10, 0, 0, 10586)
VERSION_EXPORT(IsWindows10RS1OrGreater, 10, 0, 0, 14393)
VERSION_EXPORT(IsWindows10RS2OrGreater, 10, 0, 0, 15063)
VERSION_EXPORT(IsWindows10RS3OrGreater, 10, 0, 0, 16299)
VERSION_EXPORT(IsWindows10RS4OrGreater, 10, 0, 0, 17134)
VERSION_EXPORT(IsWindows10RS5OrGreater, 10, 0, 0, 17763)

EXPORT bool __cdecl IsWindowsServer() {
    using RtlGetVersionFn = LONG (WINAPI *)(OSVERSIONINFOEXW *);
    const auto query = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOEXW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    return query && query(&version) == 0 && version.wProductType != VER_NT_WORKSTATION;
}
