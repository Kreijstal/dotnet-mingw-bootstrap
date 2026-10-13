/*
 * Force-included compatibility header for building dotnet/wpf native code
 * (PenImc, wpfgfx) with mingw-w64 GCC/clang instead of MSVC + Windows SDK.
 * SPDX-License-Identifier: MIT
 */
#pragma once

/* make mingw-w64 define _WCHAR_T_DEFINED etc. before WPF headers
   (Shared/inc/ddbanned.h) try to typedef wchar_t themselves */
#include <corecrt.h>

/* ---- legacy (pre-VS2008) SAL annotations used throughout WPF ---------- */
#ifndef __in
#define __in
#endif
#ifndef __out
#define __out
#endif
#ifndef __inout
#define __inout
#endif
#ifndef __in_opt
#define __in_opt
#endif
#ifndef __out_opt
#define __out_opt
#endif
#ifndef __inout_opt
#define __inout_opt
#endif
#ifndef __deref_out
#define __deref_out
#endif
#ifndef __deref_out_opt
#define __deref_out_opt
#endif
#ifndef __deref_inout
#define __deref_inout
#endif
#ifndef __in_ecount
#define __in_ecount(x)
#endif
#ifndef __out_ecount
#define __out_ecount(x)
#endif
#ifndef __inout_ecount
#define __inout_ecount(x)
#endif
#ifndef __in_bcount
#define __in_bcount(x)
#endif
#ifndef __out_bcount
#define __out_bcount(x)
#endif
#ifndef __typefix
#define __typefix(x)
#endif
#ifndef __control_entrypoint
#define __control_entrypoint(x)
#endif

/* ---- winerror.h additions missing from mingw-w64 ----------------------- */
#ifndef E_ILLEGAL_METHOD_CALL
#define E_ILLEGAL_METHOD_CALL ((HRESULT)0x8000000EL)
#endif

#ifdef __cplusplus
/* MSVC's <new> is pulled in transitively by the Windows SDK; WPF relies
   on std::nothrow being declared */
#include <new>

/* The Windows SDK's windef.h defines min()/max() macros in C++ as well;
   mingw-w64 only does so for C.  Provide functions with the same
   mixed-type semantics instead of macros, so that libstdc++/libc++ headers
   keep working.  (libstdc++ defines NOMINMAX itself, so that cannot be
   used as the opt-out.) */
#ifndef WPF_MINGW_NO_MINMAX
template <class _WpfA, class _WpfB>
inline auto max(_WpfA a, _WpfB b) -> decltype((a > b) ? a : b) { return (a > b) ? a : b; }
template <class _WpfA, class _WpfB>
inline auto min(_WpfA a, _WpfB b) -> decltype((a < b) ? a : b) { return (a < b) ? a : b; }
#endif
#endif
