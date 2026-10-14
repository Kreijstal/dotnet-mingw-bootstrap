using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
class Check {
    static int Ink(Visual visual) {
        var bitmap=new RenderTargetBitmap(400,220,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);
        var bytes=new byte[400*220*4];bitmap.CopyPixels(bytes,400*4,0);
        int count=0,minX=400,maxX=-1;for(int i=0;i<bytes.Length;i+=4)if(bytes[i+3]>100&&bytes[i]<100&&bytes[i+1]<100&&bytes[i+2]<100){++count;int x=i/4%400;minX=Math.Min(minX,x);maxX=Math.Max(maxX,x);}if(minX<16||maxX>368)throw new Exception("asymmetric physical page margins: "+minX+".."+maxX);
        if(count<50)throw new Exception("paginated page has no real glyph raster: "+count);return count;
    }
    static void Pump(FrameworkElement element){element.UpdateLayout();var frame=new DispatcherFrame();Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle,new Action(()=>frame.Continue=false));Dispatcher.PushFrame(frame);element.UpdateLayout();}
    static byte[] Viewport(Visual visual){var image=new RenderTargetBitmap(400,300,96,96,PixelFormats.Pbgra32);image.Render(visual);var pixels=new byte[400*300*4];image.CopyPixels(pixels,1600,0);int ink=0;for(int i=0;i<pixels.Length;i+=4)if(pixels[i+3]>150&&pixels[i]<100&&pixels[i+1]<100&&pixels[i+2]<100)++ink;if(ink<500)throw new Exception("RTL scrolling viewport glyphs missing");return pixels;}
    [STAThread]
    static void Main() {
        var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var pageDirection in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
        foreach(bool reverseParagraph in new[]{false,true})
        foreach(bool columns in new[]{false,true}) {
            var paragraphDirection=reverseParagraph?(pageDirection==FlowDirection.LeftToRight?FlowDirection.RightToLeft:FlowDirection.LeftToRight):pageDirection;
            var runs=new[]{new Run(string.Join(" ",Enumerable.Range(0,90).Select(i=>$"אבג{i:D3}"))),new Run(string.Join(" ",Enumerable.Range(90,70).Select(i=>$"مرحبا{i:D3}")))};
            var document=new FlowDocument(new Paragraph(runs[0]){Margin=new Thickness(0),FlowDirection=paragraphDirection,TextAlignment=paragraphDirection==FlowDirection.RightToLeft?TextAlignment.Right:TextAlignment.Left}){FontFamily=new FontFamily("Segoe UI"),FontSize=20,Foreground=Brushes.Black,FlowDirection=pageDirection,PagePadding=new Thickness(18,10,34,10),ColumnWidth=columns?160:double.PositiveInfinity,ColumnGap=20};
            document.Blocks.Add(new Paragraph(runs[1]){Margin=new Thickness(0),FlowDirection=paragraphDirection,TextAlignment=paragraphDirection==FlowDirection.RightToLeft?TextAlignment.Right:TextAlignment.Left});TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,totalInk=0;
            for(;pages<30;++pages) {
                using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;
                totalInk+=Ink(page.Visual);
                var view=((IServiceProvider)page).GetService(viewType);
                if(pages==0&&columns){var firstRect=(Rect)viewType.GetMethod("GetRectangleFromTextPosition").Invoke(view,new object[]{runs[0].ContentStart});if(firstRect.IsEmpty||(pageDirection==FlowDirection.RightToLeft?firstRect.Left<190:firstRect.Right>190))throw new Exception("first source character must occupy the first column in page reading order: "+firstRect);}
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
            Console.WriteLine($"original RTL pagination {mode}, page={pageDirection}, paragraph={paragraphDirection}, columns={columns}: {pages} rendered pages, {checkedCharacters} source characters covered exactly once, {totalInk} glyph pixels, content/page mapping, disposed-page regeneration and GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft}) {
            var run=new Run("אבג مرحبا Latin initial scrolling source.");var first=new Paragraph(run){Margin=new Thickness(0)};
            var document=new FlowDocument(first){FlowDirection=direction,FontFamily=new FontFamily("Segoe UI"),FontSize=20,PagePadding=new Thickness(18,10,34,10)};
            for(int i=0;i<16;++i)document.Blocks.Add(new Paragraph(new Run($"Paragraph {i}: אבג مرحبا Latin source content reaches the end of the original scrolling document.")){Margin=new Thickness(0)});
            TextOptions.SetTextFormattingMode(document,mode);var viewer=new FlowDocumentScrollViewer{Document=document,IsToolBarVisible=false};var window=new Window{Content=viewer,Width=420,Height=340,ShowInTaskbar=false};
            window.Show();Pump(window);var scroll=(ScrollViewer)viewer.Template.FindName("PART_ContentHost",viewer);double extent=scroll.ExtentHeight;if(extent<=scroll.ViewportHeight*3)throw new Exception("RTL bottomless full extent missing");var top=Viewport(viewer);
            scroll.ScrollToEnd();Pump(window);var bottom=Viewport(viewer);if(scroll.VerticalOffset<=scroll.ViewportHeight||top.SequenceEqual(bottom))throw new Exception("RTL bottomless scrolling did not retain different end content");scroll.ScrollToHome();Pump(window);
            run.Text+=" Added אבג مرحبا Latin words repeat to grow the first paragraph and change the original bottomless layout.";Pump(window);var edited=Viewport(viewer);if(top.SequenceEqual(edited)||scroll.ExtentHeight<=extent)throw new Exception("RTL bottomless edit must change extent and glyph raster");
            Console.WriteLine($"original horizontal RTL scrolling {mode} {direction}: extent {extent:F1}->{scroll.ExtentHeight:F1}, top/end/edited glyphs, full scrolling and original cleanup PASS");window.Close();GC.Collect(2,GCCollectionMode.Forced,true,true);
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
        foreach(var tableDirection in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft}) {
            var table=new Table{CellSpacing=2,Margin=new Thickness(0),FlowDirection=tableDirection};table.Columns.Add(new TableColumn{Width=new GridLength(150)});table.Columns.Add(new TableColumn{Width=new GridLength(150)});var group=new TableRowGroup();table.RowGroups.Add(group);
            var row=new TableRow();group.Rows.Add(row);row.Cells.Add(new TableCell(new Paragraph(new Run("אבג first")){Margin=new Thickness(0)}){Background=Brushes.Red,Padding=new Thickness(4)});row.Cells.Add(new TableCell(new Paragraph(new Run("مرحبا second")){Margin=new Thickness(0)}){Background=Brushes.Blue,Padding=new Thickness(4)});
            var document=new FlowDocument(table){FlowDirection=direction,FontFamily=new FontFamily("Segoe UI"),FontSize=18,PagePadding=new Thickness(18,10,34,10)};TextOptions.SetTextFormattingMode(document,mode);
            var paginator=((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);using var page=paginator.GetPage(0);var image=new RenderTargetBitmap(400,220,96,96,PixelFormats.Pbgra32);image.Render(page.Visual);var pixels=new byte[400*220*4];image.CopyPixels(pixels,1600,0);long redX=0,blueX=0;int red=0,blue=0,black=0;
            for(int y=0;y<220;++y)for(int x=0;x<400;++x){int at=(y*400+x)*4;if(pixels[at+3]>150&&pixels[at]<60&&pixels[at+1]<60&&pixels[at+2]<60)++black;if(pixels[at+3]>150&&pixels[at+2]>200&&pixels[at]<60&&pixels[at+1]<60){++red;redX+=x;}if(pixels[at+3]>150&&pixels[at]>200&&pixels[at+2]<60&&pixels[at+1]<60){++blue;blueX+=x;}}
            if(red<1000||blue<1000||black<100||(tableDirection==FlowDirection.RightToLeft?(double)redX/red<(double)blueX/blue:(double)redX/red>(double)blueX/blue))throw new Exception("RTL table cell reading order/background geometry");
            Console.WriteLine($"original horizontal RTL table {mode}, page={direction}, table={tableDirection}: logical first/second cell backgrounds, original subpage glyphs and arrangement PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();
        Console.WriteLine("original multi-page FlowDocument pagination and page/break-record dispatcher cleanup PASS");
    }
}
