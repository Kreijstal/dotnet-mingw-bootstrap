using System;
using System.Runtime.InteropServices;

foreach (string value in new[] { "café 東京 😀", "" })
{
    string text = new string(value.ToCharArray());
    ref char first = ref WpfStringChars.GetPtrToStringChars(text);
    ReadOnlySpan<char> characters = MemoryMarshal.CreateReadOnlySpan(ref first, text.Length);
    for (int i = 0; i < 5; ++i)
    {
        for (int j = 0; j < 256; ++j) GC.KeepAlive(new byte[4096]);
        GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
        if (!characters.SequenceEqual(value.AsSpan()))
            throw new Exception("WPF string interior pointer lost its UTF-16 storage");
    }
}
var method = typeof(WpfStringChars).GetMethod("GetPtrToStringChars");
if (method.ReturnType != typeof(char).MakeByRefType())
    throw new Exception("WPF helper did not emit a CLR char byref return");
Console.WriteLine("WPF string interior-pointer return and moving GC OK");
