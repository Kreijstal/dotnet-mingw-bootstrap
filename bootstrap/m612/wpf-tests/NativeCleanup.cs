using System;
using System.Runtime.InteropServices;

static class Check {
    [DllImport("native-cleanup-probe.dll", EntryPoint="create_probe", CallingConvention=CallingConvention.Cdecl)] static extern nint Create();
    [DllImport("native-cleanup-probe.dll", EntryPoint="live_probes", CallingConvention=CallingConvention.Cdecl)] static extern int Live();
    static void Main() {
        for(int i=0;i<32;++i) {
            nint pointer=Create();
            var com=WpfNativeCleanup.Com(pointer);
            if(com.IsInvalid||com.IsClosed||Live()!=1) throw new Exception("original COM handle did not acquire the native object");
            var getter=com.GetType().BaseType.GetProperty("Value");
            if((nuint)getter.GetValue(com)!=(nuint)pointer) throw new Exception("unused C++ template getter was not emitted with the actual pointer");
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            com.Dispose(); com.Dispose();
            if(!com.IsInvalid||!com.IsClosed||Live()!=0) throw new Exception("original ReleaseHandle did not release exactly once");
            if((nuint)getter.GetValue(com)!=0) throw new Exception("original pointer getter did not observe closed state");
            var pod=WpfNativeCleanup.Pod();
            if(pod.IsInvalid) throw new Exception("original POD handle lost the allocated GUID");
            pod.Dispose(); pod.Dispose();
            if(!pod.IsInvalid||!pod.IsClosed) throw new Exception("original erased native delete did not close the POD handle");
        }
        var empty=WpfNativeCleanup.Empty(); empty.Dispose();
        if(!empty.IsInvalid||!empty.IsClosed||Live()!=0) throw new Exception("invalid handle cleanup failed");
        Console.WriteLine("complete original WPF NativePointerWrapper.cpp, COM release, owned GUID cleanup and CLR disposal OK");
    }
}
