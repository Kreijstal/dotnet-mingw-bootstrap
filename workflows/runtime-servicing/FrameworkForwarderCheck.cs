using System;
using System.Data.SqlClient;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;

internal static class FrameworkForwarderCheck
{
    static void Main(string[] args)
    {
        if (Environment.Version != new Version(8, 0, 31)) throw new Exception("Actual serviced runtime required");
        Console.WriteLine("Runtime: " + Environment.Version);
        Check("System.Data", typeof(SqlConnection));
        Check("WindowsBase", typeof(System.IO.Packaging.Package));
        Check("System.Security", typeof(System.Security.Cryptography.Pkcs.ContentInfo));
        Check("System.Security", typeof(System.Security.Cryptography.ProtectedData));
        Check("System.ServiceProcess", typeof(System.ServiceProcess.ServiceController));
        Check("mscorlib", typeof(System.Security.AccessControl.EventWaitHandleSecurity));
        Console.WriteLine("PASS six original framework facades resolve to actual source-built NuGet types, including lower-version SqlClient");
        void Check(string facade, Type expected)
        {
            Type actual = Type.GetType(expected.FullName + ", " + facade, throwOnError: true);
            if (actual != expected) throw new Exception("Wrong forwarder target: " + facade + " / " + expected);
            string loaded = Assembly.Load(facade).Location;
            string source = Path.Combine(args[0], facade + ".dll");
            if (!SHA256.HashData(File.ReadAllBytes(source)).AsSpan().SequenceEqual(SHA256.HashData(File.ReadAllBytes(loaded))))
                throw new Exception("Facade did not load from the tested source runtime: " + loaded);
            Console.WriteLine("PASS " + facade + " -> " + actual.AssemblyQualifiedName);
        }
    }
}
