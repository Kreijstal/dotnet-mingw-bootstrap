// Metadata inspection only: never load or execute the target assembly.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;

using var pe=new PEReader(File.OpenRead(args[0]));
var reader=pe.GetMetadataReader();var assembly=reader.GetAssemblyDefinition();
var attributes=new Dictionary<string,string>();
foreach(var handle in assembly.GetCustomAttributes())
{
    var attribute=reader.GetCustomAttribute(handle);
    if(attribute.Constructor.Kind!=HandleKind.MemberReference)continue;
    var constructor=reader.GetMemberReference((MemberReferenceHandle)attribute.Constructor);
    if(constructor.Parent.Kind!=HandleKind.TypeReference)continue;
    var type=reader.GetTypeReference((TypeReferenceHandle)constructor.Parent);
    string name=reader.GetString(type.Name);
    if(name is not ("AssemblyFileVersionAttribute" or "AssemblyInformationalVersionAttribute"))continue;
    var blob=reader.GetBlobReader(attribute.Value);
    if(blob.ReadUInt16()!=1)throw new Exception("attribute prolog");
    attributes[name]=blob.ReadSerializedString();
}
var key=reader.GetBlobBytes(assembly.PublicKey);
Console.WriteLine(JsonSerializer.Serialize(new{
    name=reader.GetString(assembly.Name),version=assembly.Version.ToString(),
    public_key_token=Convert.ToHexString(SHA1.HashData(key).TakeLast(8).Reverse().ToArray()).ToLowerInvariant(),attributes
},new JsonSerializerOptions{WriteIndented=true}));
