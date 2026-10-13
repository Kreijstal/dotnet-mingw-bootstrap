using System;
using System.Reflection;
int n=new RefNumber(42);
string text=new RefNumber(1);
RefNumber restored=(RefNumber)43;
ValueNumber value=44;
int unwrapped=(int)value;
ExternalNumber external=(ExternalNumber)45;
int externalValue=external;
if(n!=42 || text!="number" || restored.Value!=43 || unwrapped!=44 || externalValue!=45 || (int)(RefNumber)null!=-1)
    throw new Exception("CLR implicit/explicit conversion bodies or out-of-line definition");
foreach(var type in new[]{typeof(RefNumber),typeof(ValueNumber),typeof(ExternalNumber),typeof(TrackingNumber)})
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.Static|BindingFlags.DeclaredOnly))
        if(!method.IsSpecialName || (method.Name!="op_Implicit" && method.Name!="op_Explicit") || method.GetParameters().Length!=1)
            throw new Exception("conversion metadata");
var tracking=typeof(TrackingNumber).GetMethod("op_Implicit");
if(!tracking.GetParameters()[0].ParameterType.IsByRef || (int)tracking.Invoke(null,new object[]{new TrackingNumber(46)})!=46)
    throw new Exception("printing-style tracking-reference conversion");
GC.Collect(); GC.WaitForPendingFinalizers();
if((int)external!=45) throw new Exception("conversion object retention");
Console.WriteLine("CLR source conversions: implicit/explicit, reference/value results and operands, multiple result types, out-of-line definitions, tracking-reference metadata/bodies, null and GC PASS");
