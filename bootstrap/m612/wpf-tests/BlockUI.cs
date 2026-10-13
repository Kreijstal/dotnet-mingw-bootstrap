using System;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
class Check {
    static int Red(Visual visual,int height=200) {
        var bitmap=new RenderTargetBitmap(400,height,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);
        var bytes=new byte[400*height*4];bitmap.CopyPixels(bytes,400*4,0);
        int count=0;for(int i=0;i<bytes.Length;i+=4)if(bytes[i+3]>200&&bytes[i+2]>200&&bytes[i]<60&&bytes[i+1]<60)++count;
        return count;
    }
    static void Pump(FrameworkElement element) {
        element.UpdateLayout();var frame=new DispatcherFrame();
        Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle,new Action(()=>frame.Continue=false));
        Dispatcher.PushFrame(frame);element.UpdateLayout();
    }
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool nested in new[]{false,true}) {
            var before=new Run("Before first\nBefore second\nBefore third");var after=new Run("After the original block control");
            var button=new Button{Content="Original block button",Width=180,Height=28};int clicks=0;button.Click+=(s,e)=>++clicks;
            var child=new Border{Background=Brushes.Red,Height=60,Child=button};
            var block=new BlockUIContainer(child){Margin=new Thickness(8,6,12,10)};
            var first=new Paragraph(before){Margin=new Thickness(0),LineHeight=24,LineStackingStrategy=LineStackingStrategy.BlockLineHeight};
            var last=new Paragraph(after){Margin=new Thickness(0),LineHeight=24,LineStackingStrategy=LineStackingStrategy.BlockLineHeight};
            var document=new FlowDocument{FontFamily=new FontFamily("Segoe UI"),FontSize=16,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};
            if(nested){var section=new Section();section.Blocks.Add(first);section.Blocks.Add(block);section.Blocks.Add(last);document.Blocks.Add(section);}
            else {document.Blocks.Add(first);document.Blocks.Add(block);document.Blocks.Add(last);}
            TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,140);
            using(var page0=paginator.GetPage(0)) {
                if(Red(page0.Visual)!=0)throw new Exception("block control should defer intact after preceding text");
            }
            using(var page1=paginator.GetPage(1)) {
                int ink=Red(page1.Visual);if(ink<1000||child.RenderSize.Height!=60||child.RenderSize.Width<300)throw new Exception("original block child was not measured/arranged/rendered: "+ink+", "+child.RenderSize);
                var position=child.TransformToAncestor(page1.Visual).Transform(new Point(0,0));
                if(Math.Abs(position.X-18)>1||Math.Abs(position.Y-10)>1)throw new Exception("block margins/page coordinates: "+position);
                button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));if(clicks!=1)throw new Exception("original block button event routing");
                var hit=VisualTreeHelper.HitTest(page1.Visual,new Point(position.X+child.RenderSize.Width/2,position.Y+30));
                DependencyObject target=hit?.VisualHit;while(target!=null&&target!=child)target=VisualTreeHelper.GetParent(target);
                if(target!=child)throw new Exception("original block visual point hit must resolve inside its child");
                Console.WriteLine($"original block UI {mode}, nested={nested}: intact next-page deferral, child size {child.RenderSize}, position {position}, {ink} red pixels and routed button event PASS");
            }
            if(paginator.GetPage(2)!=DocumentPage.Missing||!paginator.IsPageCountValid||paginator.PageCount!=2||paginator.GetPageNumber(before.ContentStart)!=0||paginator.GetPageNumber(after.ContentStart)!=1||paginator.GetPageNumber(block.ContentStart)!=1)throw new Exception("block and surrounding content/page mapping");
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            using(var again=paginator.GetPage(1))if(Red(again.Visual)<1000)throw new Exception("disposed block page regeneration");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool empty in new[]{false,true}) {
            var child=empty?null:new Border{Background=Brushes.Red,Height=80};
            var block=empty?new BlockUIContainer():new BlockUIContainer(child);block.Margin=new Thickness(0);
            var document=new FlowDocument(block){PagePadding=new Thickness(2)};TextOptions.SetTextFormattingMode(document,mode);
            var paginator=((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,10);
            using(var page=paginator.GetPage(0))if(page==DocumentPage.Missing||(!empty&&Red(page.Visual,120)<1000)||page.Size!=new Size(400,10))throw new Exception("oversized/empty block must retain actual content at requested page size");
            if(paginator.GetPage(1)!=DocumentPage.Missing||!paginator.IsPageCountValid||paginator.PageCount!=1)throw new Exception("oversized/empty block fabricated a terminal continuation");
            Console.WriteLine($"original block UI {mode}, empty={empty}: tiny page completes without a phantom continuation PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var child=new Border{Background=Brushes.Red,Height=60};var block=new BlockUIContainer(child){Margin=new Thickness(8,6,12,10)};
            var before=new Paragraph(new Run("First\nSecond\nThird")){Margin=new Thickness(0),LineHeight=24,LineStackingStrategy=LineStackingStrategy.BlockLineHeight};
            var document=new FlowDocument(before){PagePadding=new Thickness(10),ColumnWidth=170,ColumnGap=20,IsColumnWidthFlexible=false};document.Blocks.Add(block);TextOptions.SetTextFormattingMode(document,mode);
            var paginator=((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,140);
            using(var page=paginator.GetPage(0)) {
                var position=child.TransformToAncestor(page.Visual).Transform(new Point(0,0));
                if(Red(page.Visual)<1000||Math.Abs(position.X-208)>1||Math.Abs(position.Y-10)>1||Math.Abs(child.RenderSize.Width-150)>1)throw new Exception("block must defer intact into the second column: "+position+", "+child.RenderSize);
            }
            if(paginator.GetPage(1)!=DocumentPage.Missing||paginator.PageCount!=1)throw new Exception("block column deferral must finish on one page");
            Console.WriteLine($"original block UI columns {mode}: intact second-column placement, source completion and red child raster PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var child=new Border{Background=Brushes.Red,Height=60};var block=new BlockUIContainer(child){Margin=new Thickness(0)};
            var document=new FlowDocument(block){PagePadding=new Thickness(10)};TextOptions.SetTextFormattingMode(document,mode);
            var viewer=new FlowDocumentScrollViewer{Document=document,IsToolBarVisible=false};
            var window=new Window{Content=viewer,Width=420,Height=240,ShowInTaskbar=false};window.Show();Pump(window);
            int before=Red(viewer);child.Height=120;Pump(window);int after=Red(viewer);
            if(before<1000||after<=before*1.5||child.RenderSize.Height!=120)throw new Exception("bottomless block resize must update original child raster/extent: "+before+" -> "+after);
            Console.WriteLine($"original bottomless block UI {mode}: child resize 60->120, red pixels {before}->{after} PASS");window.Close();
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original block UI finite/bottomless documents and dispatcher cleanup PASS");
    }
}
