using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
class Check {
    static void Pump(FrameworkElement element){element.UpdateLayout();var frame=new DispatcherFrame();Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle,new Action(()=>frame.Continue=false));Dispatcher.PushFrame(frame);element.UpdateLayout();}
    static byte[] Raster(Visual visual){var image=new RenderTargetBitmap(400,220,96,96,PixelFormats.Pbgra32);image.Render(visual);var bytes=new byte[400*220*4];image.CopyPixels(bytes,400*4,0);return bytes;}
    [STAThread]
    static void Main() {
        var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var alignment in new[]{HorizontalAlignment.Left,HorizontalAlignment.Right}) {
            var floatingRun=new Run("Original floating text");var bodyRun=new Run(string.Join(" ",Enumerable.Range(0,100).Select(i=>$"body{i:D3}")));
            var floater=new Floater(new Paragraph(floatingRun){Margin=new Thickness(0),Foreground=Brushes.Blue}){Width=120,HorizontalAlignment=alignment,Margin=new Thickness(0),Padding=new Thickness(4),Background=Brushes.Red};
            var paragraph=new Paragraph(floater){Margin=new Thickness(0)};paragraph.Inlines.Add(bodyRun);
            var document=new FlowDocument(paragraph){FontFamily=new FontFamily("Segoe UI"),FontSize=18,Foreground=Brushes.Black,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0;
            for(;pages<20;++pages) {
                using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;
                var pixels=Raster(page.Visual);int red=0,blue=0,black=0,minX=400,maxX=-1,minY=220,maxY=-1;
                for(int y=0;y<220;++y)for(int x=0;x<400;++x){int i=(y*400+x)*4;if(pixels[i+3]<150)continue;if(pixels[i+2]>200&&pixels[i]<60&&pixels[i+1]<60){++red;minX=Math.Min(minX,x);maxX=Math.Max(maxX,x);minY=Math.Min(minY,y);maxY=Math.Max(maxY,y);}if(pixels[i]>180&&pixels[i+2]<70&&pixels[i+1]<70)++blue;if(pixels[i]<70&&pixels[i+1]<70&&pixels[i+2]<70)++black;}
                if(black<100)throw new Exception("floater host text page has no real glyph ink");
                if(pages==0) {
                    if(red<1000||blue<30)throw new Exception($"original floater content/background missing: red={red}, blue={blue}");
                    if(alignment==HorizontalAlignment.Left&&minX!=10||alignment==HorizontalAlignment.Right&&maxX!=389)throw new Exception("floater horizontal placement: "+minX+".."+maxX);
                    int forbidden=0,beside=0;
                    for(int y=minY;y<=maxY;++y)for(int x=0;x<400;++x){int i=(y*400+x)*4;if(pixels[i+3]>150&&pixels[i]<70&&pixels[i+1]<70&&pixels[i+2]<70){if(x>=minX&&x<=maxX)++forbidden;else ++beside;}}
                    if(forbidden!=0||beside<100)throw new Exception($"host glyphs must flow beside the actual floater rectangle: overlap={forbidden}, beside={beside}");
                    Console.WriteLine($"original floater {mode}, {alignment}: red bounds {minX}..{maxX}, {minY}..{maxY}, {blue} blue glyph pixels, {beside} host pixels beside and zero overlap PASS");
                }else if(red!=0||blue!=0)throw new Exception("intact first-page floater must not repeat on later text pages");
                var view=((IServiceProvider)page).GetService(viewType);
                foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();var flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;ranges.Add((pages,(TextPointer)t.GetProperty("Start",flags).GetValue(segment),(TextPointer)t.GetProperty("End",flags).GetValue(segment)));}
                GC.Collect(2,GCCollectionMode.Forced,true,true);
            }
            if(pages<2||pages>=20||!paginator.IsPageCountValid)throw new Exception("floater text pagination did not complete");
            foreach(var run in new[]{floatingRun,bodyRun})for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("floater/host source character coverage and page mapping at "+at);}
            using(var first=paginator.GetPage(0)){var regenerated=Raster(first.Visual);int red=0;for(int i=0;i<regenerated.Length;i+=4)if(regenerated[i+3]>150&&regenerated[i+2]>200&&regenerated[i]<60&&regenerated[i+1]<60)++red;if(red<1000)throw new Exception("disposed floater page must regenerate actual floating content");}
            Console.WriteLine($"original floater {mode}, {alignment}: {pages} pages, {floatingRun.Text.Length+bodyRun.Text.Length} source characters covered exactly once, original page mapping/regeneration/GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var floatingRun=new Run("Original scrolling floater");var bodyRun=new Run(string.Join(" ",Enumerable.Range(0,130).Select(i=>$"scroll{i:D3}")));
            var floater=new Floater(new Paragraph(floatingRun){Margin=new Thickness(0),Foreground=Brushes.Blue}){Width=120,HorizontalAlignment=HorizontalAlignment.Left,Margin=new Thickness(0),Padding=new Thickness(4),Background=Brushes.Red};
            var paragraph=new Paragraph(floater){Margin=new Thickness(0)};paragraph.Inlines.Add(bodyRun);var document=new FlowDocument(paragraph){FontSize=18,FontFamily=new FontFamily("Segoe UI"),PagePadding=new Thickness(10)};TextOptions.SetTextFormattingMode(document,mode);
            var viewer=new FlowDocumentScrollViewer{Document=document,IsToolBarVisible=false};var window=new Window{Content=viewer,Width=420,Height=260,ShowInTaskbar=false};window.Show();Pump(window);
            var scroll=(ScrollViewer)viewer.Template.FindName("PART_ContentHost",viewer);double oldHeight=scroll.ExtentHeight;var before=Raster(viewer);
            floatingRun.Text+=" Additional original floater text changes the obstacle height and the host line widths.";Pump(window);var after=Raster(viewer);
            if(before.SequenceEqual(after)||scroll.ExtentHeight<oldHeight||scroll.ExtentHeight<=scroll.ViewportHeight)throw new Exception("edited scrolling floater must update glyphs/obstacle/extent");
            scroll.ScrollToEnd();Pump(window);if(scroll.VerticalOffset<=0)throw new Exception("scrolling floater host must retain its full text extent");window.Close();
            Console.WriteLine($"original scrolling floater {mode}: edited content changes obstacle/host raster, full extent {oldHeight:F1}->{scroll.ExtentHeight:F1}, scrolling and cleanup PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original rectangular floater wrapping and dispatcher cleanup PASS");
    }
}
