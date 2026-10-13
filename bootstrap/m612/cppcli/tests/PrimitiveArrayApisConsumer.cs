using System;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
foreach(var type in typeof(PrimitiveArrayApis).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly))
        if(!method.IsAbstract && method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
var culture=new CultureInfo("fr-FR");
if(PrimitiveArrayApis.Format(-17,culture)!=(-17).ToString(culture) || PrimitiveArrayApis.Receivers!=1 ||
   PrimitiveArrayApis.FormatDouble(12.5,"F2",culture)!=12.5.ToString("F2",culture) ||
   PrimitiveArrayApis.FormatLong(long.MinValue,culture)!=long.MinValue.ToString(culture)) throw new Exception("primitive formatting/evaluation");
if(PrimitiveArrayApis.FormatAlias(-19,culture)!=(-19).ToString(culture) || PrimitiveArrayApis.FormatShadow(12.5,culture)!=12.5.ToString(culture) ||
    PrimitiveArrayApis.RecordShadow(20)!=23) throw new Exception("alias width/record lookup");
if(PrimitiveArrayApis.Boolean(false)!=false.ToString() || PrimitiveArrayApis.Byte(255,culture)!=((byte)255).ToString(culture) ||
    PrimitiveArrayApis.SignedByte(-128,culture)!=((sbyte)-128).ToString(culture) || PrimitiveArrayApis.Short(-32768,culture)!=((short)-32768).ToString(culture) ||
    PrimitiveArrayApis.UnsignedShort(65535,culture)!=((ushort)65535).ToString(culture) || PrimitiveArrayApis.Unsigned(uint.MaxValue,culture)!=uint.MaxValue.ToString(culture) ||
    PrimitiveArrayApis.UnsignedLong(ulong.MaxValue,culture)!=ulong.MaxValue.ToString(culture) || PrimitiveArrayApis.Char('λ')!='λ'.ToString() ||
    PrimitiveArrayApis.Float(1.5f,culture)!=1.5f.ToString(culture)) throw new Exception("primitive width/kind dispatch");
if(!PrimitiveArrayApis.Equal(42,42) || PrimitiveArrayApis.Equal(42,43) || !PrimitiveArrayApis.EqualObject(42,42) ||
    PrimitiveArrayApis.Compare(42,43)!=42.CompareTo(43) || PrimitiveArrayApis.Hash(42)!=42.GetHashCode() ||
    PrimitiveArrayApis.TypeName(42)!=typeof(int).FullName) throw new Exception("primitive typed/Object dispatch");
if(PrimitiveArrayApis.Status(1,culture)!=ParsedStatus.Ready || PrimitiveArrayApis.Status(-1,culture)!=ParsedStatus.Error) throw new Exception("original printing Enum.Parse pattern");
var slots=new OverloadSlots();
if(PrimitiveArrayApis.PrintingOverload(slots,49)!=1 || (int)slots.Stored!=49 || PrimitiveArrayApis.TypedOverload(slots,49)!=2 ||
    PrimitiveArrayApis.NullOverload(slots)!=2) throw new Exception("printing Object/Type overload filtering or typed/null alternatives");
ValueType boxed=PrimitiveArrayApis.Percent(50);
GC.Collect(); GC.WaitForPendingFinalizers();
if(boxed.GetType()!=typeof(BoxData) || ((BoxData)boxed).Value!=50 || PrimitiveArrayApis.PercentObject(51) is not BoxData data || data.Value!=51)
    throw new Exception("unary percent box copy/identity/GC");
if(PrimitiveArrayApis.LocalLiteral()!="Attributes" || PrimitiveArrayApis.LocalNumber(0) is not int zero || zero!=0 ||
    PrimitiveArrayApis.LocalZero()!=null || PrimitiveArrayApis.NativeRemainder(8)!=3+'n') throw new Exception("copy initialization or native remainder/literal");
var numbers=new int[3];
PrimitiveArrayApis.WriteInt(numbers,1,43);
if(numbers[1]!=43 || PrimitiveArrayApis.Read(numbers,1) is not int read || read!=43 ||
    PrimitiveArrayApis.Length(numbers,0)!=3 || PrimitiveArrayApis.Rank(numbers)!=1 ||
    PrimitiveArrayApis.Lower(numbers,0)!=0 || PrimitiveArrayApis.Upper(numbers,0)!=2) throw new Exception("actual Array API dispatch");
var copy=(int[])PrimitiveArrayApis.Clone(numbers); copy[1]=99;
if(numbers[1]!=43) throw new Exception("array clone aliased source");
var strings=new string[] { "first", "second" };
PrimitiveArrayApis.WriteString(strings,1,"last");
var iterator=PrimitiveArrayApis.Enumerate(strings);
GC.Collect(); GC.WaitForPendingFinalizers();
if(!iterator.MoveNext() || (string)iterator.Current!="first" || !iterator.MoveNext() || (string)iterator.Current!="last" || iterator.MoveNext()) throw new Exception("actual array enumerator/GC");
iterator.Reset(); if(!iterator.MoveNext() || (string)iterator.Current!="first") throw new Exception("array enumerator Reset");
try { PrimitiveArrayApis.WriteInt(numbers,3,1); throw new Exception("out-of-range store accepted"); } catch(IndexOutOfRangeException) {}
try { PrimitiveArrayApis.Length(numbers,1); throw new Exception("invalid dimension accepted"); } catch(IndexOutOfRangeException) {}
try { PrimitiveArrayApis.WriteObject(strings,0,new object()); throw new Exception("invalid element accepted"); } catch(InvalidCastException) {}
try { PrimitiveArrayApis.Enumerate(null); throw new Exception("null array accepted"); } catch(NullReferenceException) {}
Console.WriteLine("primitive/array APIs: culture-aware and typed dispatch, receiver once, original printing parse, real Array enumeration/stores/metrics/clone, exceptions and GC PASS");
