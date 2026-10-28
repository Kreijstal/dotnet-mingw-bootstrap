using System;
using System.Runtime.InteropServices;
using Microsoft.Management.Infrastructure;
using Microsoft.Management.Infrastructure.Options;
using Microsoft.Management.Infrastructure.Serialization;
using System.Threading;

internal static class MmiProbe
{
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static void Query(CimSession session)
    {
        int count = 0;
        foreach (CimInstance row in session.QueryInstances("root/cimv2", "WQL",
            "SELECT Caption, Version, BuildNumber FROM Win32_OperatingSystem"))
        {
            using (row)
            {
                string version = row.CimInstanceProperties["Version"].Value?.ToString();
                Require(!string.IsNullOrEmpty(version), "OS version missing");
                Console.WriteLine("CIM OS: {0}; version={1}; build={2}",
                    row.CimInstanceProperties["Caption"].Value, version,
                    row.CimInstanceProperties["BuildNumber"].Value);
                count++;
            }
        }
        Require(count == 1, "Expected exactly one OS instance");

        bool found = false;
        foreach (CimInstance row in session.QueryInstances("root/cimv2", "WQL",
            "SELECT ProcessId, Name FROM Win32_Process WHERE ProcessId=" + Environment.ProcessId))
        {
            using (row)
            {
                Require(Convert.ToInt32(row.CimInstanceProperties["ProcessId"].Value) == Environment.ProcessId,
                    "Incorrect process ID");
                Console.WriteLine("CIM process: {0}; PID matches current source-built runtime",
                    row.CimInstanceProperties["Name"].Value);
                found = true;
            }
        }
        Require(found, "Current process missing");

        bool rejected = false;
        try
        {
            foreach (CimInstance row in session.QueryInstances("root/cimv2", "WQL",
                "SELECT * FROM MSYS2_MmiProbe_ClassThatDoesNotExist")) row.Dispose();
        }
        catch (CimException error)
        {
            Require(error.NativeErrorCode == NativeErrorCode.InvalidClass, "Wrong CIM error: " + error);
            rejected = true;
            Console.WriteLine("CIM invalid class: correctly returned " + error.NativeErrorCode);
        }
        Require(rejected, "Invalid class query unexpectedly succeeded");
    }

    private sealed class Observer : IObserver<CimInstance>
    {
        internal readonly ManualResetEventSlim Done = new ManualResetEventSlim();
        internal int Count;
        internal Exception Error;
        public void OnNext(CimInstance row) { row.Dispose(); Count++; }
        public void OnCompleted() { Done.Set(); }
        public void OnError(Exception error) { Error = error; Done.Set(); }
    }

    private static int Capabilities()
    {
        int failures = 0;
        void Check(string name, Action action)
        {
            try { action(); Console.WriteLine("CAPABILITY " + name + ": PASS"); }
            catch (Exception error)
            {
                failures++;
                Console.WriteLine("CAPABILITY " + name + ": FAIL " + error.GetType().Name + ": " + error.Message);
            }
        }
        Check("operation timeout", () => {
            using var options = new CimOperationOptions();
            options.Timeout = TimeSpan.FromSeconds(10);
            Require(options.Timeout == TimeSpan.FromSeconds(10), "Timeout did not round-trip");
        });
        Check("class metadata", () => {
            using var session = CimSession.Create(null);
            using var cls = session.GetClass("root/cimv2", "Win32_OperatingSystem");
            Require(cls.CimClassProperties["Version"] != null, "Class property missing");
        });
        Check("instance serialization", () => {
            using var session = CimSession.Create(null);
            using var rows = session.QueryInstances("root/cimv2", "WQL",
                "SELECT Version FROM Win32_OperatingSystem").GetEnumerator();
            Require(rows.MoveNext(), "OS instance missing");
            using var row = rows.Current;
            using var serializer = CimSerializer.Create();
            uint size = 0;
            serializer.Serialize(row, InstanceSerializationOptions.None, null, ref size);
            Require(size > 0, "Serializer returned no data");
        });
        Check("asynchronous query", () => {
            using var session = CimSession.Create(null);
            var observer = new Observer();
            using var subscription = session.QueryInstancesAsync("root/cimv2", "WQL",
                "SELECT Version FROM Win32_OperatingSystem").Subscribe(observer);
            Require(observer.Done.Wait(TimeSpan.FromSeconds(3)), "No completion callback within 3 seconds");
            if (observer.Error != null) throw observer.Error;
            Require(observer.Count == 1, "Expected one asynchronous result");
        });
        return failures == 0 ? 0 : 3;
    }

    private static int Main(string[] args)
    {
        Console.WriteLine("Runtime: " + RuntimeInformation.FrameworkDescription);
        Console.WriteLine("MMI: " + typeof(CimSession).Assembly.FullName);
        try
        {
            if (args.Length == 1 && args[0] == "--capabilities") return Capabilities();
            if (args.Length == 1 && args[0] == "--port-checks") return MmiPortChecks.Run();
            using (var options = new DComSessionOptions())
            using (var session = CimSession.Create(null, options)) Query(session);
            GC.Collect();
            GC.WaitForPendingFinalizers();
            using (var session = CimSession.Create(null)) Query(session);
            Require(!NativeDependencies.Report(), "Unexpected Microsoft .NET Framework native dependency");
            Console.WriteLine("PASS: source-built MMI local DCOM/default queries, errors, disposal and reopen");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
