using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
class Check {
    static int Ink(Visual visual,bool bothColumns=false) {
        var bitmap=new RenderTargetBitmap(400,220,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);
        var bytes=new byte[400*220*4];bitmap.CopyPixels(bytes,400*4,0);
        int count=0;for(int i=0;i<bytes.Length;i+=4)if(bytes[i+3]>100&&bytes[i]<100&&bytes[i+1]<100&&bytes[i+2]<100)++count;
        int left=0,right=0,gap=0,rule=0;
        for(int y=10;y<210;++y)for(int x=10;x<330;++x) {
            int at=(y*400+x)*4;if(bytes[at+3]<=100)continue;
            bool dark=bytes[at]<100&&bytes[at+1]<100&&bytes[at+2]<100;
            if(dark&&x<155)++left;if(dark&&x>=185)++right;if(dark&&x>=160&&x<180)++gap;
            if(x>=160&&x<180&&bytes[at+2]>4*bytes[at]&&bytes[at+2]>4*bytes[at+1])++rule;
        }
        if(left<50||(bothColumns&&right<50)||gap!=0||rule<100)throw new Exception($"column glyph/rule geometry: left={left}, right={right}, gap={gap}, rule={rule}");
        Console.WriteLine($"column raster: left={left}, right={right}, gap={gap}, rule={rule}");
        if(count<50)throw new Exception("paginated page has no real glyph raster: "+count);return count;
    }
    [STAThread]
    static void Main() {
        var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var runs=new[]{new Run(string.Join(" ",Enumerable.Range(0,90).Select(i=>$"word{i:D3}"))),new Run(string.Join(" ",Enumerable.Range(90,70).Select(i=>$"next{i:D3}")))};
            var document=new FlowDocument(new Paragraph(runs[0]){Margin=new Thickness(0)}){FontFamily=new FontFamily("Segoe UI"),FontSize=20,Foreground=Brushes.Black,PagePadding=new Thickness(10),ColumnWidth=145,ColumnGap=30,IsColumnWidthFlexible=false,ColumnRuleWidth=2,ColumnRuleBrush=Brushes.Red};
            document.Blocks.Add(new Paragraph(runs[1]){Margin=new Thickness(0)});TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,totalInk=0;
            for(;pages<30;++pages) {
                using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;
                totalInk+=Ink(page.Visual,pages==0);
                var view=((IServiceProvider)page).GetService(viewType);
                foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)) {
                    var type=segment.GetType();var flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
                    ranges.Add((pages,(TextPointer)type.GetProperty("Start",flags).GetValue(segment),(TextPointer)type.GetProperty("End",flags).GetValue(segment)));
                }
                GC.Collect(2,GCCollectionMode.Forced,true,true);
            }
            if(pages<3||pages>=30||!paginator.IsPageCountValid||paginator.PageCount!=pages)throw new Exception("finite paginator did not reach the real document end");
            int checkedCharacters=0;
            foreach(var run in runs)for(int at=0;at<run.Text.Length;++at) {
                var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);
                var matches=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();
                if(matches.Length!=1)throw new Exception($"source character belongs to {matches.Length} pages at {at}");
                int mapped=paginator.GetPageNumber(position);if(mapped!=matches[0].Page)throw new Exception($"original content-position mapping: run={Array.IndexOf(runs,run)}, char={at}, offset={document.ContentStart.GetOffsetToPosition(position)}, mapped={mapped}, range={matches[0].Page}, pages={pages}");++checkedCharacters;
            }
            // Regenerate a disposed early page after later continuation records
            // exist, then retrieve a later page through the original break table.
            using(var first=paginator.GetPage(0))Ink(first.Visual);
            using(var last=paginator.GetPage(pages-1))Ink(last.Visual);
            Console.WriteLine($"original two-column pagination {mode}: {pages} rendered pages, {checkedCharacters} source characters covered exactly once, {totalInk} glyph pixels, content/page mapping, disposed-page regeneration and GC PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();
        Console.WriteLine("original multi-column/multi-page FlowDocument pagination and page/break-record dispatcher cleanup PASS");
    }
}
