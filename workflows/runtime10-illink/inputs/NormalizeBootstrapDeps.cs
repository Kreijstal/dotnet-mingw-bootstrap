// SDK8 omits versions for project-reference runtime assets. Populate those
// fields from the actual source-built PE files so the host can compare them
// with framework assets. No assembly bytes or runtime policy are changed.
using System;
using System.Diagnostics;
using System.IO;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Text.Json;
using System.Text.Json.Nodes;

internal static class Program
{
    public static int Main(string[] args)
    {
        string path = Path.GetFullPath(args[0]);
        string folder = Path.GetDirectoryName(path)!;
        string original = File.ReadAllText(path);
        JsonNode root = JsonNode.Parse(original)!;
        var changes = new JsonArray();
        foreach (var target in root["targets"]!.AsObject())
        foreach (var library in target.Value!.AsObject())
        {
            JsonNode? runtime = library.Value!["runtime"];
            if (runtime == null) continue;
            foreach (var asset in runtime.AsObject())
            {
                string file = Path.Combine(folder, asset.Key);
                if (!File.Exists(file) || !file.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)) continue;
                using var pe = new PEReader(File.OpenRead(file));
                if (!pe.HasMetadata) continue;
                string assemblyVersion = pe.GetMetadataReader().GetAssemblyDefinition().Version.ToString();
                string fileVersion = FileVersionInfo.GetVersionInfo(file).FileVersion!;
                if (string.IsNullOrEmpty(fileVersion)) throw new Exception("Missing actual PE file version: " + file);
                changes.Add(new JsonObject {
                    ["library"] = library.Key, ["asset"] = asset.Key,
                    ["old_assembly_version"] = asset.Value!["assemblyVersion"]?.GetValue<string>(),
                    ["old_file_version"] = asset.Value!["fileVersion"]?.GetValue<string>(),
                    ["actual_assembly_version"] = assemblyVersion, ["actual_file_version"] = fileVersion
                });
                asset.Value!["assemblyVersion"] = assemblyVersion;
                asset.Value!["fileVersion"] = fileVersion;
            }
        }
        if (changes.Count == 0) throw new Exception("No runtime asset versions populated");
        File.WriteAllText(path + ".before-version-metadata", original);
        File.WriteAllText(path, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n");
        File.WriteAllText(path + ".version-metadata.json", changes.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n");
        Console.WriteLine("PASS actual PE assembly/file versions populated for {0} dependency entries", changes.Count);
        return 0;
    }
}
