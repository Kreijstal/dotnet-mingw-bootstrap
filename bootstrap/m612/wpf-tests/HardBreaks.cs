using System;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
class Check {
    static byte[] Raster(Visual visual) {
        var image=new RenderTargetBitmap(400,220,96,96,PixelFormats.Pbgra32);image.Render(visual);
        var pixels=new byte[400*220*4];image.CopyPixels(pixels,400*4,0);return pixels;
    }
    static (int MinX,int MaxX,int MinY,int MaxY,int Pixels) Bounds(byte[] bytes,int channel) {
        int x0=400,x1=-1,y0=220,y1=-1,count=0;
        for(int y=0;y<220;++y)for(int x=0;x<400;++x) {
            int at=(y*400+x)*4;if(bytes[at+3]<100)continue;
            bool hit=channel<0?bytes[at]<100&&bytes[at+1]<100&&bytes[at+2]<100&&Math.Abs(bytes[at]-bytes[at+1])<5&&Math.Abs(bytes[at]-bytes[at+2])<5:
                bytes[at+channel]>50 && bytes[at+channel]>4*bytes[at+(channel+1)%3] && bytes[at+channel]>4*bytes[at+(channel+2)%3];
            if(hit){++count;x0=Math.Min(x0,x);x1=Math.Max(x1,x);y0=Math.Min(y0,y);y1=Math.Max(y1,y);}
        }
        return (x0,x1,y0,y1,count);
    }
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool nested in new[]{false,true}) {
            var runs=new[]{new Run("First section."),new Run("Next column."),new Run("Next page."),new Run("Following paragraph.")};
            var first=new Paragraph(runs[0]){Margin=new Thickness(0),Foreground=Brushes.Red,BreakPageBefore=true,BreakColumnBefore=true};
            var second=new Paragraph(runs[1]){Margin=new Thickness(0),Foreground=Brushes.Blue,BreakColumnBefore=true};
            var third=new Paragraph(runs[2]){Margin=new Thickness(0),Foreground=Brushes.Green,BreakPageBefore=true};
            var fourth=new Paragraph(runs[3]){Margin=new Thickness(0),Foreground=Brushes.Black};
            Block root=first;Section section=null;if(nested){section=new Section(first){Margin=new Thickness(0)};root=section;}
            var document=new FlowDocument(root){FontFamily=new FontFamily("Segoe UI"),FontSize=20,PagePadding=new Thickness(10),ColumnWidth=145,ColumnGap=30,IsColumnWidthFlexible=false};
            var blocks=nested?section.Blocks:document.Blocks;blocks.Add(second);blocks.Add(third);blocks.Add(fourth);TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);
            using var page0=paginator.GetPage(0);using var page1=paginator.GetPage(1);
            if(page0==DocumentPage.Missing||page1==DocumentPage.Missing||paginator.GetPage(2)!=DocumentPage.Missing||!paginator.IsPageCountValid||paginator.PageCount!=2)throw new Exception("explicit breaks created an empty/repeated page or lost content");
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            var zero=Raster(page0.Visual);var one=Raster(page1.Visual);
            var red=Bounds(zero,2);var blue=Bounds(zero,0);var green0=Bounds(zero,1);var green=Bounds(one,1);var black=Bounds(one,-1);
            if(red.Pixels<100||blue.Pixels<100||green.Pixels<100||black.Pixels<100||green0.Pixels!=0||Bounds(one,2).Pixels!=0||Bounds(one,0).Pixels!=0)throw new Exception("paragraph colors appeared on the wrong page or failed to render");
            if(red.MinX<10||red.MaxX>=155||blue.MinX<185||blue.MaxX>=330||green.MinX<10||green.MaxX>=155||black.MinY<=green.MaxY)throw new Exception($"explicit paragraph placement: red={red}, blue={blue}, green={green}, black={black}");
            int characters=0;
            for(int r=0;r<runs.Length;++r)for(int at=0;at<runs[r].Text.Length;++at) {
                if(paginator.GetPageNumber(runs[r].ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward))!=(r<2?0:1))throw new Exception("explicit-break source character maps to the wrong page");++characters;
            }
            Console.WriteLine($"original explicit breaks {mode}, nested={nested}: first-block suppression, red/blue separate columns, green/black next page, {characters} character/page mappings and compacting GC PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original explicit paragraph page/column breaks and dispatcher cleanup PASS");
    }
}
