/*
 * Runtime pieces that the MSVC build of wpfgfx gets from the Visual C++
 * runtime or from the DirectShow base classes (strmbase.lib), which
 * mingw-w64 does not provide.
 * SPDX-License-Identifier: MIT
 */
#include <windows.h>
#include <eh.h>
#include <strmif.h>

/* --- _set_se_translator (vcruntime) -------------------------------------
 * mingw-w64 declares it in <eh.h> but neither CRT exports it.  WPF uses it
 * (shared/SehException.h) to turn SEH exceptions raised by dynamically
 * called Win32 functions into C++ exceptions; without a translator such an
 * SEH exception is not converted and propagates as SEH. */
static _se_translator_function g_wpf_se_translator;

static _se_translator_function __cdecl wpf_set_se_translator(_se_translator_function f)
{
    _se_translator_function old = g_wpf_se_translator;
    g_wpf_se_translator = f;
    return old;
}

/* <eh.h> declares it dllimport, so callers go through the import slot */
extern "C" {
    _se_translator_function (__cdecl *__imp__Z18_set_se_translatorPFvjP19_EXCEPTION_POINTERSE)(_se_translator_function)
        = wpf_set_se_translator;
}

/* --- DeleteMediaType (DirectShow base classes, mtype.cpp) --------------- */
void WINAPI FreeMediaType(AM_MEDIA_TYPE &mt)
{
    if (mt.cbFormat != 0)
    {
        CoTaskMemFree(mt.pbFormat);
        mt.cbFormat = 0;
        mt.pbFormat = NULL;
    }
    if (mt.pUnk != NULL)
    {
        mt.pUnk->Release();
        mt.pUnk = NULL;
    }
}

void WINAPI DeleteMediaType(AM_MEDIA_TYPE *pmt)
{
    if (pmt != NULL)
    {
        FreeMediaType(*pmt);
        CoTaskMemFree(pmt);
    }
}
