using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.TextFormatting;
using System.Windows.Threading;
class Check {
    [DllImport("PresentationNative_cor3.dll")]static extern int WpfGetParagraphAbiSizes(out int context,out int methods,out int callbacks);
    [DllImport("PresentationNative_cor3.dll")]static extern int WpfGetParagraphContextState(IntPtr context,IntPtr info,IntPtr methods,IntPtr objectContexts);
    [STAThread]
    static void Main() {
        var assembly=typeof(FlowDocument).Assembly;
        const string prefix="MS.Internal.PtsHost.UnsafeNativeMethods.PTS+";
        var infoType=assembly.GetType(prefix+"FSCONTEXTINFO",true);var methodsType=assembly.GetType(prefix+"FSIMETHODS",true);var callbacksType=assembly.GetType(prefix+"FSCBK",true);
        if(WpfGetParagraphAbiSizes(out int infoSize,out int methodsSize,out int callbackSize)!=0 || infoSize!=Marshal.SizeOf(infoType) || methodsSize!=Marshal.SizeOf(methodsType) || callbackSize!=Marshal.SizeOf(callbacksType))throw new Exception("original PTS managed/native ABI mismatch");
        Console.WriteLine($"original PTS ABI: context={infoSize}, methods={methodsSize}, callbacks={callbackSize} PASS");
        var type=assembly.GetType("MS.Internal.PtsHost.PtsContext",true);
        var release=assembly.GetType("MS.Internal.PtsHost.PtsCache",true).GetMethod("ReleaseContext",BindingFlags.Static|BindingFlags.NonPublic);
        foreach(bool optimal in new[]{false,true})
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var context=(IDisposable)Activator.CreateInstance(type,BindingFlags.Instance|BindingFlags.NonPublic,null,new object[]{optimal,mode},null);
            var host=type.GetField("_ptsHost",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(context);
            var handle=(IntPtr)host.GetType().GetProperty("Context",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(host);
            IntPtr info=Marshal.AllocHGlobal(infoSize),methods=Marshal.AllocHGlobal(methodsSize*2),objects=Marshal.AllocHGlobal(IntPtr.Size*2);
            try {
                GC.Collect(2,GCCollectionMode.Forced,true,true);
                if(WpfGetParagraphContextState(handle,info,methods,objects)!=0)throw new Exception("original context not retained across GC");
                int version=Marshal.ReadInt32(info,(int)Marshal.OffsetOf(infoType,"version"));int count=Marshal.ReadInt32(info,(int)Marshal.OffsetOf(infoType,"cInstalledObjects"));
                if(version!=0 || count!=4 || Marshal.ReadIntPtr(objects)!=(IntPtr)10 || Marshal.ReadIntPtr(objects,IntPtr.Size)!=(IntPtr)12)throw new Exception("original container/subpage context identities or installed objects");
                bool penalty=Marshal.ReadIntPtr(info,(int)Marshal.OffsetOf(infoType,"ptsPenaltyModule"))!=IntPtr.Zero;
                if(penalty!=optimal)throw new Exception("original penalty module/context binding");
                if(Marshal.ReadIntPtr(methods)==IntPtr.Zero || Marshal.ReadIntPtr(methods,methodsSize)==IntPtr.Zero)throw new Exception("original object callback metadata was lost");
                Console.WriteLine($"original PtsContext {mode}, optimal={optimal}: installed objects, source object create delegates, retained context/configuration and compacting GC PASS");
            } finally { Marshal.FreeHGlobal(info);Marshal.FreeHGlobal(methods);Marshal.FreeHGlobal(objects);release.Invoke(null,new object[]{context}); }
            var frame=new DispatcherFrame();
            Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle,new Action(()=>frame.Continue=false));
            Dispatcher.PushFrame(frame); // complete the original cache's queued release before shutdown
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();
        Console.WriteLine("original paragraph context registration, pooling/release and dispatcher shutdown PASS");
    }
}
