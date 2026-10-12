using namespace System;
using namespace System::Runtime::ConstrainedExecution;
using namespace System::Runtime::InteropServices;
public ref class ReliableHandle : CriticalHandle {
protected:
    virtual bool ReleaseHandle() override { return true; }
public:
    ReliableHandle() : CriticalHandle(IntPtr::Zero) {}
    virtual property bool IsInvalid {
        [ReliabilityContract(Consistency::WillNotCorruptState, Cer::Success)]
        bool get() override { return true; }
    }
    [ReliabilityContractAttribute(Consistency::MayCorruptInstance, Cer::MayFail)]
    static int Read() { return 42; }
};
