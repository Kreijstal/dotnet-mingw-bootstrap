using namespace System;
using namespace System::Runtime::InteropServices;
#define interface struct
[ComImport(), Guid("138EF579-12A0-4B01-8913-675542958CBD"), InterfaceType(ComInterfaceType::InterfaceIsIUnknown)]
public interface class IComProbe {
    [PreserveSig]
    int Read([In, MarshalAs(UnmanagedType::U8)] UInt64 value, [Out, MarshalAs(UnmanagedType::Bool)] bool% result);
    [PreserveSigAttribute()]
    int Fail();
    [PreserveSig]
    int WriteAddress([Out] IntPtr* address);
};
[ComImport]
[System::Runtime::InteropServices::GuidAttribute("271D54C4-1BE3-4F9F-ACB7-8242A2BB9A34")]
[InterfaceTypeAttribute(ComInterfaceType::InterfaceIsIUnknown)]
public interface class ITranslatedProbe {
    int GetValue();
    void FailWithoutSig();
};
public ref class ParameterFlags {
public:
    static void Set([Runtime::InteropServices::Out] int% value) { value = 42; }
    static int Indexed() { array<int>^ values = gcnew array<int>(2); int Out = 1; values[Out] = 42; return values[Out]; }
};
