using System;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
if (WindowsApi.Open("SOFTWARE") != 0 || WindowsApi.Process() != (uint)Environment.ProcessId)
    throw new Exception("Windows API calling convention, predefined HKEY or output pointer mismatch");
if (WindowsApi.Open("SOFTWARE\\cppcli-missing-λ-😀-" + Guid.NewGuid()) != 2)
    throw new Exception("missing Unicode registry key did not preserve ERROR_FILE_NOT_FOUND");
var imports = typeof(WindowsApi).Assembly.ManifestModule.GetMethods().Where(m => m.IsDefined(typeof(DllImportAttribute))).ToArray();
if (imports.Length != 3) throw new Exception("unrelated SDK imports were emitted");
foreach (var method in imports) {
    var import = method.GetCustomAttribute<DllImportAttribute>();
    string expected = method.Name.StartsWith("Reg") ? "ADVAPI32.dll" : "KERNEL32.dll";
    if (!string.Equals(import.Value, expected, StringComparison.OrdinalIgnoreCase) || import.EntryPoint != method.Name ||
        import.CallingConvention != CallingConvention.Winapi || !import.ExactSpelling || import.SetLastError ||
        (method.MethodImplementationFlags & MethodImplAttributes.PreserveSig) == 0)
        throw new Exception("incorrect Windows API import metadata: " + method.Name);
}
Console.WriteLine("Windows API import archive bindings, registry read and process ID OK");
