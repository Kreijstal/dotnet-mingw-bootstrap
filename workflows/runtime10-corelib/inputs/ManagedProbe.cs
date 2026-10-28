using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

internal static class Program
{
    public static int Main(string[] args)
    {
        if (typeof(object).Assembly.GetName().Version!.Major != 10) return 10;
        if (Environment.Version != new Version(10, 0, 5)) return 11;
        if (RuntimeInformation.FrameworkDescription != ".NET 10.0.5") return 12;
        if (args.Length != 1) return 13;
        switch (args[0])
        {
            case "basic":
                var values = new List<int> { 7, 2, 4 };
                values.Sort();
                string text = "source λ 😀";
                return values[0] == 2 && values[2] == 7 && text.Contains("λ") && Math.Sqrt(81) == 9 ? 0 : 20;
            case "gc":
                var objects = new object[2048];
                for (int i = 0; i < objects.Length; i++) objects[i] = new byte[1024];
                GC.Collect(2, GCCollectionMode.Forced, true, true);
                GC.KeepAlive(objects);
                return objects[100] is byte[] bytes && bytes.Length == 1024 ? 0 : 21;
            case "exceptions":
                int completed = 0;
                try { throw new InvalidOperationException("source10"); }
                catch (InvalidOperationException error) { if (error.Message == "source10") completed++; }
                finally { completed++; }
                return completed == 2 ? 0 : 22;
            case "thread":
                int result = 0;
                var worker = new Thread(() => Interlocked.Exchange(ref result, 42));
                worker.Start();
                return worker.Join(10000) && result == 42 ? 0 : 23;
            default:
                return 14;
        }
    }
}
