using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Media;
class Check {
    static readonly BindingFlags All=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance;
    [StructLayout(LayoutKind.Sequential)]
    struct State {
        public uint Version;
        public int Estimated,Priorities,Handlers,Breaking,Incremental,TabCount,Display,Equal;
        public uint Dxp,Dyp,Dxr,Dyr;
        public IntPtr FetchRun;
    }
    [DllImport("PresentationNative_cor3.dll")]
    static extern int WpfGetLineServicesContextState(IntPtr context,out State state);
    static void Main() {
        var assembly=typeof(Typeface).Assembly;
        Type Type(string name)=>assembly.GetType("MS.Internal.TextFormatting."+name,true);
        int Size(string name)=>Marshal.SizeOf(Type(name));
        int Offset(string name,string field)=>Marshal.OffsetOf(Type(name),field).ToInt32();
        Console.WriteLine($"managed ABI LsContextInfo {Size("LsContextInfo")} pols {Offset("LsContextInfo","pols")} pap {Offset("LsContextInfo","pfnFetchPap")} last {Offset("LsContextInfo","fDontReleaseRuns")}; callbacks {Size("LscbkRedefined")} tabs {Size("LsTbd")} state {Marshal.SizeOf<State>()}");
        if(Size("LsContextInfo")!=760 || Offset("LsContextInfo","pols")!=120 ||
           Offset("LsContextInfo","pfnFetchPap")!=184 || Offset("LsContextInfo","fDontReleaseRuns")!=752 ||
           Size("LscbkRedefined")!=24 || Size("LsTbd")!=12 || Marshal.SizeOf<State>()!=64)
            throw new Exception("managed/native LS ABI mismatch");
        var contextType=assembly.GetType("System.Windows.Media.TextFormatting.TextFormatterContext",true);
        object context;
        try { context=Activator.CreateInstance(contextType,true); }
        catch(TargetInvocationException error) { throw error.InnerException; }
        var wrapped=contextType.GetProperty("Ploc",All).GetValue(context);
        var handle=(IntPtr)wrapped.GetType().GetProperty("Value",All).GetValue(wrapped);
        try {
            if(handle==IntPtr.Zero || WpfGetLineServicesContextState(handle,out var state)!=0 ||
               state.Version!=4 || state.Priorities!=3 || state.Dxp!=1440 || state.Dyp!=1440 ||
               state.Dxr!=1440 || state.Dyr!=1440 || state.FetchRun==IntPtr.Zero || state.Display!=1 || state.Equal!=1)
                throw new Exception("original formatter-context creation/configuration failed");
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            if(WpfGetLineServicesContextState(handle,out state)!=0 || state.FetchRun==IntPtr.Zero)
                throw new Exception("original context/callback lifetime failed after GC");
            contextType.GetMethod("SetBreaking",All).Invoke(context,new[]{Enum.ToObject(assembly.GetType("System.Windows.Media.TextFormatting.BreakStrategies",true),1)});
            if(WpfGetLineServicesContextState(handle,out state)!=0 || state.Breaking!=1) throw new Exception("original SetBreaking failed");
            Console.WriteLine("original TextFormatterContext creation, callback table, twips device setup, breaking and GC lifetime PASS");
        } finally { contextType.GetMethod("Destroy",All).Invoke(context,null); }
        if(WpfGetLineServicesContextState(handle,out _)==0) throw new Exception("destroyed managed context still valid");
        contextType.GetMethod("Destroy",All).Invoke(context,null);
        Console.WriteLine("original formatter context teardown/repeated managed teardown PASS");
    }
}
