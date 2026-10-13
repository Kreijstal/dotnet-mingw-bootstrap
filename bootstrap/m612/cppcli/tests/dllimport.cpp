// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
using namespace System::Runtime::InteropServices;
public ref class DeclaredImports abstract sealed {
public:
    [DllImport("declared-imports-native.dll", EntryPoint="ProbeWide", CharSet=CharSet::Unicode,
        CallingConvention=CallingConvention::Winapi, SetLastError=true, ExactSpelling=true)]
    static int Wide(String^ text, [Out] int% first);
    [DllImportAttribute("declared-imports-native.dll", EntryPoint="ProbeBool", CallingConvention=CallingConvention::Cdecl)]
    static bool Boolean(bool value);
    [DllImport("declared-imports-native.dll", EntryPoint="ProbeArray", CallingConvention=CallingConvention::StdCall)]
    static int Array([In, Out, MarshalAs(UnmanagedType::LPArray, SizeParamIndex=1, ArraySubType=UnmanagedType::I4)] array<int>^ values,int length);
    [DllImport("declared-imports-native.dll", EntryPoint="ProbeHandle", CallingConvention=CallingConvention::Winapi)]
    static int Handle(Microsoft::Win32::SafeHandles::SafeWaitHandle^ value);
    [DllImport("declared-imports-native.dll", EntryPoint="ProbeBuffer", CharSet=CharSet::Unicode, ExactSpelling=true)]
    static int Buffer(System::Text::StringBuilder^ value,int size);
    [DllImport("declared-imports-native.dll", EntryPoint="ProbeAnsi", CharSet=CharSet::Ansi, ExactSpelling=true,
        BestFitMapping=false, ThrowOnUnmappableChar=true, PreserveSig=true)]
    static int Ansi(String^ value);
    static int SourceWide(String^ text) { int first; int length=Wide(text,first); return length+first; }
private:
    [DllImport("declared-imports-native.dll", EntryPoint="ProbeBool")]
    static bool Hidden(bool value);
public:
    static bool SourceBoolean(bool value) { return Hidden(value); }
};
