using System;
using System.Buffers;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Composition;
using System.Composition.Hosting;
using System.Globalization;
using System.IO;
using System.IO.Pipelines;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Resources;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

public interface IService { string Text { get; } }
[Export(typeof(IService)), Shared]
public sealed class Service : IService { public string Text => "source9 λ"; }
[Export]
public sealed class Client
{
    [ImportingConstructor] public Client(IService service) { Service = service; }
    public IService Service { get; }
}
internal static class Program
{
    public static async Task<int> Main(string[] args)
    {
        var values = ImmutableArray.Create(1, 2).Add(3);
        if (values.Length != 3 || values[2] != 3) return 1;
        TypeName parsed = TypeName.Parse("System.Int32[]");
        if (!parsed.IsArray || parsed.GetArrayRank() != 1) return 2;
        using (var container = new ContainerConfiguration().WithParts(typeof(Service), typeof(Client)).CreateContainer())
        {
            var client = container.GetExport<Client>();
            if (client.Service.Text != "source9 λ" || !ReferenceEquals(client.Service, container.GetExport<IService>())) return 3;
        }
        var pipe = new Pipe();
        await pipe.Writer.WriteAsync(Encoding.UTF8.GetBytes("pipe9 λ"));
        var read = await pipe.Reader.ReadAsync();
        if (Encoding.UTF8.GetString(read.Buffer.ToArray()) != "pipe9 λ") return 4;
        pipe.Reader.AdvanceTo(read.Buffer.End);
        await pipe.Writer.CompleteAsync();
        await pipe.Reader.CompleteAsync();
        string output = Path.GetFullPath(AppContext.BaseDirectory);
        string[] names = { "System.Collections.Immutable", "System.Reflection.Metadata", "System.Composition.AttributedModel",
            "System.Composition.Runtime", "System.Composition.Hosting", "System.Composition.Convention", "System.Composition.TypedParts", "System.IO.Pipelines" };
        foreach (string name in names)
        {
            var assembly = Assembly.Load(name);
            if (assembly.GetName().Version != new Version(9, 0, 0, 0) || !Path.GetFullPath(assembly.Location).StartsWith(output, StringComparison.OrdinalIgnoreCase)) return 5;
            Console.WriteLine("SOURCE9 " + assembly.FullName + " " + assembly.Location);
        }
        using var document = JsonDocument.Parse(File.ReadAllText(args[0]));
        int resourceSets = 0, resourceValues = 0;
        foreach (var item in document.RootElement.EnumerateArray())
        {
            var assembly = Assembly.Load(item.GetProperty("assembly").GetString()!);
            string logical = item.GetProperty("logical_name").GetString()!;
            string culture = item.GetProperty("culture").GetString()!;
            var manager = new ResourceManager(logical, assembly);
            using ResourceSet set = manager.GetResourceSet(CultureInfo.GetCultureInfo(culture), true, false)!;
            if (set == null) throw new Exception("Missing original resource set: " + logical + "/" + culture);
            var expected = item.GetProperty("strings").EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetString());
            var actual = new Dictionary<string, string?>();
            foreach (DictionaryEntry pair in set) actual.Add((string)pair.Key, (string?)pair.Value);
            if (actual.Count != expected.Count || expected.Any(p => !actual.TryGetValue(p.Key, out string? value) || value != p.Value))
                throw new Exception("Original resource values differ: " + logical + "/" + culture);
            resourceSets++;resourceValues += expected.Count;
        }
        Console.WriteLine("PASS original source9 immutable collections, new metadata parser, MEF shared exports/imports, async Unicode pipe and " + resourceSets + " resource sets/" + resourceValues + " values");
        return 0;
    }
}
