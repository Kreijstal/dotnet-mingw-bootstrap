using System;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using EnumScope;
foreach(var type in typeof(Host).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static|BindingFlags.DeclaredOnly))
        if(!method.IsAbstract && method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
if(Host.Token()!=typeof(Host.Access) || typeof(Host.Access).DeclaringType!=typeof(Host) ||
   typeof(Host.Access).FullName!="EnumScope.Host+Access" || Enum.GetUnderlyingType(typeof(Host.Access))!=typeof(ushort) ||
   !typeof(Host.Access).IsNestedPublic || !typeof(Host.Access).IsDefined(typeof(FlagsAttribute),false))
    throw new Exception("nested enum identity/flags/visibility");
if(Host.Child.Token()!=typeof(Host.Child.Access) || typeof(Host.Child.Access).DeclaringType!=typeof(Host.Child) ||
   typeof(Host.Child.Access).FullName!="EnumScope.Host+Child+Access" ||
   (long)Host.Child.Access.Negative!=-5000000002L || (long)Host.Child.Access.Positive!=5000000002L ||
   (ulong)Host.Wide.High!=0x8000000000000000UL || (ulong)Host.Wide.Max!=ulong.MaxValue)
    throw new Exception("nested path and wide enum literals");
var values=Host.Values();
if(values.GetType()!=typeof(Host.Access[]) || !values.Cast<Host.Access>().SequenceEqual(new[]{Host.Access.Read,Host.Access.Write,Host.Access.All}) ||
   Host.Parse("Write")!=Host.Access.Write || Host.Round(Host.Access.All)!=Host.Access.All ||
   Host.Box(Host.Access.Read).GetType()!=typeof(Host.Access) || Host.Unbox(Host.Box(Host.Access.Write))!=Host.Access.Write)
    throw new Exception("nested enum values/parse/signatures/boxing");
GC.Collect();
if(!Host.ProtectedToken().IsNestedFamily || !Host.HiddenToken().IsNestedPrivate || !Host.AssemblyToken().IsNestedAssembly)
    throw new Exception("nonpublic nested visibility");
var printing=Expose.PrintingValues();
if(printing.Length!=4 || !printing.Cast<object>().Select(Convert.ToInt32).SequenceEqual(new[]{1,2,5,10}) ||
   printing.GetType().GetElementType()!.DeclaringType!.FullName!="EnumScope.InternalHost" || printing.GetType().GetElementType()!.IsVisible)
    throw new Exception("original printing enum shape");
Console.WriteLine("nested managed enums: CLR paths, access flags, wide constants, boxing, Enum APIs, original printing shape and GC PASS");
