#nullable enable
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Resources;
using System.Security.Cryptography;
using System.Text.Json;
using System.Xml.Linq;

internal static class Program
{
    public static int Main(string[] args)
    {
        using var pe = new PEReader(File.OpenRead(args[0]));
        MetadataReader md = pe.GetMetadataReader();
        AssemblyDefinition assembly = md.GetAssemblyDefinition();
        string name = md.GetString(assembly.Name);
        byte[] key = md.GetBlobBytes(assembly.PublicKey);
        string token = Convert.ToHexString(SHA1.HashData(key).TakeLast(8).Reverse().ToArray()).ToLowerInvariant();
        if (name != "System.Private.CoreLib" || assembly.Version != new Version(10, 0, 0, 0) || token != "7cec85d7bea7798e") return 1;
        if (md.AssemblyReferences.Count != 0) return 2;
        string[] resourceNames = md.ManifestResources.Select(h => md.GetString(md.GetManifestResource(h).Name)).ToArray();
        foreach (string expected in new[] { "System.Private.CoreLib.Strings.resources", "ILLink.Descriptors.xml", "ILLink.Substitutions.xml", "ILLink.LinkAttributes.xml" })
            if (!resourceNames.Contains(expected)) return 3;
        ManifestResource resource = md.ManifestResources.Select(md.GetManifestResource).Single(r => md.GetString(r.Name) == "System.Private.CoreLib.Strings.resources");
        byte[] section = pe.GetSectionData(pe.PEHeaders.CorHeader!.ResourcesDirectory.RelativeVirtualAddress).GetContent().ToArray();
        int offset = checked((int)resource.Offset);
        int length = BitConverter.ToInt32(section, offset);
        using var reader = new ResourceReader(new MemoryStream(section, offset + 4, length));
        var values = new Dictionary<string, string>();
        foreach (DictionaryEntry entry in reader) values.Add((string)entry.Key, (string)entry.Value!);
        var originals = XDocument.Load(args[1]).Root!.Elements("data").ToDictionary(e => (string)e.Attribute("name")!, e => e.Element("value")!.Value);
        if (values.Count != originals.Count || originals.Any(pair => !values.TryGetValue(pair.Key, out string? value) || value != pair.Value)) return 4;
        int internalCalls = 0, withBody = 0;
        foreach (MethodDefinitionHandle handle in md.MethodDefinitions)
        {
            MethodDefinition method = md.GetMethodDefinition(handle);
            if ((method.ImplAttributes & MethodImplAttributes.InternalCall) == 0) continue;
            internalCalls++;
            if (method.RelativeVirtualAddress != 0) withBody++;
        }
        Console.WriteLine(JsonSerializer.Serialize(new {
            name, version = assembly.Version.ToString(), public_key_token = token,
            assembly_references = md.AssemblyReferences.Count, types = md.TypeDefinitions.Count,
            methods = md.MethodDefinitions.Count, resources = resourceNames,
            resource_strings = values.Count, all_original_resource_values_match = true,
            internal_calls = internalCalls, internal_calls_with_body = withBody
        }, new JsonSerializerOptions { WriteIndented = true }));
        return 0;
    }
}
