// SPDX-License-Identifier: MIT
using namespace System;
using namespace System::Runtime::InteropServices;
[Guid("4084D4BC-11A0-4F79-BB15-494282FD42A0"), InterfaceType(ComInterfaceType::InterfaceIsIUnknown), ComVisible(true)]
public interface class IArrayService {
    [PreserveSig]
    int Change([In,Out,MarshalAs(UnmanagedType::LPArray, SizeParamIndex=1)] array<unsigned char>^ values,int count);
    [PreserveSig]
    int Fixed([In,MarshalAs(UnmanagedType::LPArray,ArraySubType=UnmanagedType::I4,SizeConst=3)] array<int>^ values);
    [PreserveSig]
    int Combined([In,MarshalAs(UnmanagedType::LPArray,SizeConst=3,SizeParamIndex=1,ArraySubType=UnmanagedType::I4)] array<int>^ values,int count);
    [PreserveSig]
    int FirstIndex(int count,[In,MarshalAs(UnmanagedType::LPArray,SizeParamIndex=0)] array<unsigned char>^ values);
};
[ClassInterface(ClassInterfaceType::None),ComVisible(true)]
public ref class ArrayService:IArrayService {
public:
    virtual int Change(array<unsigned char>^ values,int count) {
        if(values->Length!=count) return -1;
        for(int i=0;i<count;++i) ++values[i];
        return values->Length;
    }
    virtual int Fixed(array<int>^ values) {
        if(values->Length!=3) return -1;
        return values[0]+values[1]+values[2];
    }
    virtual int Combined(array<int>^ values,int count) {
        if(values->Length!=count+3) return -1;
        int result=0;
        for(int i=0;i<values->Length;++i) result+=values[i];
        return result;
    }
    virtual int FirstIndex(int count,array<unsigned char>^ values) {
        if(values->Length!=count) return -1;
        return values[0]+values[count-1];
    }
};
