// fcallrva <assembly>: clears the RVA of the methods marked InternalCall that have an IL body.
//
// Roslyn gives an auto-property accessor marked [MethodImpl(MethodImplOptions.InternalCall)]
// (e.g. Thread.OptimalMaxSpinWaitsPerSpinIteration in CoreCLR's CoreLib) a body that reads the
// backing field. Upstream CoreLib goes through ILLink (eng/illink.targets, ILLinkTrimAssembly),
// whose Cecil writer drops the bodies of InternalCall methods, so the VM sees RVA 0 and binds
// the FCall (methodtablebuilder.cpp: InternalCall with RVA != 0 is taken for a P/Invoke).
// Without the trimming step, this does the same in place: the MethodDef rows' RVA column is
// set to 0; nothing else in the file changes.
using System;
using System.IO;
using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;

static class FCallRva
{
    static int Main(string[] args)
    {
        string path = args[0];
        byte[] image = File.ReadAllBytes(path);
        int fixedCount = 0;
        using (var pe = new PEReader(new MemoryStream(image)))
        {
            MetadataReader md = pe.GetMetadataReader();
            int metadataStart = pe.PEHeaders.MetadataStartOffset;
            int tableOffset = md.GetTableMetadataOffset(TableIndex.MethodDef);
            int rowSize = md.GetTableRowSize(TableIndex.MethodDef);
            foreach (MethodDefinitionHandle h in md.MethodDefinitions)
            {
                MethodDefinition m = md.GetMethodDefinition(h);
                if ((m.ImplAttributes & MethodImplAttributes.InternalCall) == 0 || m.RelativeVirtualAddress == 0)
                    continue;
                int row = MetadataTokens.GetRowNumber(h);
                int at = metadataStart + tableOffset + (row - 1) * rowSize;   // RVA is the first column
                if (BitConverter.ToInt32(image, at) != m.RelativeVirtualAddress)
                    throw new InvalidOperationException("MethodDef row layout mismatch");
                image[at] = image[at + 1] = image[at + 2] = image[at + 3] = 0;
                TypeDefinition t = md.GetTypeDefinition(m.GetDeclaringType());
                Console.WriteLine("  RVA 0: {0}.{1}::{2}", md.GetString(t.Namespace), md.GetString(t.Name), md.GetString(m.Name));
                fixedCount++;
            }
        }
        File.WriteAllBytes(path, image);
        Console.WriteLine("fcallrva: {0} InternalCall method(s) without a body now", fixedCount);
        return 0;
    }
}
