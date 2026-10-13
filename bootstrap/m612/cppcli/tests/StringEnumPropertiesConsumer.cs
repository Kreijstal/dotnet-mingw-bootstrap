using System;
using System.Reflection;
using System.Runtime.CompilerServices;
foreach(var type in typeof(StringEnumProperties).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly))
        if(!method.IsAbstract && method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
var left=new string(new[]{'.','t','t','f'});
var right=new string(new[]{'.','t','t','f'});
if(object.ReferenceEquals(left,right) || !StringEnumProperties.Equal(left,right) || StringEnumProperties.NotEqual(left,right) ||
    !StringEnumProperties.NotEqual(left,".otf") || !StringEnumProperties.Literal(left) || !StringEnumProperties.Unicode("東京 😀"))
    throw new Exception("String CLR content equality/literals");
if(!StringEnumProperties.Equal(null,null) || StringEnumProperties.Equal(left,null) ||
    StringEnumProperties.Equal(null,left) || !StringEnumProperties.NotEqual(null,left)) throw new Exception("null String comparisons");
var source=new ComparisonSource {Text=left,Bits=RenderBits.High|RenderBits.Bold};
if(!StringEnumProperties.Property(source) || StringEnumProperties.Flags(source)!=(RenderBits.High|RenderBits.Italic) || ComparisonSource.Gets!=2)
    throw new Exception("property placeholder read/evaluation/unsigned scoped enum");
GC.Collect(2,GCCollectionMode.Forced,true,true);
if(!StringEnumProperties.Equal(left,right)) throw new Exception("String comparison after GC");
Console.WriteLine("CLR String comparison: separate objects, literals/Unicode/nulls, property getter once, unsigned property enum bitwise and GC PASS");
