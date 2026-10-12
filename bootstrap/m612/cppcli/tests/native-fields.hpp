// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#pragma once
#pragma pack(push, 1)
struct InnerFields { unsigned int Word; long long Count; };
struct PackedFields { unsigned char Tag; InnerFields Inner; volatile short Flag; };
#pragma pack(pop)
#define CPPCLI_FIELD_PACK 2
__pragma(pack(push, CPPCLI_FIELD_PACK))
struct MacroPackedFields { unsigned char Tag; long long Count; };
_Pragma("pack(pop)")
struct NaturalFields { unsigned char Tag; long long Count; };
union UnionFields { unsigned int Word; unsigned long long Wide; };
#ifdef CPPCLI_NATIVE_BUILD
extern "C" __declspec(dllexport) int NativeFieldsCheck(const void*);
extern "C" __declspec(dllexport) int NativePackCheck(const void*, const void*);
#else
extern "C" int NativeFieldsCheck(const void*);
extern "C" int NativePackCheck(const void*, const void*);
#endif
