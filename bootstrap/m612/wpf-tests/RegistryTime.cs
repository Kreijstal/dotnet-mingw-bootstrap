using System;
using System.Reflection;
using System.Runtime.InteropServices;

static class Check {
    [StructLayout(LayoutKind.Sequential)] struct FileTime { public uint Low, High; }
    [DllImport("advapi32.dll", ExactSpelling=true)] static extern int RegOpenKeyExW(nint root, [MarshalAs(UnmanagedType.LPWStr)] string path, uint options, uint access, out nint key);
    [DllImport("advapi32.dll", EntryPoint="RegQueryInfoKeyW", ExactSpelling=true)] static extern int Query(nint key, nint name, nint length, nint reserved, nint subkeys, nint maxSubkey, nint maxClass, nint values, nint maxValueName, nint maxValueLength, nint security, out FileTime time);
    [DllImport("advapi32.dll", ExactSpelling=true)] static extern int RegCloseKey(nint key);
    [DllImport("kernel32.dll", ExactSpelling=true)] static extern int GetProcessHandleCount(nint process, out uint count);
    static uint Handles() {
        if (GetProcessHandleCount(-1, out uint count) == 0) throw new Exception("GetProcessHandleCount failed");
        return count;
    }
    static long NativeTime(string path) {
        int error = RegOpenKeyExW(unchecked((nint)(long)-2147483646), path, 0, 1, out nint key);
        if (error != 0) throw new Exception("native registry open failed: " + error);
        try {
            error = Query(key, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, out FileTime time);
            if (error != 0) throw new Exception("native registry query failed: " + error);
            return DateTime.FromFileTimeUtc(((long)time.High << 32) | time.Low).Ticks;
        } finally { if (RegCloseKey(key) != 0) throw new Exception("native registry close failed"); }
    }
    static void Main() {
        var assembly = Assembly.LoadFrom(System.IO.Path.Combine(AppContext.BaseDirectory, "RegistryTime.dll"));
        var type = assembly.GetType("MS.Internal.FontCache.Util2", throwOnError:true);
        var method = type.GetMethod("GetRegistryKeyLastWriteTimeUtc", BindingFlags.Static | BindingFlags.Public);
        if (!method.GetParameters()[1].IsOut || method.GetParameters()[1].ParameterType != typeof(long).MakeByRefType())
            throw new Exception("original Out/Int64 tracking-reference metadata mismatch");
        bool Read(string path, out long ticks) {
            object[] args = { path, -1L };
            bool result = (bool)method.Invoke(null, args);
            ticks = (long)args[1];
            return result;
        }
        const string path = "SOFTWARE";
        for (int i = 0; i < 3; ++i) Read(path, out _);
        uint beforeHandles = Handles();
        for (int i = 0; i < 128; ++i) {
            long before = NativeTime(path);
            GC.Collect(2, GCCollectionMode.Forced, blocking:true, compacting:true);
            if (!Read(path, out long actual)) throw new Exception("original WPF helper failed to open an existing key");
            long after = NativeTime(path);
            if (actual < Math.Min(before, after) || actual > Math.Max(before, after))
                throw new Exception("native FILETIME layout, 64-bit shift or UTC ticks mismatch");
        }
        if (Handles() > beforeHandles + 8) throw new Exception("registry handles leaked across original finally cleanup");
        if (Read("SOFTWARE\\wpf-bootstrap-missing-λ-😀-" + Guid.NewGuid(), out long missing) || missing != 0)
            throw new Exception("missing Unicode key did not return false and zero output");
        Console.WriteLine("complete original WPF util2.cpp, native registry timestamp, Out metadata, GC and handle cleanup OK");
    }
}
