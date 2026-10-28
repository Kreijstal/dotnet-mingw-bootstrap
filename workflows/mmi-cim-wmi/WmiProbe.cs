using System;
using System.Management;
using System.Runtime.InteropServices;

internal static class WmiProbe
{
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static void Query()
    {
        using (var search = new ManagementObjectSearcher("root\\cimv2",
            "SELECT Caption, Version, BuildNumber FROM Win32_OperatingSystem"))
        using (var results = search.Get())
        {
            int count = 0;
            foreach (ManagementObject row in results)
            using (row)
            {
                Require(!string.IsNullOrEmpty(row["Version"]?.ToString()), "OS version missing");
                Console.WriteLine("WMI OS: {0}; version={1}; build={2}",
                    row["Caption"], row["Version"], row["BuildNumber"]);
                count++;
            }
            Require(count == 1, "Expected exactly one OS instance");
        }
        using (var search = new ManagementObjectSearcher("root\\cimv2",
            "SELECT ProcessId, Name FROM Win32_Process WHERE ProcessId=" + Environment.ProcessId))
        using (var results = search.Get())
        {
            int count = 0;
            foreach (ManagementObject row in results)
            using (row)
            {
                Require(Convert.ToInt32(row["ProcessId"]) == Environment.ProcessId, "Incorrect process ID");
                Console.WriteLine("WMI process: {0}; PID matches current source-built runtime", row["Name"]);
                count++;
            }
            Require(count == 1, "Expected exactly one current process");
        }
        bool rejected = false;
        try
        {
            using var search = new ManagementObjectSearcher("root\\cimv2",
                "SELECT * FROM MSYS2_MmiProbe_ClassThatDoesNotExist");
            using var results = search.Get();
            foreach (ManagementObject row in results) row.Dispose();
        }
        catch (ManagementException error)
        {
            Require(error.ErrorCode == ManagementStatus.InvalidClass, "Wrong WMI error: " + error);
            rejected = true;
            Console.WriteLine("WMI invalid class: correctly returned " + error.ErrorCode);
        }
        Require(rejected, "Invalid class query unexpectedly succeeded");
    }

    private static int Main()
    {
        Console.WriteLine("Runtime: " + RuntimeInformation.FrameworkDescription);
        Console.WriteLine("WMI: " + typeof(ManagementObjectSearcher).Assembly.FullName);
        try
        {
            Query();
            GC.Collect();
            GC.WaitForPendingFinalizers();
            Query();
            if (NativeDependencies.Report())
            {
                Console.WriteLine("LIMITATION: queries passed but require a Microsoft .NET Framework native binary");
                return 2;
            }
            Console.WriteLine("PASS: source-built System.Management local WMI queries, errors, disposal and reopen");
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
