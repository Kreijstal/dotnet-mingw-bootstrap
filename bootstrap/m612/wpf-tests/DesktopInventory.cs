using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text.Json;
class Inventory {
    static void Main(string[] args) {
        var assemblies=new Dictionary<string,object>(StringComparer.OrdinalIgnoreCase);
        foreach(var directory in args[1..]) foreach(var file in Directory.GetFiles(directory,"*.dll")) {
            try {
                var identity=AssemblyName.GetAssemblyName(file);
                assemblies[identity.Name]=new {Name=identity.Name,Token=Convert.ToHexString(identity.GetPublicKeyToken()).ToLowerInvariant(),Version=identity.Version.ToString(),Path=file};
            } catch(BadImageFormatException) { }
        }
        File.WriteAllText(args[0],JsonSerializer.Serialize(new {assemblies=assemblies.Values},new JsonSerializerOptions{WriteIndented=true}));
    }
}
