// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include <windows.h>
#include <vcclr.h>
using namespace System;
public ref class WindowsApi abstract sealed {
public:
    static long Open(String^ path) {
        HKEY key = NULL;
        pin_ptr<const wchar_t> name = PtrToStringChars(path);
        long result = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, name, 0, KEY_QUERY_VALUE, &key);
        if (result == ERROR_SUCCESS) return ::RegCloseKey(key);
        return result;
    }
    static unsigned long Process() { return ::GetCurrentProcessId(); }
};
