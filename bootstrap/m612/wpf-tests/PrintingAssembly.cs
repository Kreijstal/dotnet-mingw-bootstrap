using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
class Check {
    [STAThread]
    static int Main(string[] args) {
        var assembly=Assembly.Load("System.Printing");
        if(assembly.GetName().Version!=new Version(8,0,0,0) ||
           assembly.GetCustomAttribute<AssemblyFileVersionAttribute>()?.Version!="8.0.0.0" ||
           assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion!="8.0.0")
            throw new Exception("printing version metadata");
        var friend=assembly.GetCustomAttributesData().Single(a=>a.AttributeType.FullName=="System.Runtime.CompilerServices.InternalsVisibleToAttribute");
        if(!((string)friend.ConstructorArguments[0].Value).StartsWith("ReachFramework, PublicKey=",StringComparison.Ordinal))
            throw new Exception("upstream printing friend identity");
        Type[] types;
        try { types=assembly.GetTypes(); }
        catch(ReflectionTypeLoadException e) {
            foreach(var failure in e.LoaderExceptions) Console.WriteLine(failure);
            return 1;
        }
        Console.WriteLine($"complete System.Printing assembly loaded: {types.Length} types");
        const BindingFlags All=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static|BindingFlags.DeclaredOnly;
        int prepared=0;
        var failures=new List<string>();
        void Prepare(MethodBase method) {
            try {
                if(method.IsAbstract || method.ContainsGenericParameters || method.GetMethodBody()==null) return;
                RuntimeHelpers.PrepareMethod(method.MethodHandle); ++prepared;
            }
            catch(Exception e) { failures.Add($"{method.DeclaringType?.FullName ?? "<Module>"}::{method.Name}: {e}"); }
        }
        foreach(var type in types)
            foreach(var method in type.GetMethods(All).Cast<MethodBase>().Concat(type.GetConstructors(All))) Prepare(method);
        foreach(var method in assembly.ManifestModule.GetMethods(All)) Prepare(method);
        foreach(var failure in failures) Console.WriteLine(failure);
        Console.WriteLine($"complete printing methods prepared: {prepared}; failures: {failures.Count}");
        if(failures.Count!=0) return 1;
        var enumeration=assembly.GetType("MS.Internal.PrintWin32Thunk.AttributeValueInteropHandler+PrintPropertyTypeInterop",true);
        var values=Enum.GetValues(enumeration);
        if(!values.Cast<object>().Select(Convert.ToInt32).SequenceEqual(new[]{1,2,5,10}))
            throw new Exception("original printing enum values");
        Console.WriteLine("complete printing metadata and CLR method preparation PASS");
        if(args.Length!=0 && args[0]=="runtime") PrintingRuntime.Run();
        return 0;
    }
}
