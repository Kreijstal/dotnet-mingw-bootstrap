/*
 * Force-included after wpf_mingw_compat.h when building WpfGfx (milcore)
 * with mingw-w64.
 * SPDX-License-Identifier: MIT
 */
#pragma once

/* Always.h #defines malloc/free/... to CRT_*_DontUse around <stdlib.h> so
   that only deprecated declarations remain; mingw-w64's winnt.h pulls in
   <stdlib.h> (via x86intrin.h) before that, so load it up front instead. */
#include <stdlib.h>
#include <malloc.h>
#ifdef __cplusplus
#include <cstdlib>
#endif

/* ---- legacy SAL (Windows SDK specstrings.h / sal.h / driverspecs.h) ----
   generated from the annotations WpfGfx uses; all are no-ops here */
#ifndef __allocator
#define __allocator
#endif
#ifndef __analysis_noreturn
#define __analysis_noreturn
#endif
#ifndef __bcount
#define __bcount(...)
#endif
#ifndef __bcount_opt
#define __bcount_opt(...)
#endif
#ifndef __bound
#define __bound
#endif
#ifndef __byte_readableTo
#define __byte_readableTo(...)
#endif
#ifndef __byte_writableTo
#define __byte_writableTo(...)
#endif
#ifndef __checkReturn
#define __checkReturn
#endif
#ifndef __deref
#define __deref
#endif
#ifndef __deref_bcount
#define __deref_bcount(...)
#endif
#ifndef __deref_ecount
#define __deref_ecount(...)
#endif
#ifndef __deref_in_range
#define __deref_in_range(...)
#endif
#ifndef __deref_inout
#define __deref_inout
#endif
#ifndef __deref_inout_ecount
#define __deref_inout_ecount(...)
#endif
#ifndef __deref_inout_ecount_inopt
#define __deref_inout_ecount_inopt(...)
#endif
#ifndef __deref_inout_ecount_opt
#define __deref_inout_ecount_opt(...)
#endif
#ifndef __deref_inout_ecount_outopt
#define __deref_inout_ecount_outopt(...)
#endif
#ifndef __deref_inout_opt
#define __deref_inout_opt
#endif
#ifndef __deref_inout_xcount
#define __deref_inout_xcount(...)
#endif
#ifndef __deref_opt_inout_ecount
#define __deref_opt_inout_ecount(...)
#endif
#ifndef __deref_opt_inout_ecount_opt
#define __deref_opt_inout_ecount_opt(...)
#endif
#ifndef __deref_opt_inout_opt
#define __deref_opt_inout_opt
#endif
#ifndef __deref_opt_out
#define __deref_opt_out
#endif
#ifndef __deref_opt_out_ecount
#define __deref_opt_out_ecount(...)
#endif
#ifndef __deref_opt_out_ecount_opt
#define __deref_opt_out_ecount_opt(...)
#endif
#ifndef __deref_opt_out_xcount_part
#define __deref_opt_out_xcount_part(...)
#endif
#ifndef __deref_out
#define __deref_out
#endif
#ifndef __deref_out_bcount
#define __deref_out_bcount(...)
#endif
#ifndef __deref_out_bcount_part
#define __deref_out_bcount_part(...)
#endif
#ifndef __deref_out_ecount
#define __deref_out_ecount(...)
#endif
#ifndef __deref_out_ecount_full
#define __deref_out_ecount_full(...)
#endif
#ifndef __deref_out_ecount_opt
#define __deref_out_ecount_opt(...)
#endif
#ifndef __deref_out_ecount_part
#define __deref_out_ecount_part(...)
#endif
#ifndef __deref_out_opt
#define __deref_out_opt
#endif
#ifndef __deref_out_range
#define __deref_out_range(...)
#endif
#ifndef __deref_out_xcount
#define __deref_out_xcount(...)
#endif
#ifndef __deref_out_xcount_full
#define __deref_out_xcount_full(...)
#endif
#ifndef __deref_out_xcount_opt
#define __deref_out_xcount_opt(...)
#endif
#ifndef __deref_outro_bcount
#define __deref_outro_bcount(...)
#endif
#ifndef __deref_outro_bcount_opt
#define __deref_outro_bcount_opt(...)
#endif
#ifndef __deref_outro_ecount
#define __deref_outro_ecount(...)
#endif
#ifndef __deref_outro_ecount_opt
#define __deref_outro_ecount_opt(...)
#endif
#ifndef __deref_outro_xcount
#define __deref_outro_xcount(...)
#endif
#ifndef __deref_outro_xcount_opt
#define __deref_outro_xcount_opt(...)
#endif
#ifndef __deref_xcount
#define __deref_xcount(...)
#endif
#ifndef __drv_aliasesMem
#define __drv_aliasesMem
#endif
#ifndef __drv_allocatesMem
#define __drv_allocatesMem(...)
#endif
#ifndef __drv_formatString
#define __drv_formatString(...)
#endif
#ifndef __drv_freesMem
#define __drv_freesMem(...)
#endif
#ifndef __drv_functionClass
#define __drv_functionClass(...)
#endif
#ifndef __drv_maxIRQL
#define __drv_maxIRQL(...)
#endif
#ifndef __drv_sameIRQL
#define __drv_sameIRQL
#endif
#ifndef __ecount
#define __ecount(...)
#endif
#ifndef __ecount_opt
#define __ecount_opt(...)
#endif
#ifndef __elem_readableTo
#define __elem_readableTo(...)
#endif
#ifndef __elem_writableTo
#define __elem_writableTo(...)
#endif
#ifndef __exceptthat
#define __exceptthat
#endif
#ifndef __fallthrough
#define __fallthrough
#endif
#ifndef __field_bcount
#define __field_bcount(...)
#endif
#ifndef __field_bcount_opt
#define __field_bcount_opt(...)
#endif
#ifndef __field_bcount_part
#define __field_bcount_part(...)
#endif
#ifndef __field_ecount
#define __field_ecount(...)
#endif
#ifndef __field_ecount_full
#define __field_ecount_full(...)
#endif
#ifndef __field_ecount_full_opt
#define __field_ecount_full_opt(...)
#endif
#ifndef __field_ecount_opt
#define __field_ecount_opt(...)
#endif
#ifndef __field_ecount_part
#define __field_ecount_part(...)
#endif
#ifndef __field_ecount_part_opt
#define __field_ecount_part_opt(...)
#endif
#ifndef __field_range
#define __field_range(...)
#endif
#ifndef __in
#define __in
#endif
#ifndef __in_bcount
#define __in_bcount(...)
#endif
#ifndef __in_bcount_opt
#define __in_bcount_opt(...)
#endif
#ifndef __in_ecount
#define __in_ecount(...)
#endif
#ifndef __in_ecount_opt
#define __in_ecount_opt(...)
#endif
#ifndef __in_opt
#define __in_opt
#endif
#ifndef __in_pcount
#define __in_pcount(...)
#endif
#ifndef __in_pcount_bcount
#define __in_pcount_bcount(...)
#endif
#ifndef __in_pcount_bcount_opt
#define __in_pcount_bcount_opt(...)
#endif
#ifndef __in_pcount_ecount
#define __in_pcount_ecount(...)
#endif
#ifndef __in_pcount_ecount_opt
#define __in_pcount_ecount_opt(...)
#endif
#ifndef __in_pcount_in
#define __in_pcount_in(...)
#endif
#ifndef __in_pcount_in_bcount
#define __in_pcount_in_bcount(...)
#endif
#ifndef __in_pcount_in_ecount
#define __in_pcount_in_ecount(...)
#endif
#ifndef __in_pcount_in_ecount_opt
#define __in_pcount_in_ecount_opt(...)
#endif
#ifndef __in_pcount_in_opt
#define __in_pcount_in_opt(...)
#endif
#ifndef __in_pcount_inout
#define __in_pcount_inout(...)
#endif
#ifndef __in_pcount_inout_bcount
#define __in_pcount_inout_bcount(...)
#endif
#ifndef __in_pcount_inout_bcount_full
#define __in_pcount_inout_bcount_full(...)
#endif
#ifndef __in_pcount_inout_bcount_part
#define __in_pcount_inout_bcount_part(...)
#endif
#ifndef __in_pcount_inout_ecount
#define __in_pcount_inout_ecount(...)
#endif
#ifndef __in_pcount_inout_ecount_full
#define __in_pcount_inout_ecount_full(...)
#endif
#ifndef __in_pcount_inout_ecount_full_opt
#define __in_pcount_inout_ecount_full_opt(...)
#endif
#ifndef __in_pcount_inout_ecount_opt
#define __in_pcount_inout_ecount_opt(...)
#endif
#ifndef __in_pcount_inout_ecount_part
#define __in_pcount_inout_ecount_part(...)
#endif
#ifndef __in_pcount_inout_ecount_part_opt
#define __in_pcount_inout_ecount_part_opt(...)
#endif
#ifndef __in_pcount_inout_opt
#define __in_pcount_inout_opt(...)
#endif
#ifndef __in_pcount_opt
#define __in_pcount_opt(...)
#endif
#ifndef __in_pcount_opt_bcount
#define __in_pcount_opt_bcount(...)
#endif
#ifndef __in_pcount_opt_bcount_opt
#define __in_pcount_opt_bcount_opt(...)
#endif
#ifndef __in_pcount_opt_ecount
#define __in_pcount_opt_ecount(...)
#endif
#ifndef __in_pcount_opt_ecount_opt
#define __in_pcount_opt_ecount_opt(...)
#endif
#ifndef __in_pcount_opt_in
#define __in_pcount_opt_in(...)
#endif
#ifndef __in_pcount_opt_in_opt
#define __in_pcount_opt_in_opt(...)
#endif
#ifndef __in_pcount_opt_inout
#define __in_pcount_opt_inout(...)
#endif
#ifndef __in_pcount_opt_inout_opt
#define __in_pcount_opt_inout_opt(...)
#endif
#ifndef __in_pcount_opt_out
#define __in_pcount_opt_out(...)
#endif
#ifndef __in_pcount_opt_out_opt
#define __in_pcount_opt_out_opt(...)
#endif
#ifndef __in_pcount_out
#define __in_pcount_out(...)
#endif
#ifndef __in_pcount_out_bcount
#define __in_pcount_out_bcount(...)
#endif
#ifndef __in_pcount_out_bcount_full
#define __in_pcount_out_bcount_full(...)
#endif
#ifndef __in_pcount_out_bcount_part
#define __in_pcount_out_bcount_part(...)
#endif
#ifndef __in_pcount_out_ecount
#define __in_pcount_out_ecount(...)
#endif
#ifndef __in_pcount_out_ecount_full
#define __in_pcount_out_ecount_full(...)
#endif
#ifndef __in_pcount_out_ecount_full_opt
#define __in_pcount_out_ecount_full_opt(...)
#endif
#ifndef __in_pcount_out_ecount_opt
#define __in_pcount_out_ecount_opt(...)
#endif
#ifndef __in_pcount_out_ecount_part
#define __in_pcount_out_ecount_part(...)
#endif
#ifndef __in_pcount_out_ecount_part_opt
#define __in_pcount_out_ecount_part_opt(...)
#endif
#ifndef __in_pcount_out_opt
#define __in_pcount_out_opt(...)
#endif
#ifndef __in_range
#define __in_range(...)
#endif
#ifndef __in_xcount
#define __in_xcount(...)
#endif
#ifndef __in_xcount_opt
#define __in_xcount_opt(...)
#endif
#ifndef __in_z
#define __in_z
#endif
#ifndef __inexpressible_readableTo
#define __inexpressible_readableTo(...)
#endif
#ifndef __inexpressible_writableTo
#define __inexpressible_writableTo(...)
#endif
#ifndef __inout
#define __inout
#endif
#ifndef __inout_bcount
#define __inout_bcount(...)
#endif
#ifndef __inout_bcount_part_opt
#define __inout_bcount_part_opt(...)
#endif
#ifndef __inout_ecount
#define __inout_ecount(...)
#endif
#ifndef __inout_ecount_full
#define __inout_ecount_full(...)
#endif
#ifndef __inout_ecount_opt
#define __inout_ecount_opt(...)
#endif
#ifndef __inout_ecount_part
#define __inout_ecount_part(...)
#endif
#ifndef __inout_ecount_part_opt
#define __inout_ecount_part_opt(...)
#endif
#ifndef __inout_opt
#define __inout_opt
#endif
#ifndef __inout_pcount_in
#define __inout_pcount_in(...)
#endif
#ifndef __inout_pcount_in_bcount
#define __inout_pcount_in_bcount(...)
#endif
#ifndef __inout_pcount_in_ecount
#define __inout_pcount_in_ecount(...)
#endif
#ifndef __inout_xcount
#define __inout_xcount(...)
#endif
#ifndef __maybenull
#define __maybenull
#endif
#ifndef __notnull
#define __notnull
#endif
#ifndef __nullterminated
#define __nullterminated
#endif
#ifndef __out
#define __out
#endif
#ifndef __out_bcount
#define __out_bcount(...)
#endif
#ifndef __out_bcount_full
#define __out_bcount_full(...)
#endif
#ifndef __out_bcount_part
#define __out_bcount_part(...)
#endif
#ifndef __out_bcount_part_opt
#define __out_bcount_part_opt(...)
#endif
#ifndef __out_ecount
#define __out_ecount(...)
#endif
#ifndef __out_ecount_full
#define __out_ecount_full(...)
#endif
#ifndef __out_ecount_full_opt
#define __out_ecount_full_opt(...)
#endif
#ifndef __out_ecount_opt
#define __out_ecount_opt(...)
#endif
#ifndef __out_ecount_part
#define __out_ecount_part(...)
#endif
#ifndef __out_opt
#define __out_opt
#endif
#ifndef __out_pcount
#define __out_pcount(...)
#endif
#ifndef __out_pcount_bcount
#define __out_pcount_bcount(...)
#endif
#ifndef __out_pcount_ecount
#define __out_pcount_ecount(...)
#endif
#ifndef __out_pcount_full
#define __out_pcount_full(...)
#endif
#ifndef __out_pcount_full_bcount
#define __out_pcount_full_bcount(...)
#endif
#ifndef __out_pcount_full_ecount
#define __out_pcount_full_ecount(...)
#endif
#ifndef __out_pcount_full_out
#define __out_pcount_full_out(...)
#endif
#ifndef __out_pcount_full_out_bcount
#define __out_pcount_full_out_bcount(...)
#endif
#ifndef __out_pcount_full_out_bcount_full
#define __out_pcount_full_out_bcount_full(...)
#endif
#ifndef __out_pcount_full_out_bcount_part
#define __out_pcount_full_out_bcount_part(...)
#endif
#ifndef __out_pcount_full_out_ecount
#define __out_pcount_full_out_ecount(...)
#endif
#ifndef __out_pcount_full_out_ecount_full
#define __out_pcount_full_out_ecount_full(...)
#endif
#ifndef __out_pcount_full_out_ecount_part
#define __out_pcount_full_out_ecount_part(...)
#endif
#ifndef __out_pcount_out
#define __out_pcount_out(...)
#endif
#ifndef __out_pcount_out_bcount
#define __out_pcount_out_bcount(...)
#endif
#ifndef __out_pcount_out_bcount_full
#define __out_pcount_out_bcount_full(...)
#endif
#ifndef __out_pcount_out_bcount_part
#define __out_pcount_out_bcount_part(...)
#endif
#ifndef __out_pcount_out_ecount
#define __out_pcount_out_ecount(...)
#endif
#ifndef __out_pcount_out_ecount_full
#define __out_pcount_out_ecount_full(...)
#endif
#ifndef __out_pcount_out_ecount_part
#define __out_pcount_out_ecount_part(...)
#endif
#ifndef __out_pcount_out_out_ecount
#define __out_pcount_out_out_ecount(...)
#endif
#ifndef __out_pcount_out_out_ecount_full
#define __out_pcount_out_out_ecount_full(...)
#endif
#ifndef __out_pcount_out_out_ecount_part
#define __out_pcount_out_out_ecount_part(...)
#endif
#ifndef __out_pcount_part
#define __out_pcount_part(...)
#endif
#ifndef __out_pcount_part_bcount
#define __out_pcount_part_bcount(...)
#endif
#ifndef __out_pcount_part_ecount
#define __out_pcount_part_ecount(...)
#endif
#ifndef __out_pcount_part_out
#define __out_pcount_part_out(...)
#endif
#ifndef __out_pcount_part_out_bcount
#define __out_pcount_part_out_bcount(...)
#endif
#ifndef __out_pcount_part_out_bcount_full
#define __out_pcount_part_out_bcount_full(...)
#endif
#ifndef __out_pcount_part_out_bcount_part
#define __out_pcount_part_out_bcount_part(...)
#endif
#ifndef __out_pcount_part_out_ecount
#define __out_pcount_part_out_ecount(...)
#endif
#ifndef __out_pcount_part_out_ecount_full
#define __out_pcount_part_out_ecount_full(...)
#endif
#ifndef __out_pcount_part_out_ecount_part
#define __out_pcount_part_out_ecount_part(...)
#endif
#ifndef __out_range
#define __out_range(...)
#endif
#ifndef __out_xcount
#define __out_xcount(...)
#endif
#ifndef __out_xcount_full
#define __out_xcount_full(...)
#endif
#ifndef __out_xcount_opt
#define __out_xcount_opt(...)
#endif
#ifndef __out_xcount_part
#define __out_xcount_part(...)
#endif
#ifndef __outro
#define __outro
#endif
#ifndef __outro_bcount
#define __outro_bcount(...)
#endif
#ifndef __outro_bcount_opt
#define __outro_bcount_opt(...)
#endif
#ifndef __outro_ecount
#define __outro_ecount(...)
#endif
#ifndef __outro_ecount_opt
#define __outro_ecount_opt(...)
#endif
#ifndef __outro_opt
#define __outro_opt
#endif
#ifndef __outro_xcount
#define __outro_xcount(...)
#endif
#ifndef __outro_xcount_opt
#define __outro_xcount_opt(...)
#endif
#ifndef __override
#define __override
#endif
#ifndef __post
#define __post
#endif
#ifndef __post_invalid
#define __post_invalid
#endif
#ifndef __pre
#define __pre
#endif
#ifndef __range
#define __range(...)
#endif
#ifndef __readonly
#define __readonly
#endif
#ifndef __refparam
#define __refparam
#endif
#ifndef __returnro
#define __returnro
#endif
#ifndef __returnro_bcount
#define __returnro_bcount(...)
#endif
#ifndef __returnro_bcount_opt
#define __returnro_bcount_opt(...)
#endif
#ifndef __returnro_ecount
#define __returnro_ecount(...)
#endif
#ifndef __returnro_ecount_opt
#define __returnro_ecount_opt(...)
#endif
#ifndef __returnro_opt
#define __returnro_opt
#endif
#ifndef __returnro_xcount
#define __returnro_xcount(...)
#endif
#ifndef __returnro_xcount_opt
#define __returnro_xcount_opt(...)
#endif
#ifndef __success
#define __success(...)
#endif
#ifndef __typefix
#define __typefix(...)
#endif
#ifndef __valid
#define __valid
#endif
#ifndef __xcount
#define __xcount(...)
#endif

