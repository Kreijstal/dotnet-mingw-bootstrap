using System;
using System.Reflection;
var box=new AccessorBox();
if(box.Value!=7||box.After()!=8) throw new Exception("public getter or following member visibility lost");
box.Update(23);
if(box.Value!=23||box.PublicAfter()!=23) throw new Exception("same-assembly internal setter access failed");
var value=typeof(AccessorBox).GetProperty("Value");
if(!value.GetMethod.IsPublic||!value.SetMethod.IsAssembly||!value.GetMethod.IsVirtual||!value.SetMethod.IsVirtual)
    throw new Exception("per-accessor public/internal virtual metadata lost");
var other=typeof(AccessorBox).GetProperty("ProtectedRead",BindingFlags.Instance|BindingFlags.NonPublic);
if(!other.GetMethod.IsFamily||!other.SetMethod.IsPrivate||!typeof(AccessorBox).GetMethod("PublicAfter").IsPublic)
    throw new Exception("per-accessor protected/private metadata or enclosing visibility restoration lost");
box.UpdateExternal(31);
if(box.External!=31||!typeof(AccessorBox).GetProperty("External").SetMethod.IsAssembly)
    throw new Exception("out-of-line accessor internal metadata or binding lost");
Console.WriteLine("property accessor visibility: public/internal/protected/private flags, virtual slots, same-assembly setter and enclosing visibility restoration PASS");
