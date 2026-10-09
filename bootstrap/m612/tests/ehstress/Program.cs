// Exception paths of the runtime that cross native (VM, OS) frames: on CoreCLR built with
// clang/MinGW these go through minipal/Windows/mingw_eha.cpp (/EHa emulation) and the
// managed exception dispatch. Each case runs many times to catch leaks and state corruption.
using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

static unsafe class Program
{
    static int failures;
    static void Check(string name, Func<bool> test, int iterations = 2000)
    {
        bool ok = true;
        string err = null;
        try { for (int i = 0; i < iterations && ok; i++) ok = test(); }
        catch (Exception e) { ok = false; err = e.GetType().Name + ": " + e.Message; }
        Console.WriteLine("[{0}] {1}{2}", ok ? "ok  " : "FAIL", name, err != null ? " (" + err + ")" : "");
        if (!ok) failures++;
    }

    class Thrower { public static int Throw(int x) => throw new InvalidOperationException("t" + x); }
    class BadCtor { public BadCtor() => throw new NotSupportedException("ctor"); }
    static class BadCctor { public static int Value = Init(); static int Init() => throw new ArithmeticException("cctor"); }
    struct S { public int A; }

    [DllImport("ucrtbase.dll", CallingConvention = CallingConvention.Cdecl)]
    static extern void qsort(int* b, nuint n, nuint w, delegate* unmanaged[Cdecl]<int*, int*, int> cmp);
    static int calls;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static int Cmp(int* a, int* b) { if (++calls == 5) throw new FormatException("from callback"); return *a - *b; }

    static int Main()
    {
        MethodInfo mi = typeof(Thrower).GetMethod("Throw");
        Check("reflection invoke, target throws (VM frames between managed ones)", () =>
        {
            try { mi.Invoke(null, new object[] { 1 }); return false; }
            catch (TargetInvocationException e) { return e.InnerException is InvalidOperationException; }
        });
        Check("Activator.CreateInstance, constructor throws", () =>
        {
            try { Activator.CreateInstance(typeof(BadCtor)); return false; }
            catch (TargetInvocationException e) { return e.InnerException is NotSupportedException; }
        });
        Check("static constructor throws (EX_CATCH of a managed exception)", () =>
        {
            try { return BadCctor.Value == 42; }
            catch (TypeInitializationException e) { return e.InnerException is ArithmeticException; }
        }, 200);
        Check("Delegate.DynamicInvoke, target throws", () =>
        {
            Func<int, int> f = Thrower.Throw;
            try { f.DynamicInvoke(2); return false; }
            catch (TargetInvocationException e) { return e.InnerException is InvalidOperationException; }
        });
        Check("exception filter, nested finally, rethrow", () =>
        {
            int state = 0;
            try
            {
                try { try { throw new ArgumentException("a"); } finally { state |= 1; } }
                // the filter runs in the first pass, before the inner finally
                catch (ArgumentException) when (state == 0) { state |= 2; throw; }
            }
            catch (ArgumentException) { state |= 4; }
            return state == 7;
        });
        Check("hardware fault in managed code -> NullReferenceException", () =>
        {
            try { S* p = null; return p->A == 1; }
            catch (NullReferenceException) { return true; }
        });
        Check("integer divide by zero", () =>
        {
            int z = Environment.TickCount < 0 ? 1 : 0;
            try { return 1 / z == 0; } catch (DivideByZeroException) { return true; }
        });
        Check("exception from a reverse P/Invoke callback through native qsort frames", () =>
        {
            int[] a = { 5, 4, 3, 2, 1, 9, 8, 7 };
            calls = 0;
            try { fixed (int* p = a) qsort(p, (nuint)a.Length, 4, &Cmp); return false; }
            catch (FormatException) { return true; }
        }, 500);
        Check("Marshal.ThrowExceptionForHR", () =>
        {
            try { Marshal.ThrowExceptionForHR(unchecked((int)0x80070005)); return false; }
            catch (UnauthorizedAccessException) { return true; }
        });
        Check("exceptions on thread pool threads / tasks", () =>
        {
            var t = Task.Run(() => Thrower.Throw(3));
            try { t.Wait(); return false; }
            catch (AggregateException e) { return e.InnerException is InvalidOperationException; }
        }, 500);
        Check("stack trace of a deep exception", () =>
        {
            try { Recurse(40); return false; }
            catch (InvalidOperationException e) { return e.StackTrace.Split('\n').Length >= 40; }
        }, 200);
        Check("Monitor.Wait/Pulse across threads (EE_FINALLY in DoAppropriateWait)", () =>
        {
            object o = new object(); bool done = false;
            var th = new Thread(() => { lock (o) { done = true; Monitor.Pulse(o); } });
            lock (o) { th.Start(); while (!done) Monitor.Wait(o, 1000); }
            th.Join();
            return done;
        }, 200);
        Check("GC under exceptions (allocation + collection)", () =>
        {
            for (int i = 0; i < 20; i++) { try { throw new Exception(new string('x', 100)); } catch { } }
            GC.Collect();
            return true;
        }, 200);
        Console.WriteLine(failures == 0 ? "EHSTRESS OK" : "EHSTRESS FAILED: " + failures);
        return failures;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Recurse(int n) { if (n == 0) throw new InvalidOperationException("deep"); Recurse(n - 1); }
}
