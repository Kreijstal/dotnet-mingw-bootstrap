using System;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;

class Check {
    static readonly BindingFlags All=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
    static object Call(object target,string name,params object[] arguments) {
        var method=target.GetType().GetMethods(All).Single(m=>m.Name==name && m.GetParameters().Length==arguments.Length);
        return method.Invoke(target,arguments);
    }
    static object Get(object target,string name)=>target.GetType().GetProperty(name,All).GetValue(target);
    static void Main() {
        var assembly=Assembly.Load("DirectWriteForwarder");
        if(assembly.GetName().Version!=new Version(8,0,0,0) ||
           assembly.GetCustomAttribute<AssemblyFileVersionAttribute>()?.Version!="8.0.0.0" ||
           assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion!="8.0.0")
            throw new Exception("source WPF assembly/file/informational version metadata missing");
        var friend=assembly.GetCustomAttributesData().Single(a=>a.AttributeType.FullName=="System.Runtime.CompilerServices.InternalsVisibleToAttribute");
        if(!((string)friend.ConstructorArguments[0].Value).StartsWith("PresentationCore, PublicKey=",StringComparison.Ordinal)) throw new Exception("upstream PresentationCore friend identity missing");
        var forwarded=assembly.GetForwardedTypes().Single(t=>t.FullName=="System.Windows.Media.TextFormattingMode");
        if(forwarded.Assembly.GetName().Name!="WindowsBase") throw new Exception("upstream WindowsBase text-formatting type forwarder missing");
        Type[] types=assembly.GetTypes();
        Console.WriteLine($"complete DirectWriteForwarder assembly loaded: {types.Length} types");
        int prepared=0;
        var failures=new System.Collections.Generic.List<string>();
        foreach(Type type in types) {
            foreach(MethodBase method in type.GetMethods(All|BindingFlags.DeclaredOnly).Cast<MethodBase>().Concat(type.GetConstructors(All|BindingFlags.DeclaredOnly))) {
                if(method.IsAbstract || method.ContainsGenericParameters || method.GetMethodBody()==null) continue;
                try { RuntimeHelpers.PrepareMethod(method.MethodHandle); ++prepared; }
                catch(Exception e) { failures.Add($"{type.FullName}::{method.Name}: {e.GetType().Name}: {e.Message}"); }
            }
        }
        foreach(MethodInfo method in assembly.ManifestModule.GetMethods(All)) {
            if(method.GetMethodBody()==null) continue;
            try { RuntimeHelpers.PrepareMethod(method.MethodHandle); ++prepared; }
            catch(Exception e) { failures.Add($"<Module>::{method.Name}: {e.GetType().Name}: {e.Message}"); }
        }
        foreach(string failure in failures) Console.WriteLine(failure);
        if(failures.Count!=0) throw new Exception($"{failures.Count} complete bridge methods failed CLR preparation");
        Console.WriteLine($"complete bridge method bodies prepared: {prepared}");
        string prefix="MS.Internal.Text.TextInterface.";
        Type factoryType=assembly.GetType(prefix+"Factory",true);
        Type factoryKind=assembly.GetType(prefix+"FactoryType",true);
        var create=factoryType.GetMethods(All).Single(m=>m.Name=="Create" && m.GetParameters().Length==3);
        object factory=create.Invoke(null,new[]{Enum.ToObject(factoryKind,0),null,null});
        try {
            object collection=Call(factory,"GetSystemFontCollection");
            uint count=(uint)Get(collection,"FamilyCount");
            if(count==0) throw new Exception("complete bridge returned no system fonts");
            Console.WriteLine($"system font families through complete bridge: {count}");
            if(DirectWriteFriendProbe.Count()!=count) throw new Exception("signed PresentationCore peer could not call original internal bridge APIs");
            var indexer=collection.GetType().GetMethods(All).Single(m=>m.Name=="get_Item" && m.GetParameters().Length==1 && m.GetParameters()[0].ParameterType==typeof(uint));
            object family=indexer.Invoke(collection,new object[]{0u});
            string name=(string)Get(family,"OrdinalName");
            if(string.IsNullOrEmpty(name)) throw new Exception("complete bridge lost font family Unicode name");
            object font=Call(family,"GetFirstMatchingFont",
                Enum.ToObject(assembly.GetType(prefix+"FontWeight",true),400),
                Enum.ToObject(assembly.GetType(prefix+"FontStretch",true),5),
                Enum.ToObject(assembly.GetType(prefix+"FontStyle",true),0));
            object face=Call(font,"CreateFontFace");
            try {
                if((ushort)Get(face,"GlyphCount")==0) throw new Exception("complete bridge font face has no glyphs");
                object[] rights={ (ushort)0 };
                if(!(bool)face.GetType().GetMethod("ReadFontEmbeddingRights",All).Invoke(face,rights)) throw new Exception("complete bridge failed OS/2 rights read");
                object file=Call(face,"GetFileZero");
                try {
                    GC.Collect(2,GCCollectionMode.Forced,true,true);
                    string path=(string)Call(file,"GetUriPath");
                    if(!System.IO.File.Exists(path)) throw new Exception("complete bridge native font path is missing");
                } finally { ((IDisposable)file).Dispose(); ((IDisposable)file).Dispose(); }
                Console.WriteLine("complete bridge font family, font/face, embedding rights and owned font-file path OK");
            } finally { ((IDisposable)face).Dispose(); ((IDisposable)face).Dispose(); }
        } finally { ((IDisposable)factory).Dispose(); }
        Console.WriteLine("complete DirectWriteForwarder CLR load, managed COM loaders, real factory and font collection OK");
    }
}
