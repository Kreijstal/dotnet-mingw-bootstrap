// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
struct Pair { int Low; long long High; };
static_assert(sizeof(Pair)==16);
extern "C" __declspec(dllexport) int NativePointerCheck(const void* base, const void* middle, const void* earlier, long long difference) {
    auto* values=static_cast<const Pair*>(base);
    return static_cast<const Pair*>(middle)==values+3 && static_cast<const Pair*>(earlier)==values+1 && difference==3 ? 0 : 1;
}
