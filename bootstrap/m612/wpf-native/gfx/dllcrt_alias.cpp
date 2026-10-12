/*
 * WpfGfx's DLL entry point (_DllMainStartup, shared/util/DllUtil) creates
 * the process heap and then hands over to the CRT's DLL entry point under
 * its MSVC name.  mingw-w64's CRT calls it DllMainCRTStartup.
 * SPDX-License-Identifier: MIT
 */
#include <windows.h>

extern "C" BOOL WINAPI DllMainCRTStartup(HANDLE hDllHandle, DWORD dwReason, LPVOID lpreserved);

extern "C" BOOL WINAPI _DllMainCRTStartup(HANDLE hDllHandle, DWORD dwReason, LPVOID lpreserved)
{
    return DllMainCRTStartup(hDllHandle, dwReason, lpreserved);
}
