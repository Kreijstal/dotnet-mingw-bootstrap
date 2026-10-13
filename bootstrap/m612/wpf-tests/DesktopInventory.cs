using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Linq;
using System.Text.Json;
class Inventory {
    static void Main(string[] args) {
        var assemblies=new Dictionary<string,object>(StringComparer.OrdinalIgnoreCase);
        foreach(var directory in args[1..]) foreach(var file in Directory.GetFiles(directory,"*.dll")) {
            try {
                var identity=AssemblyName.GetAssemblyName(file);
                using var input=File.OpenRead(file);
                using var pe=new PEReader(input);
                var reader=pe.GetMetadataReader();
                var required=new HashSet<AssemblyReferenceHandle>();
                foreach(var handle in reader.TypeReferences) {
                    var scope=reader.GetTypeReference(handle).ResolutionScope;
                    if(scope.Kind==HandleKind.AssemblyReference) required.Add((AssemblyReferenceHandle)scope);
                }
                foreach(var handle in reader.ExportedTypes) {
                    var scope=reader.GetExportedType(handle).Implementation;
                    if(scope.Kind==HandleKind.AssemblyReference) required.Add((AssemblyReferenceHandle)scope);
                }
                var references=required.Select(handle=>reader.GetString(reader.GetAssemblyReference(handle).Name)).OrderBy(name=>name).ToArray();
                assemblies[identity.Name]=new {Name=identity.Name,Token=Convert.ToHexString(identity.GetPublicKeyToken()).ToLowerInvariant(),Version=identity.Version.ToString(),Path=file,References=references};
            } catch(BadImageFormatException) { }
        }
        File.WriteAllText(args[0],JsonSerializer.Serialize(new {assemblies=assemblies.Values},new JsonSerializerOptions{WriteIndented=true}));
    }
}
