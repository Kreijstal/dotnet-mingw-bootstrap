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
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool opposite in new[]{false,true}) {
            var firstRun=new Run("First floating content");var secondRun=new Run("Second floating content");var body=new Run(string.Join(" ",Enumerable.Range(0,100).Select(i=>$"multi{i:D3}")));
            Floater Make(Run run,Brush background,double width,HorizontalAlignment alignment)=>new Floater(new Paragraph(run){Foreground=Brushes.Blue,Margin=new Thickness(0)}){Background=background,Padding=new Thickness(4),Margin=new Thickness(0),Width=width,HorizontalAlignment=alignment};
            var paragraph=new Paragraph(Make(firstRun,Brushes.Red,100,HorizontalAlignment.Left)){Margin=new Thickness(0)};paragraph.Inlines.Add(Make(secondRun,Brushes.Lime,140,opposite?HorizontalAlignment.Right:HorizontalAlignment.Left));paragraph.Inlines.Add(body);
            var document=new FlowDocument(paragraph){FontFamily=new FontFamily("Segoe UI"),FontSize=18,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,260);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0;
            for(;pages<20;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;var pixels=Raster(page.Visual);
                var red=new List<(int X,int Y)>();var green=new List<(int X,int Y)>();
                for(int y=0;y<220;++y)for(int x=0;x<400;++x){int i=(y*400+x)*4;if(pixels[i+3]<150)continue;if(pixels[i+2]>200&&pixels[i]<60&&pixels[i+1]<60)red.Add((x,y));if(pixels[i+1]>200&&pixels[i]<60&&pixels[i+2]<60)green.Add((x,y));}
                if(pages==0){if(red.Count<1000||green.Count<1000)throw new Exception("both original floaters must render");int redBottom=red.Max(p=>p.Y),greenTop=green.Min(p=>p.Y);
                    if(opposite){if(red.Min(p=>p.X)!=10||green.Max(p=>p.X)!=389||red.Min(p=>p.Y)!=greenTop)throw new Exception("opposite edge floaters must share a nonoverlapping row");}
                    else if(red.Min(p=>p.X)!=10||green.Min(p=>p.X)!=10||greenTop<=redBottom)throw new Exception("same-edge floaters must retain alignment without overlap");
                    int overlap=0,host=0;foreach(var points in new[]{red,green}){int left=points.Min(p=>p.X),right=points.Max(p=>p.X),top=points.Min(p=>p.Y),bottom=points.Max(p=>p.Y);for(int y=top;y<=bottom;++y)for(int x=0;x<400;++x){int i=(y*400+x)*4;if(pixels[i+3]>150&&pixels[i]<70&&pixels[i+1]<70&&pixels[i+2]<70){if(x>=left&&x<=right)++overlap;else ++host;}}}if(overlap!=0||host<100)throw new Exception("host text must flow around both actual obstacle extents");
                    Console.WriteLine($"original multiple floaters {mode}, opposite={opposite}: red bottom={redBottom}, green top={greenTop}, zero rectangle/host overlap, {host} host glyph pixels beside obstacles PASS");
                }else if(red.Count!=0||green.Count!=0)throw new Exception("multiple intact floaters must not repeat after continuation");
                var view=((IServiceProvider)page).GetService(viewType);foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();var flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;ranges.Add((pages,(TextPointer)t.GetProperty("Start",flags).GetValue(segment),(TextPointer)t.GetProperty("End",flags).GetValue(segment)));}GC.Collect(2,GCCollectionMode.Forced,true,true);
            }
            if(pages<2||pages>=20||!paginator.IsPageCountValid)throw new Exception("multiple floater pagination must complete");
            foreach(var run in new[]{firstRun,secondRun,body})for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("multiple floater exact source/page ownership at "+at);}
            using(var first=paginator.GetPage(0)){var bytes=Raster(first.Visual);int red=0,green=0;for(int i=0;i<bytes.Length;i+=4){if(bytes[i+3]<150)continue;if(bytes[i+2]>200&&bytes[i]<60&&bytes[i+1]<60)++red;if(bytes[i+1]>200&&bytes[i]<60&&bytes[i+2]<60)++green;}if(red<1000||green<1000)throw new Exception("multiple floater page must regenerate both original objects");}
            Console.WriteLine($"original multiple floaters {mode}, opposite={opposite}: {pages} pages, {firstRun.Text.Length+secondRun.Text.Length+body.Text.Length} characters covered exactly once, page mapping/regeneration/GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool multiple in new[]{false,true}) {
            var prefix=new Run("prefix zero\nprefix one\nprefix two\nprefix three\nprefix four\nprefix five");
            var firstRun=new Run("float one\nfloat two\nfloat three");var secondRun=new Run("next one\nnext two\nnext three");var body=new Run(string.Join(" ",Enumerable.Range(0,100).Select(i=>$"defer{i:D3}")));
            Floater Make(Run run,Brush color)=>new Floater(new Paragraph(run){Margin=new Thickness(0),Foreground=Brushes.Blue,LineHeight=24}){Width=120,HorizontalAlignment=HorizontalAlignment.Left,Padding=new Thickness(4),Margin=new Thickness(0),Background=color};
            var host=new Paragraph(Make(firstRun,Brushes.Red)){Margin=new Thickness(0)};if(multiple)host.Inlines.Add(Make(secondRun,Brushes.Lime));host.Inlines.Add(body);
            var document=new FlowDocument(new Paragraph(prefix){Margin=new Thickness(0),LineHeight=24}){FontFamily=new FontFamily("Segoe UI"),FontSize=18,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};document.Blocks.Add(host);TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0;
            for(;pages<20;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;var bytes=Raster(page.Visual);int red=0,green=0;
                for(int i=0;i<bytes.Length;i+=4){if(bytes[i+3]<150)continue;if(bytes[i+2]>200&&bytes[i]<60&&bytes[i+1]<60)++red;if(bytes[i+1]>200&&bytes[i]<60&&bytes[i+2]<60)++green;}
                if(pages==1){if(red<1000||(multiple&&green<1000))throw new Exception("deferred intact floaters must render on fresh page");}else if(red!=0||green!=0)throw new Exception("deferred floater must not render on occupied or later pages");
                var view=((IServiceProvider)page).GetService(viewType);foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();var flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;ranges.Add((pages,(TextPointer)t.GetProperty("Start",flags).GetValue(segment),(TextPointer)t.GetProperty("End",flags).GetValue(segment)));}GC.Collect(2,GCCollectionMode.Forced,true,true);
            }
            if(pages<3||pages>=20||!paginator.IsPageCountValid)throw new Exception("deferred floater pagination must complete");
            var runs=multiple?new[]{prefix,firstRun,secondRun,body}:new[]{prefix,firstRun,body};
            foreach(var run in runs)for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("deferred floater exact source ownership at "+at);if((run==firstRun||run==secondRun)&&owners[0].Page!=1)throw new Exception("intact floating source belongs to next page");}
            using(var page=paginator.GetPage(1)){var bytes=Raster(page.Visual);int red=0,green=0;for(int i=0;i<bytes.Length;i+=4){if(bytes[i+3]<150)continue;if(bytes[i+2]>200&&bytes[i]<60&&bytes[i+1]<60)++red;if(bytes[i+1]>200&&bytes[i]<60&&bytes[i+2]<60)++green;}if(red<1000||(multiple&&green<1000))throw new Exception("deferred page regeneration must retain original objects");}
            Console.WriteLine($"original deferred floaters {mode}, multiple={multiple}: {pages} pages, {runs.Sum(r=>r.Text.Length)} characters covered exactly once, intact next-page source/raster, regeneration and GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(int scenario in new[]{0,1,2}) {
            string lead=scenario==0?"Inline anchor ":scenario==1?"Words before float at inline position ":"prefix zero\nprefix one\nprefix two\nprefix three\nprefix four\nprefix five\nInline anchor ";
            var prefix=new Run(lead);var floating=new Run("inline one\ninline two\ninline three");var body=new Run(string.Join(" ",Enumerable.Range(0,100).Select(i=>$"inline{i:D3}")));
            var floater=new Floater(new Paragraph(floating){Margin=new Thickness(0),Foreground=Brushes.Blue,LineHeight=24}){Width=120,HorizontalAlignment=HorizontalAlignment.Left,Padding=new Thickness(4),Margin=new Thickness(0),Background=Brushes.Red};
            var host=new Paragraph(prefix){Margin=new Thickness(0),LineHeight=24};host.Inlines.Add(floater);host.Inlines.Add(body);
            var document=new FlowDocument(host){FontFamily=new FontFamily("Segoe UI"),FontSize=18,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();var floatingPages=new List<int>();int pages=0;
            for(;pages<20;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;var bytes=Raster(page.Visual);var red=new List<(int X,int Y)>();
                for(int y=0;y<220;++y)for(int x=0;x<400;++x){int i=(y*400+x)*4;if(bytes[i+3]>150&&bytes[i+2]>200&&bytes[i]<60&&bytes[i+1]<60)red.Add((x,y));}
                if(red.Count>0){if(red.Count<1000||red.Min(p=>p.X)!=10)throw new Exception("inline floater content/placement missing");floatingPages.Add(pages);int overlap=0,beside=0;int top=red.Min(p=>p.Y),bottom=red.Max(p=>p.Y);for(int y=top;y<=bottom;++y)for(int x=0;x<400;++x){int i=(y*400+x)*4;if(bytes[i+3]>150&&bytes[i]<70&&bytes[i+1]<70&&bytes[i+2]<70){if(x<130)++overlap;else ++beside;}}if(overlap!=0||beside<100)throw new Exception("inline host glyphs must flow outside the floating rectangle");}
                var view=((IServiceProvider)page).GetService(viewType);foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();var flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;ranges.Add((pages,(TextPointer)t.GetProperty("Start",flags).GetValue(segment),(TextPointer)t.GetProperty("End",flags).GetValue(segment)));}GC.Collect(2,GCCollectionMode.Forced,true,true);
            }
            if(pages<2||pages>=20||floatingPages.Count!=1||!paginator.IsPageCountValid)throw new Exception("inline floating object pagination must finish with one intact object");
            foreach(var run in new[]{prefix,floating,body})for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("inline floater exact source ownership at "+at);if(run==floating&&owners[0].Page!=floatingPages[0])throw new Exception("inline floating source and raster must belong to same page");}
            if(scenario==2&&floatingPages[0]==0)throw new Exception("late inline floater must defer after preceding paragraph lines");
            using(var page=paginator.GetPage(floatingPages[0])){var bytes=Raster(page.Visual);int red=0;for(int i=0;i<bytes.Length;i+=4)if(bytes[i+3]>150&&bytes[i+2]>200&&bytes[i]<60&&bytes[i+1]<60)++red;if(red<1000)throw new Exception("inline floating page must regenerate");}
            Console.WriteLine($"original inline floater {mode}, scenario={scenario}: {pages} pages, floating page={floatingPages[0]}, {prefix.Text.Length+floating.Text.Length+body.Text.Length} characters covered once, zero host overlap, source mapping/regeneration/GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(int scenario in new[]{0,1,2,3,4,5}) {
            var floating=new Run(string.Join("\n",Enumerable.Range(0,30).Select(i=>$"float{i:D2}")));var body=new Run(scenario==4?string.Join(" ",Enumerable.Range(0,200).Select(i=>$"longhost{i:D3}")):"Short host text remains visible beside the floating content.");var prefix=new Run(scenario==2?"prefix zero\nprefix one\nprefix two\nprefix three\nprefix four\nprefix five":scenario==1?"Inline split anchor ":"");
            var floater=new Floater(new Paragraph(floating){Margin=new Thickness(0),LineHeight=24,Foreground=Brushes.Blue}){Width=120,HorizontalAlignment=HorizontalAlignment.Left,Padding=new Thickness(4),Margin=new Thickness(0),Background=Brushes.Red};
            var other=new Run(string.Join("\n",Enumerable.Range(0,20).Select(i=>$"other{i:D2}")));
            var host=new Paragraph(){Margin=new Thickness(0)};if(scenario==1)host.Inlines.Add(prefix);host.Inlines.Add(floater);if(scenario==5)host.Inlines.Add(new Floater(new Paragraph(other){Margin=new Thickness(0),LineHeight=24,Foreground=Brushes.Blue}){Width=120,HorizontalAlignment=HorizontalAlignment.Right,Padding=new Thickness(4),Margin=new Thickness(0),Background=Brushes.Lime});host.Inlines.Add(body);
            var document=new FlowDocument(){FontFamily=new FontFamily("Segoe UI"),FontSize=18,PagePadding=new Thickness(10),ColumnWidth=scenario==3?160:double.PositiveInfinity,ColumnGap=20};if(scenario==2)document.Blocks.Add(new Paragraph(prefix){Margin=new Thickness(0),LineHeight=24});document.Blocks.Add(host);TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);
            var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();var floatPages=new List<int>();int pages=0,blueTotal=0,floatChunks=0;bool secondColumn=false;int greenPages=0;
            for(;pages<20;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;var bytes=Raster(page.Visual);int red=0,blue=0,leftRed=0,rightRed=0,green=0;for(int i=0;i<bytes.Length;i+=4){if(bytes[i+3]<150)continue;if(bytes[i+2]>200&&bytes[i]<60&&bytes[i+1]<60){++red;if((i/4)%400<200)++leftRed;else ++rightRed;}if(bytes[i]>180&&bytes[i+2]<70&&bytes[i+1]<70)++blue;if(bytes[i+1]>200&&bytes[i]<60&&bytes[i+2]<60)++green;}if(green>1000)++greenPages;
                if(red>0){if(red<1000||blue<30)throw new Exception("split chunk must contain actual original floating text/background");floatPages.Add(pages);blueTotal+=blue;if(leftRed>1000)++floatChunks;if(rightRed>1000){++floatChunks;secondColumn=true;}}
                var view=((IServiceProvider)page).GetService(viewType);foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();var flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;ranges.Add((pages,(TextPointer)t.GetProperty("Start",flags).GetValue(segment),(TextPointer)t.GetProperty("End",flags).GetValue(segment)));}GC.Collect(2,GCCollectionMode.Forced,true,true);
            }
            if(pages>=20||floatChunks<3||!paginator.IsPageCountValid||floatPages[0]!=(scenario==2?1:0))throw new Exception("oversized original floater must split and complete, including intact first-page deferral");
            if(scenario==3&&!secondColumn)throw new Exception("split floater must continue into the second source column");
            if(scenario==5&&greenPages<2)throw new Exception("both independent split floaters must render actual chunks");
            var runs=scenario==5?new[]{floating,other,body}:scenario==0||scenario==3||scenario==4?new[]{floating,body}:new[]{prefix,floating,body};var ownersByRun=new HashSet<int>();
            foreach(var run in runs)for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("split floating/host character coverage and page mapping: "+at+", owners="+owners.Length);if(run==floating)ownersByRun.Add(owners[0].Page);}
            if(!ownersByRun.SetEquals(floatPages))throw new Exception("split floating source pages must match rendered chunk pages");
            foreach(int n in floatPages){using var page=paginator.GetPage(n);var bytes=Raster(page.Visual);int blue=0;for(int i=0;i<bytes.Length;i+=4)if(bytes[i+3]>150&&bytes[i]>180&&bytes[i+2]<70&&bytes[i+1]<70)++blue;if(blue<30)throw new Exception("disposed split chunk must regenerate original text");}
            Console.WriteLine($"original split floater {mode}, scenario={scenario}: {pages} pages, {floatChunks} actual floating chunks, {runs.Sum(r=>r.Text.Length)} characters covered exactly once, {blueTotal} blue glyph pixels, source/page mapping/regeneration/GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool inline in new[]{false,true}) {
            var floatingRun=new Run("Original scrolling floater");var bodyRun=new Run(string.Join(" ",Enumerable.Range(0,130).Select(i=>$"scroll{i:D3}")));
            var floater=new Floater(new Paragraph(floatingRun){Margin=new Thickness(0),Foreground=Brushes.Blue}){Width=120,HorizontalAlignment=HorizontalAlignment.Left,Margin=new Thickness(0),Padding=new Thickness(4),Background=Brushes.Red};
            var paragraph=new Paragraph(){Margin=new Thickness(0)};if(inline)paragraph.Inlines.Add(new Run("Inline scrolling anchor "));paragraph.Inlines.Add(floater);paragraph.Inlines.Add(bodyRun);var document=new FlowDocument(paragraph){FontSize=18,FontFamily=new FontFamily("Segoe UI"),PagePadding=new Thickness(10)};TextOptions.SetTextFormattingMode(document,mode);
            var viewer=new FlowDocumentScrollViewer{Document=document,IsToolBarVisible=false};var window=new Window{Content=viewer,Width=420,Height=260,ShowInTaskbar=false};window.Show();Pump(window);
            var scroll=(ScrollViewer)viewer.Template.FindName("PART_ContentHost",viewer);double oldHeight=scroll.ExtentHeight;var before=Raster(viewer);
            floatingRun.Text+=" Additional original floater text changes the obstacle height and the host line widths.";Pump(window);var after=Raster(viewer);
            if(before.SequenceEqual(after)||scroll.ExtentHeight<oldHeight||scroll.ExtentHeight<=scroll.ViewportHeight)throw new Exception("edited scrolling floater must update glyphs/obstacle/extent");
            scroll.ScrollToEnd();Pump(window);if(scroll.VerticalOffset<=0)throw new Exception("scrolling floater host must retain its full text extent");window.Close();
            Console.WriteLine($"original scrolling floater {mode}, inline={inline}: edited content changes obstacle/host raster, full extent {oldHeight:F1}->{scroll.ExtentHeight:F1}, scrolling and cleanup PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original rectangular floater wrapping and dispatcher cleanup PASS");
    }
}
