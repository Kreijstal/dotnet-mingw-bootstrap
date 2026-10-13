// SPDX-License-Identifier: MIT
using namespace System;
using namespace System::Runtime::InteropServices;
using namespace Microsoft::Win32::SafeHandles;
public ref class OrderedHandle:SafeHandleZeroOrMinusOneIsInvalid {
public:
    OrderedHandle():SafeHandleZeroOrMinusOneIsInvalid(true) { SetHandle(IntPtr(42)); }
    long long Read() { return handle.ToInt64(); }
protected:
    virtual bool ReleaseHandle() override { return true; }
};
int main() {
    OrderedHandle^ value=gcnew OrderedHandle();
    if(value->Read()!=42 || value->IsInvalid || value->IsClosed) return 1;
    value->Dispose();
    if(!value->IsClosed) return 2;
    Console::WriteLine("imported SafeHandle inheritance, complete IntPtr value-field dependency, protected access and disposal PASS");
    return 0;
}
