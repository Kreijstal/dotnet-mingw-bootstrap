using System;
using System.Linq;
using System.Collections.Concurrent;
using System.Reflection;
using System.Threading;
using Microsoft.Management.Infrastructure;
using Microsoft.Management.Infrastructure.Options;
using Microsoft.Management.Infrastructure.Serialization;

internal static class MmiPortChecks
{
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new Exception("Expected " + typeof(T).Name);
    }

    private static void Timeouts()
    {
        using var operation = new CimOperationOptions();
        using var destination = new DComSessionOptions();
        foreach (TimeSpan value in new[] {
            TimeSpan.Zero, TimeSpan.FromTicks(1), TimeSpan.FromTicks(10),
            TimeSpan.FromTicks(123456789), new TimeSpan(123, 4, 5, 6, 789), TimeSpan.MaxValue,
        })
        {
            TimeSpan expected = value == TimeSpan.MaxValue ? value : TimeSpan.FromTicks(value.Ticks / 10 * 10);
            operation.Timeout = value;
            destination.Timeout = value;
            Require(operation.Timeout == expected, "Operation timeout precision/sentinel mismatch: " + value);
            Require(destination.Timeout == expected, "Session timeout precision/sentinel mismatch: " + value);
        }
        Throws<ArgumentOutOfRangeException>(() => operation.Timeout = TimeSpan.FromTicks(-1));
        Throws<ArgumentOutOfRangeException>(() => destination.Timeout = TimeSpan.FromSeconds(-1));
        using var session = CimSession.Create(null);
        operation.Timeout = TimeSpan.FromSeconds(20);
        int count = 0;
        foreach (var row in session.QueryInstances("root/cimv2", "WQL", "SELECT Version FROM Win32_OperatingSystem", operation))
        { using (row) { count++; } }
        Require(count == 1, "Timed native query failed");
        Console.WriteLine("PASS: native operation/session timeouts, microseconds, infinity, negatives and timed query");
    }

    private static void Serialization()
    {
        using var session = CimSession.Create(null);
        using var rows = session.QueryInstances("root/cimv2", "WQL", "SELECT Caption,Version FROM Win32_OperatingSystem").GetEnumerator();
        Require(rows.MoveNext(), "OS instance missing");
        using var row = rows.Current;
        using var cls = session.GetClass("root/cimv2", "Win32_OperatingSystem");
        using var serializer = CimSerializer.Create();
        using var deserializer = CimDeserializer.Create();
        const InstanceSerializationOptions flags = InstanceSerializationOptions.IncludeClasses;
        uint needed = 0;
        Require(!serializer.Serialize(row, flags, null, ref needed) && needed > 1, "Sizing query failed");
        byte[] bytes = serializer.Serialize(row, flags);
        Require(bytes.Length == needed, "Convenience/sizing lengths differ");
        uint consumed = 0;
        using (var restored = deserializer.DeserializeInstance(bytes, ref consumed))
        {
            Require(consumed == bytes.Length, "Deserializer consumed incorrect instance length");
            Require(Equals(restored.CimInstanceProperties["Version"].Value, row.CimInstanceProperties["Version"].Value), "Instance property lost");
            Require(restored.CimSystemProperties.ClassName == "Win32_OperatingSystem", "Instance class lost");
        }

        // Nonzero offsets must preserve surrounding bytes and advance by exactly one object.
        byte[] framed = Enumerable.Repeat((byte)0xa5, bytes.Length + 15).ToArray();
        uint offset = 7;
        Require(serializer.Serialize(row, flags, framed, ref offset), "Offset serialization failed");
        Require(offset == 7 + bytes.Length, "Serializer offset incorrect");
        Require(framed.Take(7).All(b => b == 0xa5) && framed.Skip((int)offset).All(b => b == 0xa5), "Serializer damaged framing bytes");
        uint readOffset = 7;
        using (var restored = deserializer.DeserializeInstance(framed, ref readOffset))
        {
            Require(readOffset == offset, "Deserializer offset incorrect");
            Require(Equals(restored.CimInstanceProperties["Caption"].Value, row.CimInstanceProperties["Caption"].Value), "Offset round-trip lost property");
        }

        byte[] tiny = new byte[1];
        uint tinyOffset = 0;
        Require(!serializer.Serialize(row, flags, tiny, ref tinyOffset) && tinyOffset == needed, "Insufficient buffer did not report required size");
        uint badOffset = 1;
        Throws<ArgumentNullException>(() => serializer.Serialize(row, flags, null, ref badOffset));
        badOffset = uint.MaxValue;
        Throws<ArgumentOutOfRangeException>(() => serializer.Serialize(row, flags, bytes, ref badOffset));
        Throws<ArgumentNullException>(() => serializer.Serialize((CimInstance)null, flags));
        Throws<ArgumentOutOfRangeException>(() => serializer.Serialize(row, (InstanceSerializationOptions)2));

        // Cached classes also support compact instance blobs with no embedded class.
        byte[] compact = serializer.Serialize(row, InstanceSerializationOptions.None);
        consumed = 0;
        using (var restored = deserializer.DeserializeInstance(compact, ref consumed, new[] { cls }))
            Require(Equals(restored.CimInstanceProperties["Version"].Value, row.CimInstanceProperties["Version"].Value), "Class-cache round-trip failed");

        byte[] classBytes = serializer.Serialize(cls, ClassSerializationOptions.IncludeParentClasses);
        consumed = 0;
        using (var restoredClass = deserializer.DeserializeClass(classBytes, ref consumed))
        {
            Require(consumed == classBytes.Length, "Deserializer consumed incorrect class length");
            Require(restoredClass.CimClassProperties["Version"] != null, "Class property lost");
        }
        byte[] classFrame = Enumerable.Repeat((byte)0x5a, classBytes.Length + 11).ToArray();
        uint classOffset = 3;
        Require(serializer.Serialize(cls, ClassSerializationOptions.IncludeParentClasses, classFrame, ref classOffset), "Class offset serialization failed");
        Require(classFrame.Take(3).All(b => b == 0x5a) && classFrame.Skip((int)classOffset).All(b => b == 0x5a), "Class serializer damaged framing bytes");
        consumed = 3;
        using (var restoredClass = deserializer.DeserializeClass(classFrame, ref consumed))
            Require(consumed == classOffset && restoredClass.CimClassProperties["Version"] != null, "Class offset round-trip failed");

        // Construct locally; this does not create or change anything in WMI.
        using var unicode = new CimInstance("MSYS2_MmiPortProbe");
        const string text = "Grüße 世界 😀 <&>";
        unicode.CimInstanceProperties.Add(CimProperty.Create("Text", text, CimFlags.None));
        unicode.CimInstanceProperties.Add(CimProperty.Create("Values", new uint[] { 0, 42, uint.MaxValue }, CimFlags.None));
        unicode.CimInstanceProperties.Add(CimProperty.Create("Duration", TimeSpan.FromTicks(123456780), CimFlags.None));
        byte[] unicodeBytes = serializer.Serialize(unicode, flags);
        consumed = 0;
        using (var restored = deserializer.DeserializeInstance(unicodeBytes, ref consumed))
        {
            Require(Equals(restored.CimInstanceProperties["Text"].Value, text), "Unicode/XML characters lost");
            Require(((uint[])restored.CimInstanceProperties["Values"].Value).SequenceEqual(new uint[] { 0, 42, uint.MaxValue }), "Array values lost");
            Require(Equals(restored.CimInstanceProperties["Duration"].Value, TimeSpan.FromTicks(123456780)), "Interval property lost");
        }
        uint invalidOffset = 0;
        Throws<CimException>(() => deserializer.DeserializeInstance(new byte[] { 1, 2, 3 }, ref invalidOffset));
        Require(invalidOffset == 0, "Failed deserialization advanced input offset");
        invalidOffset = (uint)bytes.Length;
        Throws<ArgumentOutOfRangeException>(() => deserializer.DeserializeInstance(bytes, ref invalidOffset));
        invalidOffset = 0;
        Throws<ArgumentNullException>(() => deserializer.DeserializeInstance(null, ref invalidOffset));
        Throws<ArgumentOutOfRangeException>(() => CimSerializer.Create("MI_XML", 1));
        Throws<ArgumentOutOfRangeException>(() => CimDeserializer.Create("MI_XML", 1));

        serializer.Dispose();
        serializer.Dispose();
        deserializer.Dispose();
        deserializer.Dispose();
        Throws<ObjectDisposedException>(() => serializer.Serialize(row, flags));
        consumed = 0;
        Throws<ObjectDisposedException>(() => deserializer.DeserializeInstance(bytes, ref consumed));
        GC.Collect();
        GC.WaitForPendingFinalizers();
        Console.WriteLine("PASS: native MI_XML instance/class round-trips, offsets, Unicode, arrays, intervals, capacity, cached classes, errors and disposal");
    }

    private sealed class AsyncObserver<T> : IObserver<T>, IDisposable where T : class
    {
        internal readonly ConcurrentQueue<T> Values = new ConcurrentQueue<T>();
        internal readonly ManualResetEventSlim Done = new ManualResetEventSlim();
        internal Exception Error;
        internal int TerminalCount;
        internal bool CompactOnNext;
        internal bool ThrowOnNext;
        public void OnNext(T value)
        {
            if (ThrowOnNext) throw new InvalidOperationException("Probe observer failure");
            if (value != null) Values.Enqueue(value);
            if (CompactOnNext)
            {
                CompactOnNext = false;
                GC.Collect(2, GCCollectionMode.Forced, true, true);
                GC.WaitForPendingFinalizers();
            }
        }
        public void OnError(Exception error) { Error = error; Interlocked.Increment(ref TerminalCount); Done.Set(); }
        public void OnCompleted() { Interlocked.Increment(ref TerminalCount); Done.Set(); }
        internal void Wait()
        {
            Require(Done.Wait(TimeSpan.FromSeconds(15)), "Native async operation did not complete");
            Require(TerminalCount == 1, "Observer received duplicate terminal callbacks");
        }
        public void Dispose()
        {
            foreach (var value in Values) (value as IDisposable)?.Dispose();
            Done.Dispose();
        }
    }

    private static int BridgeCounter(string name)
    {
        var type = typeof(CimSession).Assembly.GetType("Microsoft.Management.Infrastructure.Native.MI_OperationCallbackBridge", true);
        return (int)type.GetField(name, BindingFlags.Static | BindingFlags.NonPublic).GetValue(null);
    }

    private static void WaitForNativeCleanup()
    {
        Require(SpinWait.SpinUntil(() => BridgeCounter("ActiveCount") == 0, TimeSpan.FromSeconds(15)), "Native callback roots leaked after operation completion/cancellation");
        Require(BridgeCounter("CleanupFailures") == 0, "Native operation cleanup failed");
    }

    private static void AsynchronousOperations()
    {
        using var session = CimSession.Create(null);
        using (var observer = new AsyncObserver<CimInstance> { CompactOnNext = true })
        using (session.QueryInstancesAsync("root/cimv2", "WQL", "SELECT ProcessId,Name FROM Win32_Process").Subscribe(observer))
        {
            observer.Wait();
            if (observer.Error != null) throw observer.Error;
            Require(observer.Values.Count > 1, "Expected multiple native instance callbacks");
            Require(observer.Values.Any(row => Convert.ToInt32(row.CimInstanceProperties["ProcessId"].Value) == Environment.ProcessId), "Async query lost current process");
            // Native operation cleanup must leave cloned results independently usable.
            WaitForNativeCleanup();
            Require(observer.Values.All(row => row.CimInstanceProperties["Name"].Value is string), "Async result lifetime ended before managed disposal");
        }
        using (var observer = new AsyncObserver<CimClass> { CompactOnNext = true })
        using (session.GetClassAsync("root/cimv2", "Win32_OperatingSystem").Subscribe(observer))
        {
            observer.Wait();
            if (observer.Error != null) throw observer.Error;
            Require(observer.Values.Count == 1 && observer.Values.Single().CimClassProperties["Version"] != null, "Native class callback failed");
        }
        using (var observer = new AsyncObserver<CimInstance>())
        using (session.QueryInstancesAsync("root/cimv2", "WQL", "SELECT * FROM MSYS2_MmiProbe_ClassThatDoesNotExist").Subscribe(observer))
        {
            observer.Wait();
            Require(observer.Error is CimException error && error.NativeErrorCode == NativeErrorCode.InvalidClass, "Native async error did not reach OnError");
            Require(observer.Values.IsEmpty, "Invalid query yielded a value");
            (observer.Error as IDisposable)?.Dispose();
        }
        using (var observer = new AsyncObserver<CimInstance> { ThrowOnNext = true })
        using (session.QueryInstancesAsync("root/cimv2", "WQL", "SELECT Version FROM Win32_OperatingSystem").Subscribe(observer))
        {
            observer.Wait();
            Require(observer.Error is InvalidOperationException && observer.Error.Message == "Probe observer failure", "Observer exception escaped the managed error path");
            WaitForNativeCleanup();
        }
        using (var parameters = new CimMethodParametersCollection())
        using (var observer = new AsyncObserver<CimMethodResultBase>())
        {
            parameters.Add(CimMethodParameter.Create("hDefKey", (uint)0x80000001, CimFlags.In));
            parameters.Add(CimMethodParameter.Create("sSubKeyName", "Software", CimFlags.In));
            // Read-only enumeration of the current user's registry subkeys.
            using var streamingOptions = new CimOperationOptions { EnableMethodResultStreaming = true };
            using (session.InvokeMethodAsync("root/default", "StdRegProv", "EnumKey", parameters, streamingOptions).Subscribe(observer))
            {
                observer.Wait();
                if (observer.Error != null) throw observer.Error;
                Require(Convert.ToUInt32(observer.Values.OfType<CimMethodResult>().Single().ReturnValue.Value) == 0, "Native async method returned an error");
            }
        }
        using (var options = new CimOperationOptions())
        using (var observer = new AsyncObserver<CimInstance>())
        {
            options.WriteMessage = (channel, message) => { };
            options.WriteProgress = (activity, current, status, percent, seconds) => { };
            options.PromptUser = (message, type) => CimResponseType.Yes;
            options.WriteError = error => CimResponseType.No;
            int syncCount=0;
            foreach(var value in session.QueryInstances("root/cimv2", "WQL", "SELECT Version FROM Win32_OperatingSystem", options))
            { using(value){Require(value.CimInstanceProperties["Version"].Value is string,"Extended callback options lost sync query value");syncCount++;} }
            Require(syncCount==1,"Extended callback options lost sync query result");
            using var subscription = session.QueryInstancesAsync("root/cimv2", "WQL", "SELECT Version FROM Win32_OperatingSystem", options).Subscribe(observer);
            observer.Wait();
            if (observer.Error != null) throw observer.Error;
            Require(observer.Values.Count == 1, "Extended callback options lost async query result");
            foreach (var value in observer.Values) value.Dispose();
        }
        const string events = "SELECT * FROM __InstanceCreationEvent WITHIN 1 WHERE TargetInstance ISA 'Win32_Process'";
        using (var observer = new AsyncObserver<CimSubscriptionResult>())
        {
            var subscription = session.SubscribeAsync("root/cimv2", "WQL", events).Subscribe(observer);
            Require(BridgeCounter("ActiveCount") > 0, "Subscription did not retain native callback roots");
            subscription.Dispose();
            subscription.Dispose();
            WaitForNativeCleanup();
            Require(observer.TerminalCount == 0, "Unsubscribe should silently stop observer callbacks");
        }
        using (var observer = new AsyncObserver<CimSubscriptionResult>())
        using (var cancellation = new CancellationTokenSource())
        using (var options = new CimOperationOptions { CancellationToken = cancellation.Token })
        using (session.SubscribeAsync("root/cimv2", "WQL", events, options).Subscribe(observer))
        {
            cancellation.Cancel();
            observer.Wait();
            Require(observer.Error is OperationCanceledException, "CancellationToken did not produce OperationCanceledException");
            WaitForNativeCleanup();
        }
        WaitForNativeCleanup();
        using (var observer = new AsyncObserver<object>())
        using (session.CloseAsync().Subscribe(observer))
        {
            observer.Wait();
            if (observer.Error != null) throw observer.Error;
        }
        session.Close();
        Throws<ObjectDisposedException>(() => session.QueryInstances("root/cimv2", "WQL", "SELECT Version FROM Win32_OperatingSystem"));
        Console.WriteLine("PASS: native asynchronous instances/classes/methods/errors, observer exceptions, compacting GC, independent result lifetime, unsubscribe, CancellationToken, callback-root cleanup and session close");
    }

    internal static int Run()
    {
        MmiStreamChecks.Run();
        MmiExtendedCallbackChecks.Run();
        MmiCredentialChecks.Run();
        Timeouts();
        Serialization();
        AsynchronousOperations();
        Require(!NativeDependencies.Report(), "Unexpected Microsoft .NET Framework dependency");
        Console.WriteLine("PASS: MMI timeout and serialization port checks");
        return 0;
    }
}
