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
    static void CheckPlacement() {
        var first=new Paragraph(new Run("First red paragraph.")){Margin=new Thickness(0),Foreground=Brushes.Red};
        var second=new Paragraph(new Run("Second blue paragraph.")){Margin=new Thickness(0),Foreground=Brushes.Blue};
        var document=new FlowDocument(first){FontFamily=new FontFamily("Segoe UI"),FontSize=20,PagePadding=new Thickness(12),ColumnWidth=double.PositiveInfinity};document.Blocks.Add(second);
        var paginator=((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,300);
        using var page=paginator.GetPage(0);var image=new RenderTargetBitmap(400,300,96,96,PixelFormats.Pbgra32);image.Render(page.Visual);
        var pixels=new byte[400*300*4];image.CopyPixels(pixels,400*4,0);
        int redMinX=400,redMinY=300,redMaxY=-1,blueMinY=300,red=0,blue=0;
        for(int y=0;y<300;++y)for(int x=0;x<400;++x) {
            int at=(y*400+x)*4;if(pixels[at+3]<=100)continue;
            if(pixels[at+2]>4*pixels[at] && pixels[at+2]>4*pixels[at+1]){++red;redMinX=Math.Min(redMinX,x);redMinY=Math.Min(redMinY,y);redMaxY=Math.Max(redMaxY,y);}
            if(pixels[at]>4*pixels[at+2] && pixels[at]>4*pixels[at+1]){++blue;blueMinY=Math.Min(blueMinY,y);}
        }
        if(red<200||blue<200||redMinX<12||redMinY<12||blueMinY<=redMaxY)throw new Exception($"paragraph page coordinates overlap or ignore padding: red={red} x={redMinX} y={redMinY}..{redMaxY}, blue={blue} y={blueMinY}");
        Console.WriteLine($"original finite paragraph placement: padding x={redMinX}, red y={redMinY}..{redMaxY}, blue y={blueMinY}, disjoint glyph rows PASS");
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
        CheckPlacement();
        Dispatcher.CurrentDispatcher.InvokeShutdown();
        Console.WriteLine("original FlowDocument finite paragraph/subtrack layout, wrapped text rendering and dispatcher resource cleanup PASS");
    }
}
