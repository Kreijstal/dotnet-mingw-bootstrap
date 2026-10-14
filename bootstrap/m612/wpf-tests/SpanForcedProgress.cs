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
    static byte[] Raster(Visual visual) {
        var bitmap=new RenderTargetBitmap(400,220,96,96,PixelFormats.Pbgra32);
        bitmap.Render(visual);var pixels=new byte[400*220*4];bitmap.CopyPixels(pixels,1600,0);return pixels;
    }
    [STAThread] static void Main(string[] args) {
        var pageDirection=args.Length==0?FlowDirection.LeftToRight:Enum.Parse<FlowDirection>(args[0]);
        var tableDirection=args.Length==0?FlowDirection.LeftToRight:Enum.Parse<FlowDirection>(args[1]);
        var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(int height in new[]{10,40,80})foreach(int scenario in new[]{0,1,2}) {
            var table=new Table {FlowDirection=tableDirection,CellSpacing=0,Margin=new Thickness(0)};
            var group=new TableRowGroup();table.RowGroups.Add(group);var runs=new List<Run>();
            var span=new Run("large zero\nlarge one\nlarge two");runs.Add(span);
            for(int i=0;i<3;i++) {
                var row=new TableRow();group.Rows.Add(row);
                if(i==0)row.Cells.Add(new TableCell(new Paragraph(span){Margin=new Thickness(0),LineHeight=40,LineStackingStrategy=LineStackingStrategy.BlockLineHeight}){RowSpan=3,Padding=new Thickness(0)});
                if(scenario>0) {
                    var run=new Run(scenario==1?$"neighbor{i}":$"neighbor{i} zero\nneighbor{i} one\nneighbor{i} two");runs.Add(run);
                    row.Cells.Add(new TableCell(new Paragraph(run){Margin=new Thickness(0),LineHeight=40,LineStackingStrategy=LineStackingStrategy.BlockLineHeight}){Padding=new Thickness(0)});
                }
            }
            var document=new FlowDocument(table){FlowDirection=pageDirection,FontFamily=new FontFamily("Segoe UI"),FontSize=32,Foreground=Brushes.Lime,PagePadding=new Thickness(0),ColumnWidth=double.PositiveInfinity};
            TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,height);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();var rasters=new List<byte[]>();int pages=0;
            for(;pages<20;pages++) {
                using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;
                var pixels=Raster(page.Visual);int green=0;for(int p=0;p<pixels.Length;p+=4)if(pixels[p+3]>150&&pixels[p+1]>180&&pixels[p]<70&&pixels[p+2]<70)green++;
                if(height>=40&&green<30)throw new Exception($"Span fragment ink: height={height}, scenario={scenario}, page={pages}");rasters.Add(pixels);
                var view=((IServiceProvider)page).GetService(viewType);
                foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)) {
                    var t=segment.GetType();var flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
                    ranges.Add((pages,(TextPointer)t.GetProperty("Start",flags).GetValue(segment),(TextPointer)t.GetProperty("End",flags).GetValue(segment)));
                }
                GC.Collect(2,GCCollectionMode.Forced,true,true);
            }
            if(pages<2||pages>=20||!paginator.IsPageCountValid)throw new Exception("Oversized span fragments must make bounded progress");
            if(scenario==0&&pages!=(height==80?2:3))throw new Exception("Three source lines must retain their expected pages");
            foreach(var run in runs)for(int at=0;at<run.Text.Length;at++) {
                var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);
                var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();
                if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception($"Exact span source ownership: height={height}, scenario={scenario}, offset={at}, owners={owners.Length}");
            }
            for(int p=pages-1;p>=0;p--) {using var page=paginator.GetPage(p);if(!Raster(page.Visual).SequenceEqual(rasters[p]))throw new Exception("Disposed span page must regenerate identical glyphs after GC");}
            Console.WriteLine($"forced span {mode}, page={pageDirection}, table={tableDirection}, height={height}, scenario={scenario}: {pages} pages, {runs.Sum(r=>r.Text.Length)} characters owned once, glyphs/regeneration/GC PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original public forced span pagination PASS");
    }
}
