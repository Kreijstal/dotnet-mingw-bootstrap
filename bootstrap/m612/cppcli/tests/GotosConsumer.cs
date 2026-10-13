using System;
if(Gotos.Classify(new[]{1,1,1})!=4 || Gotos.Classify(new[]{1,2,1})!=0 || Gotos.Backward(3)!=3)
    throw new Exception("original-shaped forward goto and backward branch");
if(Gotos.Protected()!=7 || Gotos.Cleanups!=1 || Gotos.Nested()!=8 || Gotos.Cleanups!=111 || Gotos.Pin(new[]{0})!=9 || Gotos.WithinFinally()!=112)
    throw new Exception("goto must leave protected/pin regions with cleanup and preserve branches within finally");
Console.WriteLine("gotos: forward/backward/classification branches, nested finally unwinding, pin cleanup and same-finally branches PASS");
