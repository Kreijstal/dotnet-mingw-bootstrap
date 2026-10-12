/*
 * Wrapper around mingw-w64's <winnt.h> for building WPF.
 * mingw-w64 defines UNREFERENCED_PARAMETER(P) as an assignment, which
 * fails for const parameters; the Windows SDK just evaluates P.
 * SPDX-License-Identifier: MIT
 */
#include_next <winnt.h>
#undef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P) ((void)(P))

/* mingw-w64's C_ASSERT declares an extern array, which is not allowed at
   class scope where WPF also uses it; the SDK version is a typedef. */
#undef C_ASSERT
#ifdef __cplusplus
#define C_ASSERT(e) static_assert((e), #e)
#else
#define C_ASSERT(e) _Static_assert((e), #e)
#endif
