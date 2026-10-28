using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.Marshalling;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

internal static partial class Program
{
    [LibraryImport("kernel32.dll")]
    private static partial uint GetCurrentProcessId();

    [GeneratedRegex("^(?<word>\\p{L}+)-(?<number>[0-9]+)$")]
    private static partial Regex OwnedRegex();

    private static void Main()
    {
        if (Environment.Version != new Version(8, 0, 31)) throw new Exception("SDK consumer ran on the wrong runtime");
        if (GetCurrentProcessId() != Environment.ProcessId) throw new Exception("Generated native interop mismatch");
        CheckCom();
        if (OwnedRegex().Match("Grüße-42").Groups["number"].Value != "42") throw new Exception("Generated regex failed");
        var original = new OwnedRecord { Text = "Grüße 日本語", Number = 42 };
        string json = JsonSerializer.Serialize(original, OwnedJsonContext.Default.OwnedRecord);
        OwnedRecord copy = JsonSerializer.Deserialize(json, OwnedJsonContext.Default.OwnedRecord)!;
        if (copy.Text != original.Text || copy.Number != 42) throw new Exception("Generated JSON context failed");
        string framework = Path.GetDirectoryName(typeof(object).Assembly.Location)!;
        if (Path.GetDirectoryName(typeof(JsonSerializer).Assembly.Location) != framework) throw new Exception("SDK consumer loaded private JSON");
        Console.WriteLine("SDK CONSUMER RUNTIME " + Environment.Version);
        Console.WriteLine("SDK CONSUMER CORELIB " + typeof(object).Assembly.Location);
        Console.WriteLine("SDK CONSUMER JSON " + FileVersionInfo.GetVersionInfo(typeof(JsonSerializer).Assembly.Location).FileVersion);
        Console.WriteLine("PASS source SDK compiler and original LibraryImport/COM/Regex/JSON generators execute on serviced runtime");
    }

    private static unsafe void CheckCom()
    {
        var owned = new OwnedCom();
        void* pointer = ComInterfaceMarshaller<IOwnedCom>.ConvertToUnmanaged(owned);
        try
        {
            void** table = *(void***)pointer;
            var add = (delegate* unmanaged[MemberFunction]<void*, int, int>)table[3];
            if (add(pointer, 41) != 42) throw new Exception("Generated COM native vtable call failed");
        }
        finally { ComInterfaceMarshaller<IOwnedCom>.Free(pointer); }
        GC.KeepAlive(owned);
    }
}

[GeneratedComInterface]
[Guid("0C8BC7B6-BE1B-45BE-9311-7CBF584A2A63")]
internal partial interface IOwnedCom { [PreserveSig] int Add(int value); }

[GeneratedComClass]
internal partial class OwnedCom : IOwnedCom { public int Add(int value) => value + 1; }

internal sealed class OwnedRecord
{
    public string? Text { get; set; }
    public int Number { get; set; }
}

[JsonSerializable(typeof(OwnedRecord))]
internal partial class OwnedJsonContext : JsonSerializerContext { }
