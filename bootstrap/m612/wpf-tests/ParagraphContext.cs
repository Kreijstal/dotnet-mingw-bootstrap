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
    [DllImport("PresentationNative_cor3.dll")]static extern int WpfGetParagraphAbiField(int index,out IntPtr type,out IntPtr field,out int size,out int offset);
    [STAThread]
    static void Main() {
        var assembly=typeof(FlowDocument).Assembly;
        const string prefix="MS.Internal.PtsHost.UnsafeNativeMethods.PTS+";
        var infoType=assembly.GetType(prefix+"FSCONTEXTINFO",true);var methodsType=assembly.GetType(prefix+"FSIMETHODS",true);var callbacksType=assembly.GetType(prefix+"FSCBK",true);
        if(WpfGetParagraphAbiSizes(out int infoSize,out int methodsSize,out int callbackSize)!=0 || infoSize!=Marshal.SizeOf(infoType) || methodsSize!=Marshal.SizeOf(methodsType) || callbackSize!=Marshal.SizeOf(callbacksType))throw new Exception("original PTS managed/native ABI mismatch");
        Console.WriteLine($"original PTS ABI: context={infoSize}, methods={methodsSize}, callbacks={callbackSize} PASS");
        int fieldCount=0;
        for(;;++fieldCount) {
            int status=WpfGetParagraphAbiField(fieldCount,out var typeName,out var fieldName,out int size,out int offset);
            if(status!=0) { if(fieldCount<200)throw new Exception("incomplete paragraph ABI inventory");break; }
            string record=Marshal.PtrToStringAnsi(typeName),field=Marshal.PtrToStringAnsi(fieldName);
            var recordType=assembly.GetType(prefix+record,true);
            if(size!=Marshal.SizeOf(recordType) || offset!=(int)Marshal.OffsetOf(recordType,field))throw new Exception($"PTS ABI mismatch: {record}.{field}, native {size}/{offset}, managed {Marshal.SizeOf(recordType)}/{Marshal.OffsetOf(recordType,field)}");
        }
        Console.WriteLine($"original PTS page/section/track/text/line records: {fieldCount} field sizes/offsets including all nested union arms PASS");
        var pts=assembly.GetType(prefix.TrimEnd('+'),true);
        var rectType=assembly.GetType(prefix+"FSRECT",true);
        object Rect(int u,int v,int du,int dv) {
            var r=Activator.CreateInstance(rectType);
            foreach(var pair in new[]{("u",u),("v",v),("du",du),("dv",dv)})rectType.GetField(pair.Item1,BindingFlags.Instance|BindingFlags.NonPublic).SetValue(r,pair.Item2);
            return r;
        }
        var transform=pts.GetMethod("FsTransformRectangle",BindingFlags.Static|BindingFlags.NonPublic);
        var args=new object[]{0u,Rect(0,0,900,600),Rect(70,110,83,47),4u,null};
        if((int)transform.Invoke(null,args)!=0)throw new Exception("original PTS transform invocation");
        if((int)rectType.GetField("u",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(args[4])!=747 || (int)rectType.GetField("v",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(args[4])!=110)throw new Exception("original PTS LTR/RTL rectangle marshaling");
        args=new object[]{4u,Rect(0,0,900,600),args[4],0u,null};
        if((int)transform.Invoke(null,args)!=0 || (int)rectType.GetField("u",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(args[4])!=70)throw new Exception("original PTS RTL/LTR round trip");
        Console.WriteLine("original PTS rectangle imports: LTR/RTL coordinates and round-trip marshaling PASS");
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
