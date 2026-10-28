using System;
using System.Linq;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;

internal static class ChannelChecks
{
    public static async Task Run()
    {
        Program.Require(typeof(Channel).Assembly.GetName().Version==new Version(9,0,0,0),"Source9 channel identity");
        var bounded=Channel.CreateBounded<int>(1);
        await bounded.Writer.WriteAsync(1);
        var blocked=bounded.Writer.WriteAsync(2);
        Program.Require(!blocked.IsCompleted,"Bounded writer failed to apply backpressure");
        Program.Require(await bounded.Reader.ReadAsync()==1,"First bounded value");
        await blocked;
        Program.Require(await bounded.Reader.ReadAsync()==2,"Second bounded value");
        using(var cancelled=new CancellationTokenSource())
        {
            cancelled.Cancel();bool caught=false;
            try{await bounded.Reader.ReadAsync(cancelled.Token);}catch(OperationCanceledException){caught=true;}
            Program.Require(caught,"Channel cancellation");
        }
        bounded.Writer.Complete();await bounded.Reader.Completion;
        var dropping=Channel.CreateBounded<int>(new BoundedChannelOptions(2){FullMode=BoundedChannelFullMode.DropOldest});
        foreach(int value in new[]{1,2,3})Program.Require(dropping.Writer.TryWrite(value),"DropOldest write");
        Program.Require(await dropping.Reader.ReadAsync()==2&&await dropping.Reader.ReadAsync()==3,"DropOldest retained values");
        dropping.Writer.Complete();await dropping.Reader.Completion;
        var concurrent=Channel.CreateUnbounded<int>();
        var writers=Enumerable.Range(0,4).Select(worker=>Task.Run(async()=>{
            for(int i=0;i<500;i++)await concurrent.Writer.WriteAsync(worker*500+i);
        })).ToArray();
        var reader=Task.Run(async()=>{long sum=0;int count=0;await foreach(int value in concurrent.Reader.ReadAllAsync()){sum+=value;count++;}return(sum,count);});
        await Task.WhenAll(writers);concurrent.Writer.Complete();var result=await reader;
        Program.Require(result.count==2000&&result.sum==1999000,"Multiwriter channel data lost");
        var failed=Channel.CreateUnbounded<int>();var error=new InvalidOperationException("owned-channel-error");failed.Writer.Complete(error);
        bool readFailed=false,completionFailed=false;
        try{await failed.Reader.ReadAsync();}catch(ChannelClosedException e){readFailed=ReferenceEquals(e.InnerException,error);}
        try{await failed.Reader.Completion;}catch(InvalidOperationException e){completionFailed=ReferenceEquals(e,error);}
        Program.Require(readFailed&&completionFailed,"Channel completion error ownership");
        Console.WriteLine("PASS source9 channel backpressure, cancellation, DropOldest,2000 multiwriter items and exact completion-error propagation");
    }
}
