using System;
var first=new[]{1,2}; var second=new[]{3,4};
if(InteriorAssignment.Alias(first,second)!=42 || first[0]!=40 || first[1]!=41 || second[1]!=42 || second[0]!=3)
    throw new Exception("interior local/parameter assignment must rebind tracked pointer without rebinding its copied aliases");
var chars=new[]{'A','B'};
if(InteriorAssignment.Chars(chars)!=0x3bb || chars[0]!='A' || chars[1]!='λ')
    throw new Exception("original-shaped Char-to-ushort interior view assignment across GC");
GC.Collect(2,GCCollectionMode.Forced,true,true);
Console.WriteLine("interior assignment: local/parameter rebinding, independent aliases, original Char/ushort view, mutation and moving GC PASS");
