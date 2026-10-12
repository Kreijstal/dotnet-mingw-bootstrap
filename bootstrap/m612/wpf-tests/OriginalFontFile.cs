using System;
using System.IO;
using System.Linq;
using System.Reflection;
static class Check {
    static void Main() {
        for(int i=0;i<32;++i) {
            string path=OriginalFontFileCheck.Verify();
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            if(!File.Exists(path)) throw new Exception("original native font path did not survive buffer cleanup/GC or points to a missing file");
        }
        var assembly=typeof(OriginalFontFileCheck).Assembly;
        var type=assembly.GetType("MS.Internal.Text.TextInterface.FontFile",true);
        if(!typeof(IDisposable).IsAssignableFrom(type) || type.GetProperty("DWriteFontFileNoAddRef",BindingFlags.NonPublic|BindingFlags.Instance)==null)
            throw new Exception("original FontFile metadata missing");
        var analyze=type.GetMethod("Analyze",BindingFlags.NonPublic|BindingFlags.Instance);
        if(analyze==null || analyze.GetParameters().Take(3).Any(p=>!p.IsOut)) throw new Exception("original Analyze Out metadata missing");
        Console.WriteLine("complete original WPF FontFile.cpp/header, static GUID, Analyze, native Unicode path, metadata, cleanup and GC OK");
    }
}
