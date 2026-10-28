using System;
using System.Diagnostics;

internal static class NativeDependencies
{
    internal static bool Report()
    {
        bool frameworkDependency = false;
        using var process = Process.GetCurrentProcess();
        foreach (ProcessModule module in process.Modules)
        {
            string path = module.FileName;
            bool framework = path.IndexOf("\\Microsoft.NET\\Framework", StringComparison.OrdinalIgnoreCase) >= 0;
            if (framework || module.ModuleName.Equals("mi.dll", StringComparison.OrdinalIgnoreCase) ||
                module.ModuleName.Equals("mimofcodec.dll", StringComparison.OrdinalIgnoreCase) ||
                module.ModuleName.Equals("wminet_utils.dll", StringComparison.OrdinalIgnoreCase))
                Console.WriteLine("Native dependency: " + path);
            frameworkDependency |= framework;
        }
        return frameworkDependency;
    }
}
