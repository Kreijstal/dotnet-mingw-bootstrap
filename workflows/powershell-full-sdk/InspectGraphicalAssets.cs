// Read embedded WPF assets as raw resource data, without loading the assembly.
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Resources;
using System.Security.Cryptography;
using System.Text.Json;

using var pe = new PEReader(File.OpenRead(args[0]));
var metadata = pe.GetMetadataReader();
var assets = new SortedDictionary<string, object>();
foreach (var handle in metadata.ManifestResources)
{
    var resource = metadata.GetManifestResource(handle);
    if (!resource.Implementation.IsNil || !metadata.GetString(resource.Name).EndsWith(".g.resources", StringComparison.Ordinal)) continue;
    var section = pe.GetSectionData(pe.PEHeaders.CorHeader.ResourcesDirectory.RelativeVirtualAddress);
    int offset = checked((int)resource.Offset);
    int size = BinaryPrimitives.ReadInt32LittleEndian(section.GetContent(offset, 4).AsSpan());
    using var stream = new MemoryStream(section.GetContent(offset + 4, size).ToArray());
    using var reader = new ResourceReader(stream);
    var enumerator = reader.GetEnumerator();
    while (enumerator.MoveNext())
    {
        string name = (string)enumerator.Key;
        reader.GetResourceData(name, out string type, out byte[] raw);
        if (type != "ResourceTypeCode.Stream") throw new Exception("Unexpected WPF resource type: " + type);
        int length = BinaryPrimitives.ReadInt32LittleEndian(raw);
        if (length != raw.Length - 4) throw new Exception("Invalid resource stream length");
        assets.Add(name, new { length, sha256 = Convert.ToHexString(SHA256.HashData(raw.AsSpan(4))).ToLowerInvariant() });
    }
}
Console.WriteLine(JsonSerializer.Serialize(assets, new JsonSerializerOptions { WriteIndented = true }));
