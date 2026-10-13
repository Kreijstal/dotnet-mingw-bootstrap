using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Printing;
using System.Printing.IndexedProperties;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Shapes;
using System.Windows.Xps.Packaging;
using System.Xml.Linq;
static class PrintingRuntime {
    [StructLayout(LayoutKind.Sequential)]
    struct PrinterInfo4 { public IntPtr Name, Server; public uint Attributes; }
    [DllImport("winspool.drv",EntryPoint="EnumPrintersW",CharSet=CharSet.Unicode,SetLastError=true)]
    [return:MarshalAs(UnmanagedType.Bool)]
    static extern bool EnumPrinters(uint flags,string name,uint level,IntPtr buffer,uint bytes,out uint needed,out uint returned);
    static string[] NativeNames() {
        uint needed,count;
        bool success=EnumPrinters(6,null,4,IntPtr.Zero,0,out needed,out count);
        if(needed==0) {
            if(!success) throw new Win32Exception(Marshal.GetLastWin32Error());
            return Array.Empty<string>();
        }
        IntPtr buffer=Marshal.AllocHGlobal(checked((int)needed));
        try {
            if(!EnumPrinters(6,null,4,buffer,needed,out needed,out count)) throw new Win32Exception(Marshal.GetLastWin32Error());
            var result=new List<string>();
            for(uint i=0;i<count;++i) {
                var info=Marshal.PtrToStructure<PrinterInfo4>(IntPtr.Add(buffer,checked((int)i*Marshal.SizeOf<PrinterInfo4>())));
                result.Add(Marshal.PtrToStringUni(info.Name));
            }
            return result.OrderBy(n=>n,StringComparer.OrdinalIgnoreCase).ToArray();
        } finally { Marshal.FreeHGlobal(buffer); }
    }
    public static void Run() {
        using(var text=new PrintStringProperty("Name","東京 λ 🚀"))
        using(var number=new PrintInt32Property("Number",-123456789))
        using(var boolean=new PrintBooleanProperty("Boolean",true))
        using(var data=new PrintByteArrayProperty("Data",new byte[]{0,17,255}))
        using(var dictionary=new PrintPropertyDictionary()) {
            dictionary.Add(text); dictionary.Add(number); dictionary.Add(boolean); dictionary.Add(data);
            GC.Collect();
            if(text.Name!="Name" || (string)dictionary.GetProperty("Name").Value!="東京 λ 🚀" ||
               (int)dictionary.GetProperty("Number").Value!=-123456789 ||
               !(bool)dictionary.GetProperty("Boolean").Value ||
               !((byte[])dictionary.GetProperty("Data").Value).SequenceEqual(new byte[]{0,17,255}))
                throw new Exception("original printing properties/dictionary");
            text.Value="updated λ"; number.Value=int.MaxValue; boolean.Value=false;
            var replacement=new byte[]{42,0,128}; data.Value=replacement;
            GC.Collect();
            if((string)text.Value!="updated λ" || (int)number.Value!=int.MaxValue || (bool)boolean.Value ||
               !ReferenceEquals(data.Value,replacement)) throw new Exception("original printing property updates");
        }
        Console.WriteLine("original printing properties, dictionary, typed values, updates, disposal and GC PASS");
        var expected=NativeNames();
        using(var server=new PrintServer())
        using(var queues=server.GetPrintQueues(new[]{"Name"},new[]{EnumeratedPrintQueueTypes.Local,EnumeratedPrintQueueTypes.Connections})) {
            var actual=new List<string>();
            foreach(var queue in queues) {
                actual.Add(queue.FullName);
                queue.Dispose();
            }
            var names=actual.OrderBy(n=>n,StringComparer.OrdinalIgnoreCase).ToArray();
            if(!names.SequenceEqual(expected,StringComparer.OrdinalIgnoreCase))
                throw new Exception("source PrintServer queues differ from native Winspool: "+string.Join(" | ",names)+" vs "+string.Join(" | ",expected));
            Console.WriteLine($"original PrintServer native enumeration PASS: {names.Length} queues");
        }
        GC.Collect(); GC.WaitForPendingFinalizers();
        string path=System.IO.Path.Combine(AppContext.BaseDirectory,"printing-shapes.xps");
        if(File.Exists(path)) File.Delete(path);
        using(var document=new XpsDocument(path,FileAccess.ReadWrite)) {
            var page=new FixedPage{Width=120,Height=80};
            page.Children.Add(new Rectangle{Width=70,Height=35,Fill=Brushes.Red,Stroke=Brushes.Black,StrokeThickness=2});
            page.Measure(new Size(120,80)); page.Arrange(new Rect(0,0,120,80)); page.UpdateLayout();
            XpsDocument.CreateXpsDocumentWriter(document).Write(page);
        }
        using(var zip=ZipFile.OpenRead(path)) {
            var entry=zip.Entries.Single(e=>e.FullName.EndsWith(".fpage",StringComparison.OrdinalIgnoreCase));
            using var stream=entry.Open();
            var xml=XDocument.Load(stream);
            var shape=xml.Descendants().Single(e=>e.Name.LocalName=="Path");
            if(xml.Root?.Name.LocalName!="FixedPage" || (string)xml.Root.Attribute("Width")!="120" ||
               (string)xml.Root.Attribute("Height")!="80" || (string)shape.Attribute("Fill")!="#FFFF0000" ||
               (string)shape.Attribute("Stroke")!="#FF000000" || (string)shape.Attribute("StrokeThickness")!="2" ||
               (string)shape.Attribute("Data")!="M1,1L69,1 69,34 1,34Z")
                throw new Exception("original XpsDocumentWriter fixed-page output");
        }
        using(var document=new XpsDocument(path,FileAccess.Read)) {
            var sequence=document.GetFixedDocumentSequence();
            if(sequence.DocumentPaginator.PageCount!=1) throw new Exception("XPS page count");
            var page=sequence.DocumentPaginator.GetPage(0);
            if(page.Size!=new Size(120,80)) throw new Exception("XPS readback page geometry");
        }
        Console.WriteLine("original XpsDocumentWriter local vector page, package XML and WPF readback PASS");
        Console.WriteLine("complete printing runtime checks PASS");
    }
}
