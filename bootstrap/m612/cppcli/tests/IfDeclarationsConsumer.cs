using System;
var text=new string("λ🚀".ToCharArray());
if(!ReferenceEquals(text,IfDeclarations.Handle(text)) || IfDeclarations.Calls!=1 || IfDeclarations.Handle(null)!=null || IfDeclarations.Calls!=2)
    throw new Exception("handle condition declaration must preserve null and identity, evaluating once");
if(IfDeclarations.Handle("")!="" || IfDeclarations.Scalar(7)!=8 || IfDeclarations.Scalar(0)!=-1 || IfDeclarations.Calls!=5)
    throw new Exception("handle truth / scalar true and false branch bindings");
if(IfDeclarations.Nested(2)!=3 || IfDeclarations.Nested(0)!=0 || IfDeclarations.Calls!=8)
    throw new Exception("nested condition declarations and shadowed names");
if(IfDeclarations.Init(4)!=9 || IfDeclarations.Init(-1)!=-1 || IfDeclarations.Calls!=12)
    throw new Exception("C++17 init and condition declarations execute in order and are visible in else");
if(IfDeclarations.Loop()!=2 || IfDeclarations.Calls!=15 || IfDeclarations.Protected(8)!=8 || IfDeclarations.Protected(0)!=-1 || IfDeclarations.Calls!=19)
    throw new Exception("loop reinitialization / protected conditional returns");
Console.WriteLine("if declarations: handles/null/identity/GC, scalar true/else binding, C++17 initialization order, shadowing, loop reinitialization and finally returns PASS");
