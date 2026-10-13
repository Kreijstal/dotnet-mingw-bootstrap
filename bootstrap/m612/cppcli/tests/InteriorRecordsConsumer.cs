using System;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
foreach(var method in typeof(InteriorRecords).GetMethods(BindingFlags.Public|BindingFlags.Static|BindingFlags.DeclaredOnly))
    if(method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
var bitmap=new byte[44];
if(InteriorRecords.Bitmap(bitmap)!=146 || InteriorRecords.Indices!=6 || BitConverter.ToInt32(bitmap,0)!=40 ||
    BitConverter.ToInt32(bitmap,4)!=17 || !bitmap.Skip(40).SequenceEqual(new byte[]{44,43,43,0})) throw new Exception("BITMAPINFO offsets/data/evaluation");
InteriorRecords.Indices=0;
var packed=new byte[21];
if(InteriorRecords.Packed(packed)!=5000000027L || packed[0]!=9 || BitConverter.ToInt64(packed,1)!=5000000002L ||
    BitConverter.ToInt32(packed,13)!=25 || InteriorRecords.Indices!=1) throw new Exception("packed native field/inline array/byref GC");
GC.Collect(); GC.WaitForPendingFinalizers();
if(BitConverter.ToInt32(packed,13)!=25 || bitmap[40]!=44) throw new Exception("record backing buffer lifetime");
InteriorRecords.Indices=0;
var walk=new byte[52];
if(InteriorRecords.Walk(walk)!=50 || InteriorRecords.Indices!=2 || BitConverter.ToUInt32(walk,40)!=10 ||
    BitConverter.ToUInt32(walk,44)!=20 || BitConverter.ToUInt32(walk,48)!=30)
    throw new Exception("tracked record/field views and scaled prefix/postfix/compound pointer updates");
Console.WriteLine("native interior records: original BITMAPINFO fields, packed offsets, inline arrays, updates/index once, tracked aliases and moving GC PASS");
