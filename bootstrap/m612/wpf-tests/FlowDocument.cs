using System;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
class Check {
    static void CheckPage(FlowDocument document,int minimumInkRows,string label) {
        DocumentPaginator paginator=((IDocumentPaginatorSource)document).DocumentPaginator;
        paginator.PageSize=new Size(400,300);
        using var page=paginator.GetPage(0);
        if(page==DocumentPage.Missing || page.Visual==null || page.Size!=new Size(400,300))throw new Exception("FlowDocument did not create the requested page");
        GC.Collect(2,GCCollectionMode.Forced,true,true);
        var bitmap=new RenderTargetBitmap(400,300,96,96,PixelFormats.Pbgra32);bitmap.Render(page.Visual);
        var pixels=new byte[400*300*4];bitmap.CopyPixels(pixels,400*4,0);
        int ink=0,inkRows=0;
        for(int y=0;y<300;++y) {
            bool row=false;
            for(int x=0;x<400;++x) {
                int at=(y*400+x)*4;
                if(pixels[at+3]>100 && pixels[at]<100 && pixels[at+1]<100 && pixels[at+2]<100){++ink;row=true;}
            }
            if(row)++inkRows;
        }
        if(ink<200 || inkRows<minimumInkRows)throw new Exception($"FlowDocument page has insufficient text pixels: {ink} pixels/{inkRows} rows");
        using var again=paginator.GetPage(0);
        if(again==DocumentPage.Missing || again.Visual==null)throw new Exception("cached FlowDocument page was lost across GC");
        if(paginator.GetPage(1)!=DocumentPage.Missing || !paginator.IsPageCountValid || paginator.PageCount!=1)throw new Exception("completed single-page document has incorrect end-of-document state");
        Console.WriteLine($"original FlowDocument {label}: finite page, glyph raster ({ink} pixels/{inkRows} rows), cached page/GC and end-of-document PASS");
    }
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var document=new FlowDocument(new Paragraph(new Run("Source FlowDocument paragraph layout."))){FontFamily=new FontFamily("Segoe UI"),FontSize=20,PagePadding=new Thickness(10),Foreground=Brushes.Black,ColumnWidth=double.PositiveInfinity};
            TextOptions.SetTextFormattingMode(document,mode);
            CheckPage(document,12,$"short {mode}");
            var first=new Paragraph(new Run("Source paragraph wrapping retains real line breaks and the original text formatter. The page contains several lines.")){Margin=new Thickness(5,8,5,12),Padding=new Thickness(3)};
            var second=new Paragraph(new Run("A second source paragraph checks vertical placement.")){Margin=new Thickness(5,7,5,0)};
            var multiple=new FlowDocument(first){FontFamily=new FontFamily("Segoe UI"),FontSize=20,PagePadding=new Thickness(10),Foreground=Brushes.Black,ColumnWidth=double.PositiveInfinity};
            multiple.Blocks.Add(second);
            TextOptions.SetTextFormattingMode(multiple,mode);
            CheckPage(multiple,55,$"wrapped paragraphs {mode}");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();
        Console.WriteLine("original FlowDocument finite paragraph/subtrack layout, wrapped text rendering and dispatcher resource cleanup PASS");
    }
}
