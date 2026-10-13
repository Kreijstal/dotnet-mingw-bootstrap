using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
var wide=typeof(DeclaredImports).GetMethod("Wide");
var attr=wide.GetCustomAttribute<DllImportAttribute>();
if(attr.Value!="declared-imports-native.dll" || attr.EntryPoint!="ProbeWide" || attr.CharSet!=CharSet.Unicode ||
   attr.CallingConvention!=CallingConvention.Winapi || !attr.SetLastError || !attr.ExactSpelling || !attr.PreserveSig)
    throw new Exception("DllImport metadata flags");
if(!wide.GetParameters()[1].IsOut || wide.GetParameters()[1].ParameterType!=typeof(int).MakeByRefType())
    throw new Exception("DllImport Out tracking-ref metadata");
if(!typeof(DeclaredImports).GetMethod("Hidden",BindingFlags.Static|BindingFlags.NonPublic).IsPrivate)
    throw new Exception("DllImport method visibility");
var ansi=typeof(DeclaredImports).GetMethod("Ansi").GetCustomAttribute<DllImportAttribute>();
if(ansi.CharSet!=CharSet.Ansi || ansi.BestFitMapping || !ansi.ThrowOnUnmappableChar)
    throw new Exception("DllImport charset mapping flags");
string text="λ🚀";
for(int i=0;i<20;i++) {
    GC.Collect(2,GCCollectionMode.Forced,true,true);
    if(DeclaredImports.Wide(text,out var first)!=3 || first!=0x3bb || Marshal.GetLastPInvokeError()!=1234)
        throw new Exception("Unicode/byref/last-error native call");
    if(DeclaredImports.SourceWide(text)!=3+0x3bb || DeclaredImports.Boolean(true) || !DeclaredImports.SourceBoolean(false))
        throw new Exception("source PInvoke calls / native BOOL marshalling");
    var values=new[]{1,2,3};
    if(DeclaredImports.Array(values,3)!=6 || values[0]!=11 || values[2]!=13)
        throw new Exception("LPArray In/Out copyback");
    using var signal=new EventWaitHandle(false,EventResetMode.ManualReset);
    if(DeclaredImports.Handle(signal.SafeWaitHandle)!=1) throw new Exception("SafeHandle native lifetime");
    var buffer=new StringBuilder(10);
    if(DeclaredImports.Buffer(buffer,10)!=3 || buffer.ToString()!=text) throw new Exception("Unicode StringBuilder copyback");
    if(DeclaredImports.Ansi("ascii")!=5) throw new Exception("ANSI native string");
}
if(DeclaredImports.Wide(null,out var empty)!=-1 || empty!=-1 || DeclaredImports.Ansi(null)!=-1)
    throw new Exception("null string marshalling");
Console.WriteLine("source DllImport: reflection flags/access, Unicode/surrogates/null, BOOL, byref, LPArray, SafeHandle, StringBuilder, last-error and moving GC PASS");
