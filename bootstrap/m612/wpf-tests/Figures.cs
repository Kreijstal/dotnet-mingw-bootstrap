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
        var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(int scenario in Enumerable.Range(0,10)) {
            var lead=new Run(scenario==4?"prefix zero\nprefix one\nprefix two\nprefix three\nprefix four\nprefix five\nprefix six\nprefix seven\nAnchor ":"Anchor ");
            var content=new Run("figure one\nfigure two");
            var body=new Run(string.Join(" ",Enumerable.Range(0,70).Select(i=>$"body{i:D3}")));
            var figure=new Figure(new Paragraph(content){Margin=new Thickness(0),LineHeight=24,Foreground=Brushes.Blue}) {
                Width=scenario==8?new FigureLength(1,FigureUnitType.Auto):new FigureLength(scenario==5?100:120),Height=scenario==8?new FigureLength(1,FigureUnitType.Auto):new FigureLength(80),
                HorizontalAnchor=scenario==1?FigureHorizontalAnchor.ColumnRight:scenario==2?FigureHorizontalAnchor.ColumnCenter:scenario==6?FigureHorizontalAnchor.PageLeft:FigureHorizontalAnchor.ColumnLeft,
                VerticalAnchor=scenario==7?FigureVerticalAnchor.PageBottom:FigureVerticalAnchor.ParagraphTop,
                WrapDirection=scenario==2?WrapDirection.Right:WrapDirection.Both,
                HorizontalOffset=scenario==3?15:0,VerticalOffset=scenario==3?7:0,
                CanDelayPlacement=true,Margin=new Thickness(0),Padding=new Thickness(4),Background=Brushes.Red
            };
            if(scenario==9){var paragraph=(Paragraph)figure.Blocks.FirstBlock;figure.Blocks.Remove(paragraph);figure.Blocks.Add(new Section(paragraph){Margin=new Thickness(0)});}
            var host=new Paragraph(lead){Margin=new Thickness(0),LineHeight=24};host.Inlines.Add(figure);host.Inlines.Add(body);
            var document=new FlowDocument(host){FontFamily=new FontFamily("Segoe UI"),FontSize=18,Foreground=Brushes.Black,PagePadding=new Thickness(10),ColumnWidth=scenario==5?180:double.PositiveInfinity,ColumnGap=20};
            TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();var figurePages=new List<int>();int pages=0;
            for(;pages<20;++pages) {
                using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;
                var pixels=Raster(page.Visual);var red=new List<(int X,int Y)>();int blue=0;
                for(int y=0;y<220;++y)for(int x=0;x<400;++x){int i=(y*400+x)*4;if(Red(pixels,i))red.Add((x,y));if(pixels[i+3]>150&&pixels[i]>180&&pixels[i+1]<70&&pixels[i+2]<70)++blue;}
                if(red.Count>0) {
                    figurePages.Add(pages);if(red.Count<3000||blue<30)throw new Exception($"figure background/content missing: {red.Count}/{blue}");
                    int minX=red.Min(p=>p.X),maxX=red.Max(p=>p.X),minY=red.Min(p=>p.Y),maxY=red.Max(p=>p.Y);
                    int expectedX=scenario==1?270:scenario==2?140:scenario==3?25:scenario==6?0:10;
                    if(minX!=expectedX)throw new Exception($"figure horizontal anchor/offset {scenario}: {minX} != {expectedX}");
                    if(scenario==3&&minY<17||scenario==7&&maxY!=219)throw new Exception("figure vertical anchor/offset");
                    int overlap=0;
                    for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x){int i=(y*400+x)*4;if(pixels[i+3]>150&&pixels[i]<70&&pixels[i+1]<70&&pixels[i+2]<70)++overlap;}
                    if(overlap!=0)throw new Exception("host glyphs overlap figure: "+overlap);
                }
                var view=((IServiceProvider)page).GetService(viewType);
                foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)) {
                    var t=segment.GetType();var flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
                    ranges.Add((pages,(TextPointer)t.GetProperty("Start",flags).GetValue(segment),(TextPointer)t.GetProperty("End",flags).GetValue(segment)));
                }
                GC.Collect(2,GCCollectionMode.Forced,true,true);
            }
            if(pages<1||pages>=20||figurePages.Count!=1||!paginator.IsPageCountValid)throw new Exception($"figure pagination failed {scenario}: pages={pages}, figures={figurePages.Count}");
            foreach(var run in new[]{lead,content,body})for(int at=0;at<run.Text.Length;++at) {
                var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();
                if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception($"figure source ownership {scenario}: {at}, owners={owners.Length}");
                if(run==content&&owners[0].Page!=figurePages[0])throw new Exception("figure source and raster page differ");
            }
            if(scenario==4&&figurePages[0]==0)throw new Exception("late figure must defer intact");
            using(var page=paginator.GetPage(figurePages[0])){var pixels=Raster(page.Visual);int red=0;for(int i=0;i<pixels.Length;i+=4)if(Red(pixels,i))++red;if(red<3000)throw new Exception("disposed figure page must regenerate");}
            Console.WriteLine($"original figure {mode}, scenario={scenario}: {pages} pages, figure page={figurePages[0]}, {lead.Text.Length+content.Text.Length+body.Text.Length} characters covered once, anchor/offset/wrapping/source mapping/regeneration/GC PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original figure callbacks, visuals and dispatcher cleanup PASS");
    }
}
