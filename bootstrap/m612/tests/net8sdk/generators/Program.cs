using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;

var m = Gen.Version().Match("SDK 8.0.131 on .NET 8.0.0");
Console.WriteLine($"[GeneratedRegex] {m.Value} (generated: {Gen.Version().GetType().Name})");
Console.WriteLine($"[LibraryImport] GetCurrentProcessId = {Native.GetCurrentProcessId()} (Environment.ProcessId = {Environment.ProcessId})");
var json = JsonSerializer.Serialize(new Info("m612", [1, 2, 3]), InfoContext.Default.Info);
var back = JsonSerializer.Deserialize(json, InfoContext.Default.Info)!;
Console.WriteLine($"[JsonSerializable] {json} -> {back.Name} {back.Values.Length}");

static partial class Gen
{
    [GeneratedRegex(@"\d+\.\d+\.\d+")]
    public static partial Regex Version();
}
static partial class Native
{
    [LibraryImport("kernel32")]
    public static partial uint GetCurrentProcessId();
}
record Info(string Name, int[] Values);
[JsonSerializable(typeof(Info))]
partial class InfoContext : JsonSerializerContext { }
