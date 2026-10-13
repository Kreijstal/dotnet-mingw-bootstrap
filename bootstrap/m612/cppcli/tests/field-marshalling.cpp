// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#define UNICODE 1
#include <windows.h>
using namespace System;
using namespace System::Runtime::InteropServices;
public enum class FieldAccess : unsigned int { High=0x80000000 };
[StructLayout(LayoutKind::Sequential,CharSet=CharSet::Unicode)]
public ref class FieldDefaults {
public:
    [MarshalAs(UnmanagedType::LPWStr)] String^ DataType;
    [MarshalAs(UnmanagedType::SysInt)] IntPtr DeviceMode;
    [MarshalAs(UnmanagedType::U4)] FieldAccess Access;
    FieldDefaults(String^ data,IntPtr pointer,FieldAccess access):DataType(data),DeviceMode(pointer),Access(access) {}
};
[StructLayout(LayoutKind::Sequential,CharSet=CharSet::Unicode)]
public ref class FieldDocInfo {
public:
    int Size;
    [MarshalAs(UnmanagedType::LPWStr)] String^ Name;
    [MarshalAs(UnmanagedType::LPWStr)] String^ Output;
    [MarshalAs(UnmanagedType::LPWStr)] String^ DataType;
    [MarshalAs(UnmanagedType::U4)] int Types;
    FieldDocInfo(String^ name):Size(sizeof(DOCINFOW)),Name(name),Output(nullptr),DataType("RAW"),Types((int)0x80000000u) {}
};
