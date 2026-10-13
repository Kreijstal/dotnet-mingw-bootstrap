using System;
if(DoLoops.Once()!=1 || DoLoops.Conditions!=0 || DoLoops.Continue()!=3 || DoLoops.Conditions!=3 || DoLoops.Break()!=1 || DoLoops.Conditions!=3)
    throw new Exception("do first iteration / continue condition / break skips condition");
if(DoLoops.Protected()!=3 || DoLoops.Conditions!=5 || DoLoops.Cleanups!=3 || DoLoops.Nested()!=6 || DoLoops.Return()!=7 || DoLoops.Cleanups!=4)
    throw new Exception("nested do loops / finally cleanup across continue, break and return");
Console.WriteLine("do loops: first iteration, condition evaluation/GC, continue/break, nested loops and finally return/exit cleanup PASS");
