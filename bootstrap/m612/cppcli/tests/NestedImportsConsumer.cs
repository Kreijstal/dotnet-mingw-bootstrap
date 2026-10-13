using System;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using NestedContract;
foreach(var type in typeof(NestedBridge).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly))
        if(!method.IsAbstract && method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
if(NestedBridge.Echo(Outer.Flags.High)!=Outer.Flags.High || NestedBridge.Facility(unchecked((int)0x80070005))!=7) throw new Exception("nested enum CLR identity/return");
if(NestedBridge.Node("node")!="node" || NestedBridge.Point(41)!=42 || NestedBridge.Detail("deep")!="deep") throw new Exception("nested constructor/field/property/value/deep path");
if(NestedBridge.LiteralNode()!="RAW") throw new Exception("nested constructor literal");
ValueType box=NestedBridge.Box(Outer.Flags.High);
if(box.GetType()!=typeof(Outer.Flags) || (Outer.Flags)box!=Outer.Flags.High) throw new Exception("nested typed box identity");
var api=typeof(NestedBridge).GetMethod("Echo");
if(api.ReturnType!=typeof(Outer.Flags) || api.GetParameters()[0].ParameterType!=typeof(Outer.Flags)) throw new Exception("nested signature identity");
if(!typeof(NestedBridge).GetMethod("Box").ReturnParameter.GetOptionalCustomModifiers().Contains(typeof(Outer.Flags))) throw new Exception("nested box modifier identity");
if(typeof(NestedBridge).Assembly.GetTypes().Any(t=>t.Name.Contains("__cli_nested"))) throw new Exception("nested facade leaked into runtime");
GC.Collect(); GC.WaitForPendingFinalizers();
if((Outer.Flags)box!=Outer.Flags.High || NestedBridge.Node("after GC")!="after GC") throw new Exception("nested GC lifetime");
Console.WriteLine("nested imports: enum/ref/value/deep CLR identities, constructors, fields/properties, cross-assembly TypeRefs, modifiers and GC PASS");
