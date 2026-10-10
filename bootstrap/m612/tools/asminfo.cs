// asminfo <assembly...>: identity, MVID and assembly-level custom attributes of managed assemblies
// (System.Reflection.Metadata only), one block per file.  m612 S12 diagnostics.
using System;
using System.IO;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;

static class AsmInfo
{
    static string TypeName(MetadataReader md, EntityHandle ctor)
    {
        EntityHandle t;
        if (ctor.Kind == HandleKind.MemberReference) t = md.GetMemberReference((MemberReferenceHandle)ctor).Parent;
        else t = md.GetMethodDefinition((MethodDefinitionHandle)ctor).GetDeclaringType();
        if (t.Kind == HandleKind.TypeReference) { var r = md.GetTypeReference((TypeReferenceHandle)t); return md.GetString(r.Namespace) + "." + md.GetString(r.Name); }
        if (t.Kind == HandleKind.TypeDefinition) { var d = md.GetTypeDefinition((TypeDefinitionHandle)t); return md.GetString(d.Namespace) + "." + md.GetString(d.Name); }
        return t.Kind.ToString();
    }
    static int Main(string[] args)
    {
        foreach (var f in args)
        {
            using var s = File.OpenRead(f);
            using var pe = new PEReader(s);
            if (!pe.HasMetadata) { Console.WriteLine($"{f}: not managed"); continue; }
            var md = pe.GetMetadataReader();
            var a = md.GetAssemblyDefinition();
            Console.WriteLine($"{f}: {md.GetString(a.Name)} {a.Version} mvid={md.GetGuid(md.GetModuleDefinition().Mvid)} corflags={pe.PEHeaders.CorHeader!.Flags} timestamp=0x{pe.PEHeaders.CoffHeader.TimeDateStamp:x8}");
            foreach (var h in a.GetCustomAttributes())
            {
                var ca = md.GetCustomAttribute(h);
                var blob = md.GetBlobBytes(ca.Value);
                var txt = System.Text.Encoding.UTF8.GetString(blob, 2, Math.Max(0, Math.Min(blob.Length - 2, 80))).Replace('\0', ' ');
                Console.WriteLine($"  [{TypeName(md, ca.Constructor)}] {txt}");
            }
            foreach (var r in md.AssemblyReferences)
            {
                var ar = md.GetAssemblyReference(r);
                Console.WriteLine($"  ref {md.GetString(ar.Name)} {ar.Version}");
            }
        }
        return 0;
    }
}
