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
    static int Ink(Visual visual) {
        // Keep the export taller than the physical page to observe retained
        // oversized line ink, including its portion below the page boundary.
        var bitmap=new RenderTargetBitmap(400,100,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);
        var bytes=new byte[400*100*4];bitmap.CopyPixels(bytes,400*4,0);
        int count=0;for(int i=0;i<bytes.Length;i+=4)if(bytes[i+3]>100&&bytes[i]<100&&bytes[i+1]<100&&bytes[i+2]<100)++count;
        if(count<20)throw new Exception("forced line has no retained glyph ink: "+count);return count;
    }
    [STAThread]
    static void Main() {
        var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool nested in new[]{false,true})
        foreach(bool keep in new[]{false,true}) {
            var runs=new[]{new Run("First oversized line"),new Run("Second oversized line"),new Run("Final oversized line")};
            var paragraphs=runs.Select(run=>new Paragraph(run){Margin=new Thickness(0),LineHeight=32,LineStackingStrategy=LineStackingStrategy.BlockLineHeight,KeepTogether=keep,KeepWithNext=keep}).ToArray();
            var document=new FlowDocument{FontFamily=new FontFamily("Segoe UI"),FontSize=24,Foreground=Brushes.Black,PagePadding=new Thickness(2),ColumnWidth=double.PositiveInfinity};
            if(nested){var section=new Section();foreach(var paragraph in paragraphs)section.Blocks.Add(paragraph);document.Blocks.Add(section);}
            else foreach(var paragraph in paragraphs)document.Blocks.Add(paragraph);
            TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,10);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,totalInk=0;
            for(;pages<12;++pages) {
                using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;
                if(page.Size!=new Size(400,10))throw new Exception("forced pagination changed requested page size: "+page.Size);
                totalInk+=Ink(page.Visual);
                var view=((IServiceProvider)page).GetService(viewType);
                foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)) {
                    var type=segment.GetType();var flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
                    ranges.Add((pages,(TextPointer)type.GetProperty("Start",flags).GetValue(segment),(TextPointer)type.GetProperty("End",flags).GetValue(segment)));
                }
                GC.Collect(2,GCCollectionMode.Forced,true,true);
            }
            if(pages!=3||!paginator.IsPageCountValid||paginator.PageCount!=pages)throw new Exception("forced paginator must finish in three pages: "+pages);
            int characters=0;
            foreach(var run in runs)for(int at=0;at<run.Text.Length;++at) {
                var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);
                var matches=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();
                int expected=Array.IndexOf(runs,run);
                if(matches.Length!=1||matches[0].Page!=expected||paginator.GetPageNumber(position)!=expected)throw new Exception($"forced source ownership/mapping at paragraph {expected}, character {at}");
                ++characters;
            }
            using(var first=paginator.GetPage(0))Ink(first.Visual);
            using(var last=paginator.GetPage(pages-1))Ink(last.Visual);
            Console.WriteLine($"original forced pagination {mode}, nested={nested}, keep={keep}: {pages} pages at requested size, {characters} characters covered exactly once, {totalInk} retained glyph pixels, regenerated pages and GC PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();
        Console.WriteLine("original undersized-page forced progress and dispatcher cleanup PASS");
    }
}
