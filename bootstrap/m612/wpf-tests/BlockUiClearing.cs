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
    static FlowDocument Make(FlowDirection direction,FlowDirection inner,WrapDirection clear,bool prefix,out Run text,out Border blue,out BlockUIContainer block) {
        var red=Float(Brushes.Red,80,HorizontalAlignment.Left,out var unused);
        var green=Float(Brushes.Lime,140,HorizontalAlignment.Right,out unused);
        var host=new Paragraph(red){Margin=new Thickness(0)};host.Inlines.Add(green);host.Inlines.Add(new Run("anchor."));
        blue=new Border{Width=70,Height=40,Background=Brushes.Blue};
        block=new BlockUIContainer(blue){Margin=new Thickness(0),ClearFloaters=clear};
        var section=new Section(block){Margin=new Thickness(0),FlowDirection=inner};
        text=new Run(string.Join(" ",Enumerable.Range(0,50).Select(i=>$"body{i:D2}")));section.Blocks.Add(new Paragraph(text){Margin=new Thickness(0)});
        var document=new FlowDocument(host){FontFamily=new FontFamily("Segoe UI"),FontSize=18,FlowDirection=direction,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};
        if(prefix)document.Blocks.InsertBefore(host,new BlockUIContainer(new Border{Width=90,Height=90,Background=Brushes.Gray}){Margin=new Thickness(0)});
        document.Blocks.Add(section);return document;
    }
    static void Placement(byte[] pixels,WrapDirection clear,bool reversed) {
        var red=Bounds(pixels,0);var green=Bounds(pixels,1);var blue=Bounds(pixels,2);
        if(blue.IsEmpty||Math.Abs(blue.Height-40)>1)throw new Exception("cleared block child missing or clipped");
        if(red.IsEmpty&&green.IsEmpty)return;
        double minimum=0;
        if(clear==WrapDirection.Both)minimum=Math.Max(red.IsEmpty?0:red.Bottom,green.IsEmpty?0:green.Bottom);
        if(clear==WrapDirection.Left)minimum=(reversed?green:red).Bottom;
        if(clear==WrapDirection.Right)minimum=(reversed?red:green).Bottom;
        if(clear!=WrapDirection.None&&Math.Abs(blue.Top-minimum)>1)throw new Exception($"block clear {clear}: child={blue}, red={red}, green={green}, reversed={reversed}");
        if(clear==WrapDirection.None&&blue.Top>=Math.Max(red.Bottom,green.Bottom)-1)throw new Exception("uncleared nested block must retain the existing float flow");
    }
    static void Pump(FrameworkElement element) {
        element.UpdateLayout();var frame=new DispatcherFrame();Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle,new Action(()=>frame.Continue=false));Dispatcher.PushFrame(frame);element.UpdateLayout();
    }
    [STAThread] static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
        foreach(bool reversed in new[]{false,true})
        foreach(var clear in new[]{WrapDirection.None,WrapDirection.Left,WrapDirection.Right,WrapDirection.Both})
        foreach(bool prefix in new[]{false,true}) {
            var inner=reversed?(direction==FlowDirection.LeftToRight?FlowDirection.RightToLeft:FlowDirection.LeftToRight):direction;
            var document=Make(direction,inner,clear,prefix,out var text,out var child,out var block);TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,260);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,bluePages=0;
            byte[] original=null;int childPage=-1;
            for(;pages<15;++pages) {
                using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;
                var pixels=Raster(page.Visual);
                if(!Bounds(pixels,2).IsEmpty){Placement(pixels,clear,reversed);++bluePages;original=pixels;childPage=pages;}
                object view=((IServiceProvider)page).GetService(ViewType);
                foreach(object segment in (IEnumerable)ViewType.GetProperty("TextSegments").GetValue(view)){var type=segment.GetType();ranges.Add((pages,(TextPointer)type.GetProperty("Start",Fields).GetValue(segment),(TextPointer)type.GetProperty("End",Fields).GetValue(segment)));}
                GC.Collect(2,GCCollectionMode.Forced,true,true);if(!pixels.SequenceEqual(Raster(page.Visual)))throw new Exception("block clearing raster changed across GC");
            }
            if(pages<2||pages>=15||bluePages!=1||!paginator.IsPageCountValid||paginator.GetPageNumber(block.ContentStart)!=childPage)throw new Exception($"cleared block {mode} {direction}, reversed={reversed}, {clear}, prefix={prefix}: pages={pages}, bluePages={bluePages}, childPage={childPage}, valid={paginator.IsPageCountValid}");
            for(int i=0;i<text.Text.Length;++i){var position=text.ContentStart.GetPositionAtOffset(i,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("cleared block loses following source/page coverage");}
            using(var again=paginator.GetPage(childPage))if(!original.SequenceEqual(Raster(again.Visual)))throw new Exception("cleared block regeneration changed geometry");
            Console.WriteLine($"original block clearing {mode} {direction}, reversed={reversed}, {clear}, prefix={prefix}: selective geometry, source/pages, regeneration and GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
        foreach(bool reversed in new[]{false,true})
        foreach(int margin in new[]{0,12}) {
            var inner=reversed?(direction==FlowDirection.LeftToRight?FlowDirection.RightToLeft:FlowDirection.LeftToRight):direction;
            var document=Make(direction,inner,WrapDirection.Both,false,out var text,out var child,out var block);
            block.Margin=new Thickness(0,margin,0,margin);block.Padding=new Thickness(6);block.BorderBrush=Brushes.Magenta;block.BorderThickness=new Thickness(2);block.Background=Brushes.Yellow;
            TextOptions.SetTextFormattingMode(document,mode);var paginator=((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,260);
            using var page=paginator.GetPage(0);var pixels=Raster(page.Visual);var green=Bounds(pixels,1);var blue=Bounds(pixels,2);var yellow=Bounds(pixels,3);
            if(green.IsEmpty||blue.IsEmpty||yellow.IsEmpty||Math.Abs(blue.Top-green.Bottom-margin-8)>1||yellow.Top<green.Bottom+margin-1)throw new Exception($"styled block clearance applied after margins/padding or lost border/background: green={green}, blue={blue}, background={yellow}, margin={margin}");
            Console.WriteLine($"original styled block clearing {mode} {direction}, reversed={reversed}, margin={margin}: outer frame and content padding PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
        foreach(bool reversed in new[]{false,true}) {
            var inner=reversed?(direction==FlowDirection.LeftToRight?FlowDirection.RightToLeft:FlowDirection.LeftToRight):direction;
            var document=Make(direction,inner,WrapDirection.Both,false,out var text,out var child,out var block);TextOptions.SetTextFormattingMode(document,mode);
            var viewer=new FlowDocumentScrollViewer{Document=document,IsToolBarVisible=false};var window=new Window{Content=viewer,Width=420,Height=340,ShowInTaskbar=false};window.Show();Pump(window);
            foreach(var clear in new[]{WrapDirection.Both,WrapDirection.None,WrapDirection.Left,WrapDirection.Right,WrapDirection.Both}) {
                block.ClearFloaters=clear;Pump(window);var pixels=Raster(viewer);Placement(pixels,clear,reversed);
                GC.Collect(2,GCCollectionMode.Forced,true,true);Pump(window);if(!pixels.SequenceEqual(Raster(viewer)))throw new Exception("bottomless block clearing changed across GC");
            }
            window.Close();Console.WriteLine($"original bottomless block clearing {mode} {direction}, reversed={reversed}: live clear changes and GC PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original block UI clearing finite/bottomless documents and cleanup PASS");
    }
}
