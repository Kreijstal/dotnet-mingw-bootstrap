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
    const BindingFlags Fields=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
    static readonly Type ViewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
    static byte[] Raster(Visual visual) {
        var bitmap=new RenderTargetBitmap(400,260,96,96,PixelFormats.Pbgra32);
        bitmap.Render(visual);var pixels=new byte[400*260*4];bitmap.CopyPixels(pixels,1600,0);return pixels;
    }
    static Rect Bounds(byte[] pixels,int color) {
        int left=400,top=260,right=-1,bottom=-1;
        for(int y=0;y<260;++y)for(int x=0;x<400;++x) {
            int at=(y*400+x)*4;if(pixels[at+3]<150)continue;
            bool match=color==0?pixels[at+2]>200&&pixels[at]<60&&pixels[at+1]<60:
                color==1?pixels[at+1]>180&&pixels[at]<60&&pixels[at+2]<60:color==2?pixels[at]>200&&pixels[at+1]<60&&pixels[at+2]<60:pixels[at+2]>200&&pixels[at+1]>200&&pixels[at]<60;
            if(match){left=Math.Min(left,x);right=Math.Max(right,x);top=Math.Min(top,y);bottom=Math.Max(bottom,y);}
        }
        return right<0?Rect.Empty:new Rect(left,top,right-left+1,bottom-top+1);
    }
    static Floater Float(Brush brush,double height,HorizontalAlignment alignment,out Border border) {
        border=new Border{Width=80,Height=height,Background=brush};
        return new Floater(new BlockUIContainer(border){Margin=new Thickness(0)}){Width=80,HorizontalAlignment=alignment,Margin=new Thickness(0),Padding=new Thickness(0)};
    }
    static FlowDocument Make(FlowDirection pageDirection,FlowDirection sectionDirection,WrapDirection clear,bool prefix,out Run anchor,out Run body,out Border redBorder,out Section section) {
        var red=Float(Brushes.Red,80,HorizontalAlignment.Left,out redBorder);
        var green=Float(Brushes.Lime,140,HorizontalAlignment.Right,out var unused);
        anchor=new Run("anchor.");var host=new Paragraph(red){Margin=new Thickness(0)};host.Inlines.Add(green);host.Inlines.Add(anchor);
        body=new Run(string.Join(" ",Enumerable.Range(0,40).Select(i=>$"body{i:D2}")));
        section=new Section{Margin=new Thickness(0),Padding=new Thickness(0),FlowDirection=sectionDirection,ClearFloaters=clear,Background=Brushes.Yellow};
        section.Blocks.Add(new BlockUIContainer(new Border{Width=70,Height=40,Background=Brushes.Blue}){Margin=new Thickness(0)});
        section.Blocks.Add(new Paragraph(body){Margin=new Thickness(0)});
        var document=new FlowDocument(host){FontFamily=new FontFamily("Segoe UI"),FontSize=18,Foreground=Brushes.Black,FlowDirection=pageDirection,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};
        if(prefix)document.Blocks.InsertBefore(host,new BlockUIContainer(new Border{Width=90,Height=90,Background=Brushes.Gray}){Margin=new Thickness(0)});
        document.Blocks.Add(section);return document;
    }
    static void Placement(Rect red,Rect green,Rect blue,WrapDirection clear,bool reversed) {
        if(blue.IsEmpty||Math.Abs(blue.Height-40)>1)throw new Exception("original section child raster missing or clipped");
        if(red.IsEmpty&&green.IsEmpty)return; // A deferred section starts on a fresh page.
        double minimum=0;
        if(clear==WrapDirection.Both)minimum=Math.Max(red.IsEmpty?0:red.Bottom,green.IsEmpty?0:green.Bottom);
        if(clear==WrapDirection.Left)minimum=(reversed?green:red).Bottom;
        if(clear==WrapDirection.Right)minimum=(reversed?red:green).Bottom;
        if(clear!=WrapDirection.None&&Math.Abs(blue.Top-minimum)>1)throw new Exception($"section clear {clear}: child={blue}, red={red}, green={green}, reversed={reversed}");
        if(clear==WrapDirection.None&&blue.Top>=Math.Max(red.Bottom,green.Bottom)-1)throw new Exception("uncleared section must retain overlapping floaters beside its text");
    }
    static void Pump(FrameworkElement element) {
        element.UpdateLayout();var frame=new DispatcherFrame();Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle,new Action(()=>frame.Continue=false));Dispatcher.PushFrame(frame);element.UpdateLayout();
    }
    [STAThread] static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var pageDirection in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
        foreach(bool reversed in new[]{false,true})
        foreach(var clear in new[]{WrapDirection.None,WrapDirection.Left,WrapDirection.Right,WrapDirection.Both})
        foreach(bool prefix in new[]{false,true}) {
            var sectionDirection=reversed?(pageDirection==FlowDirection.LeftToRight?FlowDirection.RightToLeft:FlowDirection.LeftToRight):pageDirection;
            var document=Make(pageDirection,sectionDirection,clear,prefix,out var anchor,out var body,out var border,out var section);
            TextOptions.SetTextFormattingMode(document,mode);var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,260);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,bluePage=-1;byte[] blueRaster=null;
            for(;pages<15;++pages) {
                using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;
                var pixels=Raster(page.Visual);var blue=Bounds(pixels,2);
                if(!blue.IsEmpty){if(bluePage>=0)throw new Exception("section child duplicated across page continuations");bluePage=pages;blueRaster=pixels;Placement(Bounds(pixels,0),Bounds(pixels,1),blue,clear,reversed);var background=Bounds(pixels,3);if(background.IsEmpty||Math.Abs(background.Top-blue.Top)>1)throw new Exception("section background must begin at the cleared child origin");}
                object view=((IServiceProvider)page).GetService(ViewType);
                foreach(object segment in (IEnumerable)ViewType.GetProperty("TextSegments").GetValue(view)){var type=segment.GetType();ranges.Add((pages,(TextPointer)type.GetProperty("Start",Fields).GetValue(segment),(TextPointer)type.GetProperty("End",Fields).GetValue(segment)));}
                GC.Collect(2,GCCollectionMode.Forced,true,true);if(!pixels.SequenceEqual(Raster(page.Visual)))throw new Exception("section clearing geometry changed across GC");
            }
            if(pages==0||pages>=15||bluePage<0||!paginator.IsPageCountValid)throw new Exception("section clearing failed to complete actual source content");
            bool deferred=prefix&&(clear==WrapDirection.Both||(reversed?clear==WrapDirection.Left:clear==WrapDirection.Right));
            if(bluePage!=(deferred?1:0)||paginator.GetPageNumber(anchor.ContentStart)!=0)throw new Exception("cleared section must defer without consuming or moving its preceding host source");
            foreach(var run in new[]{anchor,body})for(int i=0;i<run.Text.Length;++i){var position=run.ContentStart.GetPositionAtOffset(i,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("section clearing loses exact source/page ownership");}
            using(var page=paginator.GetPage(bluePage))if(!blueRaster.SequenceEqual(Raster(page.Visual)))throw new Exception("section regeneration changed cleared geometry");
            Console.WriteLine($"original section clearing {mode}, page={pageDirection}, reversed={reversed}, clear={clear}, prefix={prefix}: {pages} pages, exact source/page mapping, child placement, regeneration and GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft}) {
            var document=Make(direction,direction,WrapDirection.Left,false,out var anchor,out var body,out var border,out var section);body.Text=string.Join(" ",Enumerable.Range(0,180).Select(i=>$"scroll{i:D3}"));TextOptions.SetTextFormattingMode(document,mode);
            var viewer=new FlowDocumentScrollViewer{Document=document,IsToolBarVisible=false};var window=new Window{Content=viewer,Width=420,Height=300,ShowInTaskbar=false};window.Show();Pump(window);
            var before=Raster(viewer);var red=Bounds(before,0);var blue=Bounds(before,2);Placement(red,Bounds(before,1),blue,WrapDirection.Left,false);
            border.Height=120;Pump(window);var after=Raster(viewer);var grownRed=Bounds(after,0);var grownBlue=Bounds(after,2);
            if(grownRed.IsEmpty||grownBlue.IsEmpty||grownBlue.Top<blue.Top+39||grownBlue.Top<grownRed.Bottom-1)throw new Exception("bottomless section clearing must follow resized inherited floater");
            Placement(grownRed,Bounds(after,1),grownBlue,WrapDirection.Left,false);
            section.ClearFloaters=WrapDirection.Right;Pump(window);var changed=Raster(viewer);Placement(Bounds(changed,0),Bounds(changed,1),Bounds(changed,2),WrapDirection.Right,false);
            var scroll=(ScrollViewer)viewer.Template.FindName("PART_ContentHost",viewer);scroll.ScrollToEnd();Pump(window);if(scroll.VerticalOffset<=0||scroll.VerticalOffset+scroll.ViewportHeight<scroll.ExtentHeight-1)throw new Exception("cleared section cannot scroll to its real end");
            window.Close();GC.Collect(2,GCCollectionMode.Forced,true,true);
            Console.WriteLine($"original bottomless section clearing {mode} {direction}: actual resized floater/clear property update, end scrolling and GC PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original section clear/pagination/update and dispatcher cleanup PASS");
    }
}
