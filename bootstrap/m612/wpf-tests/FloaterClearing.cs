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
 const BindingFlags I=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
 static byte[] Raster(Visual v){var image=new RenderTargetBitmap(400,260,96,96,PixelFormats.Pbgra32);image.Render(v);var p=new byte[400*260*4];image.CopyPixels(p,1600,0);return p;}
 static Rect Bounds(byte[] p,int color){int minX=400,maxX=-1,minY=260,maxY=-1;for(int y=0;y<260;++y)for(int x=0;x<400;++x){int at=(y*400+x)*4;if(p[at+3]<150)continue;bool match=color==0?p[at+2]>200&&p[at]<60&&p[at+1]<60:color==1?p[at+1]>180&&p[at]<60&&p[at+2]<60:p[at]>200&&p[at+1]<60&&p[at+2]<60;if(match){minX=Math.Min(minX,x);maxX=Math.Max(maxX,x);minY=Math.Min(minY,y);maxY=Math.Max(maxY,y);}}return maxX<0?Rect.Empty:new Rect(minX,minY,maxX-minX+1,maxY-minY+1);}
 static Floater Make(Brush color,double w,double h,HorizontalAlignment alignment)=>new Floater(new BlockUIContainer(new Border{Width=w,Height=h,Background=color}){Margin=new Thickness(0)}){Width=w,HorizontalAlignment=alignment,Margin=new Thickness(0),Padding=new Thickness(0)};
 static void Pump(FrameworkElement element){element.UpdateLayout();var frame=new DispatcherFrame();Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle,new Action(()=>frame.Continue=false));Dispatcher.PushFrame(frame);element.UpdateLayout();}
 [STAThread]static void Main(){
  var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})foreach(var clear in new[]{WrapDirection.None,WrapDirection.Left,WrapDirection.Right,WrapDirection.Both})foreach(bool prefix in new[]{false,true})foreach(bool wideLeft in new[]{false,true}) {
   var red=Make(Brushes.Red,wideLeft?220:90,wideLeft?160:80,HorizontalAlignment.Left);var green=Make(Brushes.Lime,90,140,HorizontalAlignment.Right);var blue=Make(Brushes.Blue,70,60,wideLeft?HorizontalAlignment.Right:HorizontalAlignment.Center);blue.SetValue(Block.ClearFloatersProperty,clear);
   var body=new Run(string.Join(" ",Enumerable.Range(0,100).Select(i=>$"body{i:D3}")));var host=new Paragraph(red){Margin=new Thickness(0)};host.Inlines.Add(green);host.Inlines.Add(blue);host.Inlines.Add(body);
   var doc=new FlowDocument(host){FontFamily=new FontFamily("Segoe UI"),FontSize=18,Foreground=Brushes.Black,FlowDirection=direction,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};if(prefix)doc.Blocks.InsertBefore(host,new BlockUIContainer(new Border{Width=100,Height=80,Background=Brushes.Gray}){Margin=new Thickness(0)});TextOptions.SetTextFormattingMode(doc,mode);
   var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)doc).DocumentPaginator;paginator.PageSize=new Size(400,260);var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,bluePage=-1;Rect firstRed=Rect.Empty,firstGreen=Rect.Empty,firstBlue=Rect.Empty;
   for(;pages<30;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;var pixels=Raster(page.Visual);var r=Bounds(pixels,0);var g=Bounds(pixels,1);var b=Bounds(pixels,2);
    if(!b.IsEmpty){firstRed=r;firstGreen=g;firstBlue=b;if(r.IsEmpty||g.IsEmpty||b.IsEmpty)throw new Exception($"colored floaters absent: {r}/{g}/{b}");double expected=wideLeft?(clear==WrapDirection.Left||clear==WrapDirection.Both?Math.Max(r.Bottom,g.Bottom):g.Bottom):clear==WrapDirection.None?Math.Min(r.Top,g.Top):clear==WrapDirection.Left?r.Bottom:clear==WrapDirection.Right?g.Bottom:Math.Max(r.Bottom,g.Bottom);if(Math.Abs(b.Top-expected)>1)throw new Exception($"clear placement {clear}: blue {b.Top} vs {expected}, red={r}, green={g}");if((wideLeft?(direction==FlowDirection.LeftToRight?Math.Abs(b.Right-g.Right)>1:Math.Abs(b.Left-g.Left)>1):Math.Abs(b.Left-165)>1)||Math.Abs(b.Width-70)>1)throw new Exception("cleared centered floater loses requested horizontal geometry");}
    if(b.IsEmpty&&(!r.IsEmpty||!g.IsEmpty))throw new Exception("cleared intact group leaves speculative earlier floaters on old page");
    if(!b.IsEmpty){if(bluePage>=0)throw new Exception("cleared floater repeats on later pages");bluePage=pages;}
    object view=((IServiceProvider)page).GetService(viewType);foreach(object segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();ranges.Add((pages,(TextPointer)t.GetProperty("Start",I).GetValue(segment),(TextPointer)t.GetProperty("End",I).GetValue(segment)));}
    GC.Collect(2,GCCollectionMode.Forced,true,true);if(!pixels.SequenceEqual(Raster(page.Visual)))throw new Exception("cleared floater raster changed across GC");
   }
   if(pages<2||pages>=30||bluePage!=(prefix&&(wideLeft||clear==WrapDirection.Right||clear==WrapDirection.Both)?1:0)||!paginator.IsPageCountValid)throw new Exception("cleared floater pagination progress");
   for(int i=0;i<body.Text.Length;++i){var position=body.ContentStart.GetPositionAtOffset(i,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("clear property loses host source/page ownership");}
   using(var page=paginator.GetPage(bluePage)){var pixels=Raster(page.Visual);if(Bounds(pixels,0)!=firstRed||Bounds(pixels,1)!=firstGreen||Bounds(pixels,2)!=firstBlue)throw new Exception("cleared floater page regeneration geometry");}
   Console.WriteLine($"original floater clearing {mode} {direction} {clear}, prefix={prefix}, wideLeft={wideLeft}, object page={bluePage}: red={firstRed}, green={firstGreen}, blue={firstBlue}, {pages} pages, exact {body.Text.Length} source/page positions and regeneration/GC PASS");
  }
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft}) {
   var changing=new Border{Width=90,Height=80,Background=Brushes.Red};var first=new Floater(new BlockUIContainer(changing){Margin=new Thickness(0)}){Width=90,HorizontalAlignment=HorizontalAlignment.Left,Padding=new Thickness(0),Margin=new Thickness(0)};
   var centered=Make(Brushes.Blue,70,60,HorizontalAlignment.Center);centered.SetValue(Block.ClearFloatersProperty,WrapDirection.Left);var host=new Paragraph(first){Margin=new Thickness(0)};host.Inlines.Add(centered);host.Inlines.Add(new Run(string.Join(" ",Enumerable.Range(0,100).Select(i=>$"scroll{i:D3}"))));
   var doc=new FlowDocument(host){FontFamily=new FontFamily("Segoe UI"),FontSize=18,FlowDirection=direction,PagePadding=new Thickness(10)};TextOptions.SetTextFormattingMode(doc,mode);var viewer=new FlowDocumentScrollViewer{Document=doc,IsToolBarVisible=false};var window=new Window{Width=440,Height=320,Content=viewer};window.Show();Pump(viewer);
   var scroll=(ScrollViewer)viewer.Template.FindName("PART_ContentHost",viewer);var before=Raster(viewer);var red=Bounds(before,0);var blue=Bounds(before,2);if(red.IsEmpty||blue.IsEmpty||Math.Abs(blue.Top-red.Bottom)>1)throw new Exception("bottomless clear placement");
   changing.Height=120;Pump(viewer);var after=Raster(viewer);var newRed=Bounds(after,0);var newBlue=Bounds(after,2);if(newRed.IsEmpty||newBlue.IsEmpty||Math.Abs(newBlue.Top-newRed.Bottom)>1||newBlue.Top<blue.Top+39)throw new Exception("bottomless clearing does not follow resized previous floater");
   scroll.ScrollToEnd();Pump(viewer);if(scroll.VerticalOffset<=0||scroll.VerticalOffset+scroll.ViewportHeight<scroll.ExtentHeight-1)throw new Exception("cleared bottomless document cannot scroll to actual end");scroll.ScrollToTop();Pump(viewer);window.Close();GC.Collect(2,GCCollectionMode.Forced,true,true);
   Console.WriteLine($"original bottomless floater clearing {mode} {direction}: blue top {blue.Top}->{newBlue.Top}, actual resized geometry, scrolling and GC PASS");
  }
  Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original centered floater and selective clear-before-format geometry/pagination PASS");
 }
}