#ifndef __analysis_assume
#define __analysis_assume(e) ((void)0)
#endif
#ifndef __annotation
#define __annotation(...) ((void)0)
#endif

/* ---- winerror.h (WIC) codes missing from mingw-w64 --------------------- */
#ifndef WINCODEC_ERR_VALUEOVERFLOW
#define WINCODEC_ERR_VALUEOVERFLOW ((HRESULT)0x80070216L)   /* INTSAFE_E_ARITHMETIC_OVERFLOW */
#endif
#ifndef WINCODEC_ERR_INVALIDPARAMETER
#define WINCODEC_ERR_INVALIDPARAMETER ((HRESULT)0x80070057L) /* E_INVALIDARG */
#endif

#ifndef __kernel_entry
#define __kernel_entry
#endif

/* ---- other Windows SDK definitions missing from mingw-w64 -------------- */
#ifndef DWM_E_COMPOSITIONDISABLED
#define DWM_E_COMPOSITIONDISABLED ((HRESULT)0x80263001L)
#endif
#ifndef LODWORD
#define LODWORD(l) ((DWORD)(((DWORD_PTR)(l)) & 0xffffffff))
#endif
#ifndef HIDWORD
#define HIDWORD(l) ((DWORD)((((DWORD_PTR)(l)) >> 32) & 0xffffffff))
#endif
#ifdef __cplusplus
#include <guiddef.h>
/* evr.h (Windows SDK): CLSID_EnhancedVideoRenderer */
__declspec(selectany) extern const GUID CLSID_EnhancedVideoRenderer =
    {0xfa10746c, 0x9b63, 0x4b6c, {0xbc, 0x49, 0xfc, 0x30, 0x0e, 0xa5, 0xf2, 0x56}};
#endif

/* __uuidof() for interfaces whose IID mingw-w64 (or WPF itself, through
   __declspec(uuid)) does not register with its __uuidof emulation */
#ifdef __cplusplus
#include <_mingw.h>
struct IVideoWindow;
__CRT_UUID_DECL(IVideoWindow, 0x56a868b4, 0x0ad4, 0x11ce, 0xb0, 0x3a, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70)
struct IObjectSafety;
__CRT_UUID_DECL(IObjectSafety, 0xcb5bdc81, 0x93c1, 0x11cf, 0x8f, 0x20, 0x00, 0x80, 0x5f, 0x2c, 0xd0, 0x64)
/* core/uce/DpiProvider.h: DECLARE_DELEGATING_INTERFACE(IDpiProvider, "AB9362AC-...") */
struct IDpiProvider;
__CRT_UUID_DECL(IDpiProvider, 0xab9362ac, 0xe5ef, 0x43db, 0x9d, 0x4a, 0x55, 0x62, 0x83, 0x34, 0x1d, 0xc8)
#endif
