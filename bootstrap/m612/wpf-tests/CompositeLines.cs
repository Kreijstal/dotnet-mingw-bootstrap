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
        var image=new RenderTargetBitmap(400,220,96,96,PixelFormats.Pbgra32);
        image.Render(visual);var pixels=new byte[400*220*4];image.CopyPixels(pixels,1600,0);return pixels;
    }
    static bool Red(byte[] p,int i)=>p[i+3]>150&&p[i+2]>200&&p[i]<60&&p[i+1]<60;
    [STAThread]
    static void Main() {
        const BindingFlags I=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
        var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
        foreach(int scenario in new[]{0,1,2,3,4}) {
            var lead=new Run(scenario==1?"prefix zero prefix one prefix two prefix three prefix four Anchor ":"Anchor ");
            var content=new Run("figure one\nfigure two");
            var body=new Run(string.Join(" ",Enumerable.Range(0,80).Select(i=>$"word{i:D3}"))){FontSize=scenario==3?26:18};
            var figure=new Figure(new Paragraph(content){Margin=new Thickness(0),Foreground=Brushes.Blue}){Width=new FigureLength(scenario==4?80:120),Height=new FigureLength(scenario==4?120:100),HorizontalAnchor=FigureHorizontalAnchor.ColumnCenter,VerticalAnchor=FigureVerticalAnchor.ParagraphTop,WrapDirection=WrapDirection.Both,Margin=new Thickness(0),Padding=new Thickness(4),Background=Brushes.Red,CanDelayPlacement=true};
            if(scenario==2){var original=figure.Blocks.FirstBlock;figure.Blocks.Remove(original);figure.Blocks.Add(new Section(original){Margin=new Thickness(0)});}
            var host=new Paragraph(lead){Margin=new Thickness(0)};host.Inlines.Add(figure);host.Inlines.Add(body);
            var document=new FlowDocument(host){FontFamily=new FontFamily("Segoe UI"),FontSize=18,Foreground=Brushes.Black,FlowDirection=direction,PagePadding=new Thickness(10),ColumnWidth=scenario==4?180:double.PositiveInfinity,ColumnGap=20};TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,figurePage=-1,pairs=0,carets=0;
            for(;pages<30;++pages) {
                using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;
                var pixels=Raster(page.Visual);var red=new List<(int X,int Y)>();for(int y=0;y<220;++y)for(int x=0;x<400;++x)if(Red(pixels,(y*400+x)*4))red.Add((x,y));
                int minX=-1,maxX=-1,minY=-1,maxY=-1;
                if(red.Count>0){if(figurePage>=0)throw new Exception("intact centered figure has multiple source owners");figurePage=pages;minX=red.Min(p=>p.X);maxX=red.Max(p=>p.X);minY=red.Min(p=>p.Y);maxY=red.Max(p=>p.Y);
                    for(int y=minY;y<=maxY;++y){int left=0,right=0;for(int x=0;x<400;++x){int at=(y*400+x)*4;bool dark=pixels[at+3]>150&&pixels[at]<70&&pixels[at+1]<70&&pixels[at+2]<70;if(dark){if(x>=(scenario==4?minX-50:10)&&x<minX)++left;if(x>maxX&&x<=(scenario==4?maxX+50:390))++right;if(x>=minX&&x<=maxX)throw new Exception("host glyph overlaps centered figure");}}if(left>3&&right>3)++pairs;}
                }
                var view=((IServiceProvider)page).GetService(viewType);var local=new List<(TextPointer Start,TextPointer End)>();
                foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();var start=(TextPointer)t.GetProperty("Start",I).GetValue(segment);var end=(TextPointer)t.GetProperty("End",I).GetValue(segment);ranges.Add((pages,start,end));local.Add((start,end));}
                for(int at=0;at<Math.Min(100,body.Text.Length);++at){var position=body.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);if(!local.Any(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0))continue;var rect=(Rect)viewType.GetMethod("GetRectangleFromTextPosition").Invoke(view,new object[]{position});if(rect.IsEmpty||double.IsNaN(rect.X)||rect.Height<=0)throw new Exception("composite caret rectangle missing");if(red.Count>0&&rect.Left>=minX&&rect.Left<=maxX&&rect.Top<maxY&&rect.Bottom>minY)throw new Exception("composite caret maps into obstacle");var hit=viewType.GetMethod("GetTextPositionFromPoint").Invoke(view,new object[]{new Point(rect.Left+1,rect.Top+rect.Height/2),true});if(hit==null)throw new Exception("composite point mapping failed");++carets;}
                GC.Collect(2,GCCollectionMode.Forced,true,true);if(!pixels.SequenceEqual(Raster(page.Visual)))throw new Exception("composite raster changes after GC");
            }
            if(pages<2||pages>=30||figurePage<0||pairs<3||carets<20||!paginator.IsPageCountValid)throw new Exception($"composite pagination/render coverage: {pages}/{figurePage}/{pairs}/{carets}");
            foreach(var run in new[]{lead,content,body})for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception($"composite source ownership run={(run==body?"body":run==lead?"lead":"figure")}, at={at}, owners={string.Join(",",owners.Select(o=>o.Page))}, actualPage={paginator.GetPageNumber(position)}");if(run==content&&owners[0].Page!=figurePage)throw new Exception("figure source/render page disagreement");}
            using(var page=paginator.GetPage(figurePage)){var raster=Raster(page.Visual);int red=0;for(int at=0;at<raster.Length;at+=4)if(Red(raster,at))++red;if(red<3000)throw new Exception("composite disposed page regeneration failed");}
            Console.WriteLine($"original composite lines {mode} {direction}, scenario={scenario}: {pages} pages, {pairs} pixel rows with actual glyphs on both obstacle sides, {carets} caret/point queries, {lead.Text.Length+content.Text.Length+body.Text.Length} chars covered once, original source/page mapping/regeneration/GC PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original composite-line rendering, geometry, pagination and dispatcher cleanup PASS");
    }
}
