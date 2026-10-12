using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

unsafe static class Check {
    [DllImport("wpf-subsetter.dll", ExactSpelling=true)] static extern short WpfCreateSubset(byte* source, uint size, out nint output, out uint capacity, out uint written, ushort* glyphs, ushort count, uint offset);
    [DllImport("wpf-subsetter.dll", ExactSpelling=true)] static extern void WpfFreeSubset(nint memory);
    [DllImport("gdi32.dll", ExactSpelling=true)] static extern nint AddFontMemResourceEx(byte* memory, uint size, nint reserved, out uint fonts);
    [DllImport("gdi32.dll", ExactSpelling=true)] static extern bool RemoveFontMemResourceEx(nint handle);
    static ushort Word(byte[] bytes, int at) => (ushort)((bytes[at] << 8) | bytes[at+1]);
    static uint Long(byte[] bytes, int at) => ((uint)Word(bytes, at) << 16) | Word(bytes, at+2);
    static Dictionary<string, (int Offset,int Length)> Tables(byte[] font) {
        if (Long(font, 0) != 0x00010000) throw new Exception("expected a TrueType sfnt");
        var tables = new Dictionary<string, (int,int)>();
        for (int i=0; i<Word(font,4); ++i) {
            int row=12+16*i;
            string tag=System.Text.Encoding.ASCII.GetString(font,row,4);
            int offset=checked((int)Long(font,row+8)), length=checked((int)Long(font,row+12));
            if (offset<0 || offset>font.Length-length || (offset&3)!=0) throw new Exception("invalid table: "+tag);
            tables.Add(tag,(offset,length));
        }
        return tables;
    }
    static byte[] Glyph(byte[] font, Dictionary<string,(int Offset,int Length)> tables, ushort glyph) {
        int loca=tables["loca"].Offset, glyf=tables["glyf"].Offset;
        bool wide=Word(font,tables["head"].Offset+50)!=0;
        int start=wide?checked((int)Long(font,loca+4*glyph)):2*Word(font,loca+2*glyph);
        int end=wide?checked((int)Long(font,loca+4*(glyph+1))):2*Word(font,loca+2*(glyph+1));
        return font.AsSpan(glyf+start,end-start).ToArray();
    }
    static byte[] Native(byte[] font, ushort[] glyphs) {
        fixed (byte* source=font) fixed (ushort* selected=glyphs) {
            short error=WpfCreateSubset(source,(uint)font.Length,out nint output,out uint capacity,out uint written,selected,(ushort)glyphs.Length,0);
            try {
                if (error!=0 || output==0 || written==0 || written>capacity) throw new Exception("native subset failed: "+error);
                byte[] result=new byte[written]; Marshal.Copy(output,result,0,result.Length); return result;
            } finally { WpfFreeSubset(output); }
        }
    }
    static void Main() {
        string path=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows),"Fonts","arial.ttf");
        byte[] original=File.ReadAllBytes(path);
        ushort[] glyphs={0,36,37,171}; // Includes outlines and a composite glyph in Arial.
        // Concurrent first use checks native once-only control-table initialization.
        byte[][] concurrent=new byte[8][];
        Parallel.For(0,concurrent.Length,i=>concurrent[i]=Native(original,glyphs));
        byte[] subset=concurrent[0];
        if (concurrent.Any(bytes=>!bytes.SequenceEqual(subset))) throw new Exception("concurrent subset mismatch");
        var assembly=Assembly.LoadFrom(Path.Combine(AppContext.BaseDirectory,"Subsetter.dll"));
        var method=assembly.GetType("MS.Internal.TrueTypeSubsetter",true).GetMethod("ComputeSubset",BindingFlags.Static|BindingFlags.NonPublic);
        byte[] Managed(byte[] input, ushort[] selected) {
            fixed (byte* pointer=input) return (byte[])method.Invoke(null,new object[]{Pointer.Box(pointer,typeof(void*)),input.Length,new Uri(path),0,selected});
        }
        byte[] managed=Managed(original,glyphs);
        if (!managed.SequenceEqual(subset)) throw new Exception("original managed ComputeSubset did not match the native algorithm");
        if (subset.Length>=original.Length) throw new Exception("subsetting did not reduce the font");
        var before=Tables(original); var after=Tables(subset);
        foreach(ushort glyph in glyphs) if(!Glyph(original,before,glyph).SequenceEqual(Glyph(subset,after,glyph)))
            throw new Exception("requested glyph outline changed: "+glyph);
        uint checksum=0;
        for(int i=0;i<subset.Length;i+=4) {
            uint value=0; for(int j=0;j<4;++j) value=(value<<8)|(i+j<subset.Length?subset[i+j]:0u);
            checksum=unchecked(checksum+value);
        }
        if(checksum!=0xB1B0AFBA) throw new Exception("font checksum mismatch");
        fixed(byte* pointer=subset) {
            nint handle=AddFontMemResourceEx(pointer,(uint)subset.Length,0,out uint count);
            if(handle==0||count==0) throw new Exception("Windows rejected the subset font");
            if(!RemoveFontMemResourceEx(handle)) throw new Exception("font resource cleanup failed");
        }
        try { Managed(new byte[32],glyphs); throw new Exception("invalid font accepted"); }
        catch(TargetInvocationException error) {
            if(error.InnerException?.GetType().FullName!="System.IO.FileFormatException") throw;
        }
        GC.Collect(2,GCCollectionMode.Forced,true,true);
        if(!Managed(original,glyphs).SequenceEqual(subset)) throw new Exception("subset failed after compacting GC");
        File.WriteAllBytes(Path.Combine(AppContext.BaseDirectory,"subset.ttf"),subset);
        Console.WriteLine($"upstream TrueType subsetter/native bridge OK: {original.Length} -> {subset.Length} bytes; outlines, checksum, Windows font loading, concurrency and error path");
    }
}
