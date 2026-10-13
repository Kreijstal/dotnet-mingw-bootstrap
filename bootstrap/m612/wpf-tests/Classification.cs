using System;
using System.Reflection;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Media;

class Check {
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    delegate int Query(out View view);
    [StructLayout(LayoutKind.Sequential)]
    struct View { public IntPtr Classes,Attributes; public int Count; }
    delegate short Scalar(int value);
    delegate short Utf16(char value);
    delegate bool Predicate(int value);
    static void Main() {
        var assembly=typeof(Typeface).Assembly;
        var type=assembly.GetType("MS.Internal.Classification",true);
        var scalar=(Scalar)type.GetMethod("GetUnicodeClass").CreateDelegate(typeof(Scalar));
        var utf16=(Utf16)type.GetMethod("GetUnicodeClassUTF16").CreateDelegate(typeof(Utf16));
        var combining=(Predicate)type.GetMethod("IsCombining").CreateDelegate(typeof(Predicate));
        var joiner=(Predicate)type.GetMethod("IsJoiner").CreateDelegate(typeof(Predicate));
        var library=NativeLibrary.Load(Path.Combine(AppContext.BaseDirectory,"PresentationNative_cor3.dll"));
        var query=Marshal.GetDelegateForFunctionPointer<Query>(NativeLibrary.GetExport(library,"WpfGetClassificationTables"));
        if(query(out var view)!=0 || view.Count<=0) throw new Exception("native classification view failed");
        for(int value=0;value<0x110000;++value) {
            int id=scalar(value);
            if(id<0 || id>=view.Count) throw new Exception("managed class index outside native table");
            if(value<=0xFFFF && utf16((char)value)!=id) throw new Exception("UTF16/scalar table disagreement");
        }
        var attribute=assembly.GetType("MS.Internal.CharacterAttribute",true);
        if(Marshal.SizeOf(attribute)!=8 || Marshal.SizeOf<View>()!=24 ||
           Marshal.OffsetOf(attribute,"Flags").ToInt32()!=2 || Marshal.OffsetOf(attribute,"LineBreak").ToInt32()!=6)
            throw new Exception("managed/native classification ABI mismatch");
        void Flags(int value,int required,int excluded=0) {
            int flags=Marshal.ReadInt16(view.Attributes,scalar(value)*8+2)&0xFFFF;
            if((flags&required)!=required || (flags&excluded)!=0) throw new Exception($"classification flags U+{value:X}: {flags:X}");
        }
        Flags('A',0x810,0x3); Flags(' ',0x90,0x1); Flags('7',0x110,0x1);
        Flags(0x05D0,0x803,0x10); Flags(0x0627,0x803,0x10);
        Flags(0x4E00,0x820,0x11); Flags(0x1F600,0x40);
        Flags('\n',0x404); Flags(0x2029,0x200);
        var script=type.GetMethod("GetScript");
        foreach(var sample in new (int Value,string Script)[] {
            ('A',"Latin"),(0x03B1,"Greek"),(0x0410,"Cyrillic"),(0x05D0,"Hebrew"),
            (0x0627,"Arabic"),(0x0915,"Devanagari"),(0x4E00,"CJKIdeographic"),
            (0x3042,"Kana"),(0xAC00,"Hangul"),(0x10300,"OldItalic"),('7',"Digit")})
            if(script.Invoke(null,new object[]{sample.Value}).ToString()!=sample.Script)
                throw new Exception($"script mapping U+{sample.Value:X} failed");
        if(!combining(0x0301) || !combining(0x1D185) || !combining(0xE0100) || combining('A') ||
           !joiner(0x200C) || !joiner(0x200D) || joiner('A')) throw new Exception("managed mark/joiner/variation classification mismatch");
        GC.Collect(); GC.WaitForPendingFinalizers();
        if(scalar('A')!=utf16('A')) throw new Exception("classification lifetime failed after GC");
        Console.WriteLine($"source Classification: {view.Count} classes; all Unicode scalar/UTF16 indices, scripts/flags, packed ABI, marks/joiners and GC PASS");
    }
}
