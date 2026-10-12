/*
 * The MSVC build runs TraceWpp.exe (Windows Driver Kit) over core/av to
 * generate per-file <name>.tmh headers implementing the WPP trace macros
 * declared in core/av/avtrace.h.  There is no WPP preprocessor for
 * mingw-w64, so every .tmh just includes this file and tracing compiles to
 * nothing.  SPDX-License-Identifier: MIT
 */
#pragma once
#define LogAVDataX(...)          ((void)0)
#define LogAVDataM(...)          ((void)0)
#define EXPECT_SUCCESS(...)      ((void)0)
#define EXPECT_SUCCESSID(...)    ((void)0)
#ifndef DBG
#define TRACEF(...)
#define TRACEFID(...)
#endif
#define WPP_INIT_TRACING(...)    ((void)0)
#define WPP_CLEANUP(...)         ((void)0)
