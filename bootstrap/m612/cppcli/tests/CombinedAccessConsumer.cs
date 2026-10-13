using System;
using System.Reflection;
var source=new CombinedAccess();
if(CombinedAccess.SameAssembly(source)!=62 || new LocalAccessDerived().ReadBoth()!=43 || new ForeignAccessDerived().Read()!=20)
    throw new Exception("same assembly/derived combined access");
var flags=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
var type=typeof(CombinedAccess);
if(!type.GetMethod("OrMethod",flags).IsFamilyOrAssembly || !type.GetMethod("AndMethod",flags).IsFamilyAndAssembly || !type.GetMethod("AssemblyOnly",flags).IsAssembly)
    throw new Exception("combined method metadata");
if(!type.GetField("OrField",flags).IsFamilyOrAssembly || !type.GetField("AndField",flags).IsFamilyAndAssembly || !type.GetField("OrStatic",flags).IsFamilyOrAssembly || !type.GetField("AndStatic",flags).IsFamilyAndAssembly)
    throw new Exception("combined field metadata");
var mixed=type.GetProperty("Mixed",flags);
if(!mixed.GetGetMethod(true).IsFamilyOrAssembly || !mixed.GetSetMethod(true).IsFamilyAndAssembly || !type.GetProperty("OrAutomatic",flags).GetGetMethod(true).IsFamilyOrAssembly)
    throw new Exception("combined accessor metadata");
Console.WriteLine("combined CLI access: both token orders, instance/static fields, methods, auto/custom accessors, same assembly and foreign derived CLR calls PASS");
