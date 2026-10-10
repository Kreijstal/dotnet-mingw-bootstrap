// pecompare <a.dll> <b.dll> [<a.pdb> <b.pdb>]: why two deterministic builds of the same assembly differ.
// Roslyn's deterministic build derives the MVID, the COFF time stamp and the PDB id from hashes of the
// content (PE and PDB), so one difference anywhere shows up in all of them.  This blanks exactly those
// derived fields (COFF TimeDateStamp, PE CheckSum, the debug directory's CodeView GUID/age and PDB
// checksum, the module MVID) and compares the rest byte by byte: "NORMALIZED-SAME" means the two images
// differ only through their PDBs.  With the PDBs it then lists the portable-PDB compilation options and
// metadata references that differ (compiler-version, runtime-version, define ...).
// m612 S12 diagnostics.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;

static class PeCompare
{
    static byte[] Normalize(string path, out string mvid)
    {
        var bytes = File.ReadAllBytes(path);
        using var pe = new PEReader(new MemoryStream(bytes));
        var h = pe.PEHeaders;
        void Zero(int off, int len) { for (int i = 0; i < len; i++) bytes[off + i] = 0; }
        Zero(h.CoffHeaderStartOffset + 4, 4);                       // TimeDateStamp
        Zero(h.PEHeaderStartOffset + 64, 4);                        // CheckSum
        foreach (var e in pe.ReadDebugDirectory())
        {
            // the directory entry's own stamp, and the data of the CodeView / PDB checksum entries
            int entryOff = DebugEntryOffset(pe, bytes, e);
            if (entryOff >= 0) Zero(entryOff + 4, 4);
            if (e.Type == DebugDirectoryEntryType.CodeView || e.Type == DebugDirectoryEntryType.PdbChecksum)
                Zero(e.DataPointer, e.DataSize);
        }
        var md = pe.GetMetadataReader();
        var g = md.GetModuleDefinition().Mvid;
        mvid = md.GetGuid(g).ToString();
        // the #GUID heap entry of the MVID (16 bytes at index g-1)
        int heapOff = h.MetadataStartOffset + md.GetHeapMetadataOffset(HeapIndex.Guid) + (MetadataTokens.GetHeapOffset(g) - 1) * 16;
        Zero(heapOff, 16);
        return bytes;
    }

    static int DebugEntryOffset(PEReader pe, byte[] bytes, DebugDirectoryEntry e)
    {
        var dir = pe.PEHeaders.PEHeader!.DebugTableDirectory;
        if (dir.Size == 0) return -1;
        if (!pe.PEHeaders.TryGetDirectoryOffset(dir, out int start)) return -1;
        for (int off = start; off + 28 <= start + dir.Size; off += 28)
            if (BitConverter.ToInt32(bytes, off + 24) == e.DataPointer) return off;
        return -1;
    }

    static Dictionary<string, string> PdbInfo(string pdb)
    {
        var r = new Dictionary<string, string>();
        using var prov = MetadataReaderProvider.FromPortablePdbStream(File.OpenRead(pdb));
        var md = prov.GetMetadataReader();
        var opts = new Guid("B5FEEC05-8CD0-4A83-96DA-466284BB4BD8");
        var refs = new Guid("7E4D4708-096E-4C5C-AEDA-CB10BA6A740D");
        foreach (var h in md.GetCustomDebugInformation(EntityHandle.ModuleDefinition))
        {
            var cdi = md.GetCustomDebugInformation(h);
            var kind = md.GetGuid(cdi.Kind);
            var blob = md.GetBlobBytes(cdi.Value);
            if (kind == opts)
            {
                var parts = Encoding.UTF8.GetString(blob).Split('\0');
                for (int i = 0; i + 1 < parts.Length; i += 2) r["option " + parts[i]] = parts[i + 1];
            }
            else if (kind == refs)
            {
                // name\0 aliases\0 flags(1) timestamp(4) imagesize(4) mvid(16)
                int p = 0, n = 0;
                while (p < blob.Length)
                {
                    int z = Array.IndexOf(blob, (byte)0, p); var name = Encoding.UTF8.GetString(blob, p, z - p); p = z + 1;
                    z = Array.IndexOf(blob, (byte)0, p); p = z + 1;
                    p += 1; var ts = BitConverter.ToUInt32(blob, p); p += 4; p += 4;
                    var mv = new Guid(blob.AsSpan(p, 16)); p += 16;
                    r[$"reference {name}"] = $"{ts:x8} {mv}"; n++;
                }
            }
        }
        r["documents"] = md.Documents.Count.ToString();
        foreach (var dh in md.Documents)
        {
            var d = md.GetDocument(dh);
            r["document " + md.GetString(d.Name)] = BitConverter.ToString(md.GetBlobBytes(d.Hash));
        }
        return r;
    }

    static int Main(string[] a)
    {
        var x = Normalize(a[0], out var mx);
        var y = Normalize(a[1], out var my);
        int first = -1;
        for (int i = 0; i < Math.Min(x.Length, y.Length); i++) if (x[i] != y[i]) { first = i; break; }
        if (first < 0 && x.Length != y.Length) first = Math.Min(x.Length, y.Length);
        Console.WriteLine(first < 0 ? $"NORMALIZED-SAME {Path.GetFileName(a[0])} (mvid {mx} / {my})"
                                    : $"NORMALIZED-DIFF {Path.GetFileName(a[0])} at 0x{first:x} (sizes {x.Length}/{y.Length})");
        if (a.Length >= 4 && File.Exists(a[2]) && File.Exists(a[3]))
        {
            var p = PdbInfo(a[2]); var q = PdbInfo(a[3]);
            foreach (var k in p.Keys.Union(q.Keys).OrderBy(k => k))
            {
                p.TryGetValue(k, out var u); q.TryGetValue(k, out var v);
                if (u != v) Console.WriteLine($"  pdb {k}: '{u}' -> '{v}'");
            }
        }
        return 0;
    }
}
