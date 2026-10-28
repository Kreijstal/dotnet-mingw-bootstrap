// Inspect published artifacts as metadata only. No reference implementation is
// loaded, executed, compiled against, or copied into a source-built package.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;

internal static class Program
{
    private static object Inspect(byte[] bytes,string package,string path)
    {
        using var pe=new PEReader(new MemoryStream(bytes));
        var reader=pe.GetMetadataReader();
        var assembly=reader.GetAssemblyDefinition();
        string TypeName(TypeDefinitionHandle handle) {
            var type=reader.GetTypeDefinition(handle);
            var parent=type.GetDeclaringType();
            return parent.IsNil?(reader.GetString(type.Namespace)+"."+reader.GetString(type.Name)).TrimStart('.')
                :TypeName(parent)+"+"+reader.GetString(type.Name);
        }
        string ExportedName(ExportedTypeHandle handle) {
            var type=reader.GetExportedType(handle);
            return type.Implementation.Kind==HandleKind.ExportedType
                ?ExportedName((ExportedTypeHandle)type.Implementation)+"+"+reader.GetString(type.Name)
                :(reader.GetString(type.Namespace)+"."+reader.GetString(type.Name)).TrimStart('.');
        }
        string Target(EntityHandle handle) {
            if(handle.Kind==HandleKind.AssemblyReference) return reader.GetString(reader.GetAssemblyReference((AssemblyReferenceHandle)handle).Name);
            if(handle.Kind==HandleKind.ExportedType) return Target(reader.GetExportedType((ExportedTypeHandle)handle).Implementation);
            return handle.Kind.ToString();
        }
        string[] names={"System.ServiceModel.Channels.MtomMessageEncodingBindingElement","System.ServiceModel.ReliableMessagingVersion","System.ServiceModel.WSHttpBindingBase"};
        var types=new List<object>();
        foreach(var handle in reader.TypeDefinitions) {
            var type=reader.GetTypeDefinition(handle);
            string name=TypeName(handle);
            if(!names.Contains(name)) continue;
            var methods=type.GetMethods().Select(h=>reader.GetMethodDefinition(h)).Select(m=>new {
                name=reader.GetString(m.Name),access=(m.Attributes&MethodAttributes.MemberAccessMask).ToString(),
                parameters=m.GetParameters().Count(p=>reader.GetParameter(p).SequenceNumber>0),
                flags=m.Attributes.ToString()
            }).ToArray();
            types.Add(new {name,methods});
        }
        var forwards=reader.ExportedTypes.Select(h=>new {
            type=ExportedName(h),target=Target(reader.GetExportedType(h).Implementation),
            flags=(int)reader.GetExportedType(h).Attributes
        }).ToArray();
        return new {package,path,sha256=Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant(),
            assembly=reader.GetString(assembly.Name),version=assembly.Version.ToString(),
            method_definitions=reader.MethodDefinitions.Count,types,forwards};
    }
    private static int Main(string[] args)
    {
        var records=new List<object>();
        foreach(var package in Directory.GetFiles(args[0],"*.nupkg").Order()) {
            using var zip=ZipFile.OpenRead(package);
            foreach(var entry in zip.Entries.Where(e=>e.FullName.EndsWith(".dll") && !e.FullName.Contains(".resources.") &&
                (e.FullName.StartsWith("ref/net6.0/")||e.FullName.StartsWith("lib/net6.0/")||
                 (Path.GetFileName(package).StartsWith("system.private.servicemodel.")&&e.FullName.StartsWith("lib/netstandard2.0/"))))) {
                using var input=entry.Open();using var bytes=new MemoryStream();input.CopyTo(bytes);
                records.Add(Inspect(bytes.ToArray(),Path.GetFileName(package),entry.FullName));
            }
        }
        File.WriteAllText(args[1],JsonSerializer.Serialize(records,new JsonSerializerOptions {WriteIndented=true}));
        Console.WriteLine("Inspected "+records.Count+" assemblies without loading/executing them");
        return 0;
    }
}
