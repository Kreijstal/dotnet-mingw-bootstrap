using System;
using System.Linq;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
class Check {
    static Paragraph Para(Run run,bool keep=false)=>new Paragraph(run){Margin=new Thickness(0),LineHeight=24,LineStackingStrategy=LineStackingStrategy.BlockLineHeight,MinOrphanLines=1,MinWidowLines=1,KeepTogether=keep};
    static int Render(Visual visual) {
        var bitmap=new RenderTargetBitmap(400,140,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);var bytes=new byte[400*140*4];bitmap.CopyPixels(bytes,400*4,0);
        int count=0;for(int at=0;at<bytes.Length;at+=4)if(bytes[at+3]>100&&bytes[at]<100&&bytes[at+1]<100&&bytes[at+2]<100)++count;
        if(count<100)throw new Exception("kept paragraph page has no glyph raster");return count;
    }
    static DynamicDocumentPaginator Paginator(FlowDocument document,TextFormattingMode mode) {
        document.FontFamily=new FontFamily("Segoe UI");document.FontSize=20;document.Foreground=Brushes.Black;document.PagePadding=new Thickness(10);document.ColumnWidth=double.PositiveInfinity;TextOptions.SetTextFormattingMode(document,mode);
        var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,140);return paginator;
    }
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool nested in new[]{false,true})
        foreach(bool keep in new[]{false,true}) {
            var prefix=new Run("Prefix line one.\nPrefix line two.\nPrefix line three.");
            var text=new Run("Kept line one.\nKept line two.\nKept line three.");
            var first=Para(prefix);var second=Para(text,keep);
            var document=new FlowDocument();
            if(nested){var section=new Section(first){Margin=new Thickness(0)};section.Blocks.Add(second);document.Blocks.Add(section);}
            else {document.Blocks.Add(first);document.Blocks.Add(second);}
            var paginator=Paginator(document,mode);int pages=0,ink=0;
            for(;pages<10;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;ink+=Render(page.Visual);}
            if(pages!=2||!paginator.IsPageCountValid||paginator.PageCount!=2)throw new Exception("keep policy created empty/repeated pages or lost text");
            int firstPage=paginator.GetPageNumber(text.ContentStart.GetPositionAtOffset(0,LogicalDirection.Forward));
            int lastPage=paginator.GetPageNumber(text.ContentStart.GetPositionAtOffset(text.Text.Length-1,LogicalDirection.Forward));
            if(firstPage!=(keep?1:0)||lastPage!=1)throw new Exception($"keep-together comparison failed: keep={keep}, first={firstPage}, last={lastPage}");
            foreach(var run in new[]{prefix,text})for(int at=0;at<run.Text.Length;++at) {
                int page=paginator.GetPageNumber(run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward));
                if(page<0||page>=2||(run==prefix&&page!=0)||(run==text&&keep&&page!=1))throw new Exception("kept source character maps outside its required page");
            }
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            using(var again=paginator.GetPage(1))Render(again.Visual);
            Console.WriteLine($"original KeepTogether {mode}, nested={nested}, keep={keep}: 2 rendered pages, kept range {firstPage}..{lastPage}, {ink} glyph pixels, source/page mapping and GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var run=new Run(string.Join("\n",Enumerable.Range(0,11).Select(i=>$"Oversized kept line {i}.")));
            var document=new FlowDocument(Para(run,true));var paginator=Paginator(document,mode);int pages=0;
            for(;pages<10;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;Render(page.Visual);}
            if(pages!=3||!paginator.IsPageCountValid||paginator.GetPageNumber(run.ContentStart.GetPositionAtOffset(run.Text.Length-1,LogicalDirection.Forward))!=2)throw new Exception("oversized kept paragraph does not make bounded progress to the document end");
            Console.WriteLine($"original oversized KeepTogether {mode}: 3 rendered pages, complete source and bounded progress PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var prefix=Para(new Run("Prefix one.\nPrefix two.\nPrefix three."));prefix.Foreground=Brushes.Red;
            var kept=Para(new Run("Kept one.\nKept two.\nKept three."),true);kept.Foreground=Brushes.Blue;
            var document=new FlowDocument(prefix);document.Blocks.Add(kept);var paginator=Paginator(document,mode);
            document.ColumnWidth=145;document.ColumnGap=30;document.IsColumnWidthFlexible=false;
            using var page=paginator.GetPage(0);if(paginator.GetPage(1)!=DocumentPage.Missing||paginator.PageCount!=1)throw new Exception("kept paragraph did not resume in the next column of the same page");
            var bitmap=new RenderTargetBitmap(400,140,96,96,PixelFormats.Pbgra32);bitmap.Render(page.Visual);var pixels=new byte[400*140*4];bitmap.CopyPixels(pixels,400*4,0);
            int red=0,blue=0,wrong=0;
            for(int y=0;y<140;++y)for(int x=0;x<400;++x) {
                int at=(y*400+x)*4;if(pixels[at+3]<100)continue;
                bool r=pixels[at+2]>4*pixels[at]&&pixels[at+2]>4*pixels[at+1],b=pixels[at]>4*pixels[at+1]&&pixels[at]>4*pixels[at+2];
                if(r){++red;if(x<10||x>=155)++wrong;}if(b){++blue;if(x<185||x>=330)++wrong;}
            }
            if(red<200||blue<200||wrong!=0)throw new Exception($"keep-together column placement: red={red}, blue={blue}, wrong={wrong}");
            Console.WriteLine($"original column KeepTogether {mode}: preceding red and intact blue paragraph in separate columns, glyph placement and same-page completion PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original keep-together paragraph pagination and dispatcher resource cleanup PASS");
    }
}
