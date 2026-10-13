using System;
var child=new StringChild();
if(LiteralCalls.Inherited(child)!=3 || LiteralCalls.Receivers!=1) throw new Exception("inherited overload selection or receiver count");
if(LiteralCalls.Unicode(child)!=5) throw new Exception("wide/adjacent/parenthesized literal conversion");
if(LiteralCalls.ResourceShape(child)!="IndexOutOfRangeException.InvalidSize") throw new Exception("printing-style inherited String/CultureInfo method");
if(!LiteralCalls.Imported("a needle here") || LiteralCalls.Imported("haystack")) throw new Exception("actual imported String instance method");
if(LiteralCalls.Native()!=5) throw new Exception("native literal ABI changed");
if(!LiteralCalls.NativeBytes()) throw new Exception("native bytes, embedded null or wide alignment");
LiteralCalls.Retain(); GC.Collect(); GC.WaitForPendingFinalizers();
if(LiteralCalls.Retained()!=5) throw new Exception("native literal storage lifetime");
if(!LiteralCalls.ObjectArguments() || !LiteralCalls.LegacyFinally(false)) throw new Exception("Object literal binding or __finally return");
try { LiteralCalls.LegacyFinally(true); throw new InvalidOperationException("no throw"); }
catch(Exception ex) when(ex.GetType()==typeof(Exception)) {}
if(LiteralCalls.FinallyCount!=2) throw new Exception("managed __finally catch/rethrow cleanup");
Console.WriteLine("managed call literal binding: inherited overloads, printing ResourceManager shape, imported instance methods, Unicode/adjacency/parentheses, receiver once and native char/wchar ABI PASS");
