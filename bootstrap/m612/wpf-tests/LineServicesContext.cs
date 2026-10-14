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
    [StructLayout(LayoutKind.Sequential)]
    struct ReverseHandler { public uint Version,Kind; public IntPtr Context,Transition; }
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    delegate int Transition(IntPtr context,int basis,int level,int delta,out int next);
    [DllImport("PresentationNative_cor3.dll")]
    static extern int LocbkGetObjectHandlerInfo(IntPtr context,uint objectId,out ReverseHandler handler);
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
        Transition transition=null;
        try {
            if(handle==IntPtr.Zero || WpfGetLineServicesContextState(handle,out var state)!=0 ||
               state.Version!=4 || state.Priorities!=3 || state.Dxp!=1440 || state.Dyp!=1440 ||
               state.Dxr!=1440 || state.Dyr!=1440 || state.FetchRun==IntPtr.Zero || state.Display!=1 || state.Equal!=1)
                throw new Exception("original formatter-context creation/configuration failed");
            if(LocbkGetObjectHandlerInfo(handle,0,out var reverse)!=0||reverse.Version!=0||reverse.Kind!=0x52565331||reverse.Context!=handle||reverse.Transition==IntPtr.Zero||Marshal.SizeOf<ReverseHandler>()!=24)throw new Exception("source reverse object registration ABI");
            transition=Marshal.GetDelegateForFunctionPointer<Transition>(reverse.Transition);
            if(transition(handle,0,0,2,out var level)!=0||level!=2||transition(handle,0,level,-2,out level)!=0||level!=0||transition(handle,1,1,124,out level)!=0||level!=125)throw new Exception("source reverse handler opening/closing/base transitions");
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            if(transition(handle,0,0,1,out level)!=0||level!=1||transition(handle,1,1,-1,out level)==0||level!=0)throw new Exception("reverse handler lifetime and invalid scope after GC");
            if(WpfGetLineServicesContextState(handle,out state)!=0 || state.FetchRun==IntPtr.Zero)
                throw new Exception("original context/callback lifetime failed after GC");
            contextType.GetMethod("SetBreaking",All).Invoke(context,new[]{Enum.ToObject(assembly.GetType("System.Windows.Media.TextFormatting.BreakStrategies",true),1)});
            if(WpfGetLineServicesContextState(handle,out state)!=0 || state.Breaking!=1) throw new Exception("original SetBreaking failed");
            Console.WriteLine("original TextFormatterContext creation, callback table, twips device setup, breaking and GC lifetime PASS");
        } finally { contextType.GetMethod("Destroy",All).Invoke(context,null); }
        if(WpfGetLineServicesContextState(handle,out _)==0) throw new Exception("destroyed managed context still valid");
        if(transition(handle,0,0,1,out var invalid)==0||invalid!=0||LocbkGetObjectHandlerInfo(handle,0,out var stale)==0||stale.Transition!=IntPtr.Zero)throw new Exception("borrowed reverse handler accepted a destroyed owner");
        contextType.GetMethod("Destroy",All).Invoke(context,null);
        Console.WriteLine("original formatter context teardown/repeated managed teardown PASS");
        Console.WriteLine("original WPF context and source reverse handler registration/transitions/GC/stale owner PASS");
    }
}
