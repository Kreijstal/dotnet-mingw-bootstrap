// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <cstring>
#include <cstdint>
extern "C" __declspec(dllexport) int NativeGuidCheck(const void* pointer) {
    const GUID expected = {0xb2d9f3ec, 0xc9fe, 0x4a11, {0xa2, 0xec, 0xd8, 0x62, 0x08, 0xf7, 0xc0, 0xa2}};
    if (reinterpret_cast<std::uintptr_t>(pointer) % alignof(GUID)) return 0;
    return std::memcmp(pointer, &expected, sizeof(expected)) == 0 ? 42 : 0;
}
