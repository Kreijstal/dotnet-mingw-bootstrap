// rvaalign <dir|assembly>...: lists RVA static fields whose data is not aligned as their type
// requires. Roslyn gives the data blobs of RuntimeHelpers.CreateSpan<T> (ReadOnlySpan<T> of
// constants, T wider than a byte) a type named "__StaticArrayInitTypeSize=N_Align=A" and relies
// on System.Reflection.Metadata to align them; the SRM 5.0 of the Mono 6.12 seed only aligns the
// mapped field data to 4, and CoreCLR (ArrayNative::GetSpanDataFrom) rejects misaligned data
// with ArgumentException. Exit code 1 if anything is misaligned.
using System;
using System.IO;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;

static class RvaAlign
{
    static int Main(string[] args)
    {
        int bad = 0, files = 0;
        foreach (string a in args)
        {
            var list = Directory.Exists(a) ? Directory.GetFiles(a, "*.dll").OrderBy(x => x) : new[] { a }.AsEnumerable();
            foreach (string f in list)
            {
                using var fs = File.OpenRead(f);
                using var pe = new PEReader(fs);
                if (!pe.HasMetadata) continue;
                files++;
                MetadataReader md = pe.GetMetadataReader();
                foreach (FieldDefinitionHandle fh in md.FieldDefinitions)
                {
                    FieldDefinition fd = md.GetFieldDefinition(fh);
                    int rva = fd.GetRelativeVirtualAddress();
                    if (rva == 0) continue;
                    // the field's type: a TypeDef named __StaticArrayInitTypeSize=N_Align=A
                    BlobReader sig = md.GetBlobReader(fd.Signature);
                    if (sig.ReadSignatureHeader().Kind != SignatureKind.Field) continue;
                    SignatureTypeCode code = sig.ReadSignatureTypeCode();
                    if (code != SignatureTypeCode.TypeHandle) continue;
                    EntityHandle th = sig.ReadTypeHandle();
                    if (th.Kind != HandleKind.TypeDefinition) continue;
                    string name = md.GetString(md.GetTypeDefinition((TypeDefinitionHandle)th).Name);
                    int i = name.IndexOf("_Align=", StringComparison.Ordinal);
                    if (i < 0) continue;
                    int align = int.Parse(name.Substring(i + 7));
                    if (rva % align != 0)
                    {
                        bad++;
                        Console.WriteLine("{0}: {1} at RVA 0x{2:x} (needs {3})", Path.GetFileName(f), md.GetString(fd.Name), rva, align);
                    }
                }
            }
        }
        Console.WriteLine("rvaalign: {0} assemblies, {1} misaligned CreateSpan blobs", files, bad);
        return bad == 0 ? 0 : 1;
    }
}
