using System;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
class Check {
    static void Pump(FrameworkElement element) {
        element.UpdateLayout();
        var frame=new DispatcherFrame();
        Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle,new Action(()=>frame.Continue=false));
        Dispatcher.PushFrame(frame);element.UpdateLayout();
    }
    static byte[] Raster(Visual visual) {
        GC.Collect(2,GCCollectionMode.Forced,true,true);
        var image=new RenderTargetBitmap(400,300,96,96,PixelFormats.Pbgra32);image.Render(visual);
        var pixels=new byte[400*300*4];image.CopyPixels(pixels,400*4,0);
        int ink=0;
        for(int y=20;y<260;++y)for(int x=20;x<350;++x) {
            int at=(y*400+x)*4;
            if(pixels[at+3]>100&&pixels[at]<100&&pixels[at+1]<100&&pixels[at+2]<100)++ink;
        }
        if(ink<500)throw new Exception("bottomless viewport has no rendered text: "+ink);
        Console.WriteLine($"bottomless viewport dark glyph pixels: {ink}");
        return pixels;
    }
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var first=new Paragraph(new Run("First paragraph of the source scrolling document. Its text wraps into several lines."));
            var document=new FlowDocument(first){FontFamily=new FontFamily("Segoe UI"),FontSize=20,PagePadding=new Thickness(10),Foreground=Brushes.Black};
            for(int i=0;i<20;++i)document.Blocks.Add(new Paragraph(new Run($"Paragraph {i}: actual bottomless source WPF layout and scrolling retain text beyond a finite page.")));
            TextOptions.SetTextFormattingMode(document,mode);
            var viewer=new FlowDocumentScrollViewer{Document=document,IsToolBarVisible=false};
            var window=new Window{Content=viewer,Width=420,Height=340,ShowInTaskbar=false};window.Show();Pump(window);
            var scroll=(ScrollViewer)viewer.Template.FindName("PART_ContentHost",viewer);
            if(scroll==null||scroll.ExtentHeight<=scroll.ViewportHeight*3)throw new Exception("bottomless extent is not derived from the full document");
            double height=scroll.ExtentHeight;
            var top=Raster(viewer);scroll.ScrollToEnd();Pump(window);
            if(scroll.VerticalOffset<=scroll.ViewportHeight)throw new Exception("bottomless document did not scroll beyond a finite page");
            var bottom=Raster(viewer);if(top.SequenceEqual(bottom))throw new Exception("scrolling did not change retained text visuals");
            scroll.ScrollToHome();Pump(window);
            first.Inlines.Add(new Run(" Appending original document text exercises bottomless page update and new line wrapping."));Pump(window);
            var changed=Raster(viewer);if(top.SequenceEqual(changed)||scroll.ExtentHeight<=height)throw new Exception("edited bottomless document did not update text raster and extent");
            Console.WriteLine($"original FlowDocumentScrollViewer {mode}: full extent {height:F1}->{scroll.ExtentHeight:F1}, top/end glyph raster, text update and compacting GC PASS");
            window.Close();
            var editDocument=new FlowDocument(new Paragraph(new Run("Original RichTextBox source document, formatted through the source bottomless paragraph engine."))){FontFamily=new FontFamily("Segoe UI"),FontSize=20,PagePadding=new Thickness(10),Foreground=Brushes.Black};
            var editor=new RichTextBox(editDocument){Width=400,Height=300,VerticalScrollBarVisibility=ScrollBarVisibility.Auto};TextOptions.SetTextFormattingMode(editor,mode);
            var editorWindow=new Window{Content=editor,Width=420,Height=340,ShowInTaskbar=false};editorWindow.Show();Pump(editorWindow);
            var before=Raster(editor);editor.AppendText(" Added through the real editor and text container.");Pump(editorWindow);
            var after=Raster(editor);if(before.SequenceEqual(after)||!new TextRange(editDocument.ContentStart,editDocument.ContentEnd).Text.Contains("real editor"))throw new Exception("RichTextBox insertion did not update original text layout");
            Console.WriteLine($"original RichTextBox {mode}: initial and edited glyph raster, text-container insertion and compacting GC PASS");editorWindow.Close();
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();
        Console.WriteLine("original bottomless scrolling/editor documents and dispatcher resource cleanup PASS");
    }
}
