using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;

internal static class Program
{
    static string AttributeName(MetadataReader md, CustomAttribute attribute)
    {
        EntityHandle type = attribute.Constructor.Kind == HandleKind.MemberReference
            ? md.GetMemberReference((MemberReferenceHandle)attribute.Constructor).Parent
            : md.GetMethodDefinition((MethodDefinitionHandle)attribute.Constructor).GetDeclaringType();
        if (type.Kind == HandleKind.TypeReference)
        {
            var t = md.GetTypeReference((TypeReferenceHandle)type);
            return md.GetString(t.Namespace) + "." + md.GetString(t.Name);
        }
        var definition = md.GetTypeDefinition((TypeDefinitionHandle)type);
        return md.GetString(definition.Namespace) + "." + md.GetString(definition.Name);
    }
    public static int Main(string[] args)
    {
        string root = Path.GetFullPath(args[0]);
        using var status = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "build-status.json")));
        using var evaluated = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "evaluated.json")));
        if (!status.RootElement.GetProperty("complete_declared_reference_graph").GetBoolean()) throw new Exception("Incomplete graph");
        var results = new Dictionary<string, object>();
        var known = new Dictionary<string, (Version version, byte[] token)>(StringComparer.OrdinalIgnoreCase);
        var allReferences = new List<(string owner, string target, Version version, byte[] token)>();
        int publicAssemblies = 0, stubAssemblies = 0, zeroVersionAssemblies = 0;
        foreach (var project in status.RootElement.GetProperty("projects").EnumerateObject())
        {
            string name = project.Name.Replace("stub:", "");
            bool stub = project.Value.GetProperty("kind").GetString() == "stub";
            string path = Path.Combine(root, stub ? "stubs" : "ref/net10.0", name + ".dll");
            using var pe = new PEReader(File.OpenRead(path));
            MetadataReader md = pe.GetMetadataReader();
            var assembly = md.GetAssemblyDefinition();
            string actualName = md.GetString(assembly.Name);
            string version = assembly.Version.ToString();
            if (actualName != name || version != project.Value.GetProperty("assembly_version").GetString()) throw new Exception("Identity mismatch: " + name);
            string keyId = evaluated.RootElement.GetProperty(project.Name).GetProperty("properties").GetProperty("StrongNameKeyId").GetString()!;
            byte[] expectedKey = File.ReadAllBytes(Path.Combine(root, "keys", keyId + ".pubkey"));
            byte[] key = md.GetBlobBytes(assembly.PublicKey);
            if (!key.SequenceEqual(expectedKey)) throw new Exception("Public key mismatch: " + name);
            byte[] token = SHA1.HashData(key).TakeLast(8).Reverse().ToArray();
            known.Add(name, (assembly.Version, token));
            bool marker = assembly.GetCustomAttributes().Any(h => AttributeName(md, md.GetCustomAttribute(h)) == "System.Runtime.CompilerServices.ReferenceAssemblyAttribute");
            if (!marker) throw new Exception("Missing original reference-assembly marker: " + name);
            string fileVersion = FileVersionInfo.GetVersionInfo(path).FileVersion!;
            if (fileVersion != project.Value.GetProperty("file_version").GetString()) throw new Exception("File version mismatch: " + name);
            bool zero = project.Value.GetProperty("zero_version_policy").GetBoolean();
            var references = md.AssemblyReferences.Select(h => {
                var reference = md.GetAssemblyReference(h);
                string target = md.GetString(reference.Name);
                var bytes = md.GetBlobBytes(reference.PublicKeyOrToken);
                allReferences.Add((name, target, reference.Version, bytes));
                if (zero && reference.Version != new Version(0, 0, 0, 0)) throw new Exception("Original zero-version policy missing: " + name);
                return new { name = target, version = reference.Version.ToString(), token = Convert.ToHexString(bytes).ToLowerInvariant() };
            }).ToArray();
            if (zero) zeroVersionAssemblies++;
            if (stub) stubAssemblies++; else publicAssemblies++;
            results.Add(project.Name, new {
                name, version, file_version = fileVersion, public_key_token = Convert.ToHexString(token).ToLowerInvariant(),
                reference_assembly = marker, types = md.TypeDefinitions.Count, methods = md.MethodDefinitions.Count,
                exported_types = md.ExportedTypes.Count, original_zero_version_policy = zero, references,
                sha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant()
            });
        }
        foreach (var reference in allReferences)
        {
            if (!known.TryGetValue(reference.target, out var target)) throw new Exception("Unresolved declared reference: " + reference.owner + " -> " + reference.target);
            if (reference.version != new Version(0, 0, 0, 0) && reference.version != target.version) throw new Exception("Reference version mismatch: " + reference.owner + " -> " + reference.target);
            if (!reference.token.SequenceEqual(target.token)) throw new Exception("Reference key mismatch: " + reference.owner + " -> " + reference.target);
        }
        if (publicAssemblies != 167 || stubAssemblies != 19) throw new Exception("Declared graph count mismatch");
        Console.WriteLine(JsonSerializer.Serialize(new {
            public_reference_assemblies = publicAssemblies, private_facade_stub_assemblies = stubAssemblies,
            original_zero_version_policy_assemblies = zeroVersionAssemblies,
            all_identities_keys_versions_markers_and_graph_references_match = true,
            full_sdk10_built = false, full_dependency_goal_complete = false, assemblies = results
        }, new JsonSerializerOptions { WriteIndented = true }));
        return 0;
    }
}
