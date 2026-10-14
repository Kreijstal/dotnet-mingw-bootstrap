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
 const BindingFlags I=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
 static byte[] Raster(Visual visual){var image=new RenderTargetBitmap(400,260,96,96,PixelFormats.Pbgra32);image.Render(visual);var p=new byte[400*260*4];image.CopyPixels(p,1600,0);return p;}
 static Rect Bounds(byte[] p,int color){int left=400,right=-1,top=260,bottom=-1;for(int y=0;y<260;++y)for(int x=0;x<400;++x){int at=(y*400+x)*4;if(p[at+3]<150)continue;bool hit=color==0?p[at+2]>200&&p[at+1]<60&&p[at]<60:color==1?p[at+1]>180&&p[at+2]<60&&p[at]<60:p[at]>150&&p[at+1]<90&&p[at+2]<90;if(hit){left=Math.Min(left,x);right=Math.Max(right,x);top=Math.Min(top,y);bottom=Math.Max(bottom,y);}}return right<0?Rect.Empty:new Rect(left,top,right-left+1,bottom-top+1);}
 static Floater Float(Brush color,double width,double height,HorizontalAlignment alignment)=>new Floater(new BlockUIContainer(new Border{Width=width,Height=height,Background=color}){Margin=new Thickness(0)}){Width=width,HorizontalAlignment=alignment,Margin=new Thickness(0),Padding=new Thickness(0)};
 static void Pump(FrameworkElement element){element.UpdateLayout();var frame=new DispatcherFrame();Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle,new Action(()=>frame.Continue=false));Dispatcher.PushFrame(frame);element.UpdateLayout();}
 static void Clearing(){
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})foreach(var clear in new[]{WrapDirection.None,WrapDirection.Left,WrapDirection.Right,WrapDirection.Both}){
   var red=Float(Brushes.Red,100,160,HorizontalAlignment.Left);var green=Float(Brushes.Lime,70,50,HorizontalAlignment.Right);green.SetValue(Block.ClearFloatersProperty,clear);var first=new Paragraph(red){Margin=new Thickness(0)};first.Inlines.Add("anchor");var second=new Paragraph(green){Margin=new Thickness(0)};second.Inlines.Add("next");var body=new Paragraph(new Run(string.Join(" ",Enumerable.Range(0,80).Select(i=>$"body{i:D3}"))){Foreground=Brushes.Blue}){Margin=new Thickness(0)};
   var doc=new FlowDocument(first){FontFamily=new FontFamily("Segoe UI"),FontSize=18,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity,FlowDirection=direction};doc.Blocks.Add(second);doc.Blocks.Add(body);TextOptions.SetTextFormattingMode(doc,mode);var paginator=((IDocumentPaginatorSource)doc).DocumentPaginator;paginator.PageSize=new Size(400,260);
   using var page=paginator.GetPage(0);var pixels=Raster(page.Visual);var r=Bounds(pixels,0);var g=Bounds(pixels,1);if(r.IsEmpty||g.IsEmpty)throw new Exception("cross-paragraph colored objects missing");if(clear==WrapDirection.Left||clear==WrapDirection.Both){if(Math.Abs(g.Top-r.Bottom)>1)throw new Exception("cross-paragraph clear ignores inherited object");}else if(g.Top>=r.Bottom-20)throw new Exception("unrelated-side clear unnecessarily lowers following object");
   using var next=paginator.GetPage(1);if(next!=DocumentPage.Missing&&(!Bounds(Raster(next.Visual),0).IsEmpty||!Bounds(Raster(next.Visual),1).IsEmpty))throw new Exception("cross-paragraph objects repeated across pages");
   Console.WriteLine($"original cross-paragraph clear {mode} {direction} {clear}: red={r}, green={g} PASS");
  }
 }
 static void Bottomless(){
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft}){
   var changing=new Border{Width=120,Height=110,Background=Brushes.Red};var floater=new Floater(new BlockUIContainer(changing){Margin=new Thickness(0)}){Width=120,HorizontalAlignment=HorizontalAlignment.Center,Padding=new Thickness(0),Margin=new Thickness(0)};var first=new Paragraph(floater){Margin=new Thickness(0)};first.Inlines.Add("anchor");var following=new Paragraph(new Run(string.Join(" ",Enumerable.Range(0,150).Select(i=>$"body{i:D3}"))){Foreground=Brushes.Blue}){Margin=new Thickness(0)};
   var doc=new FlowDocument(first){FontFamily=new FontFamily("Segoe UI"),FontSize=18,PagePadding=new Thickness(10),FlowDirection=direction};doc.Blocks.Add(following);TextOptions.SetTextFormattingMode(doc,mode);var viewer=new FlowDocumentScrollViewer{Document=doc,IsToolBarVisible=false};var window=new Window{Content=viewer,Width=440,Height=320,ShowInTaskbar=false};window.Show();Pump(viewer);var scroll=(ScrollViewer)viewer.Template.FindName("PART_ContentHost",viewer);
   foreach(double height in new[]{110d,180d,110d}){changing.Height=height;Pump(viewer);var pixels=Raster(viewer);var red=Bounds(pixels,0);var blue=Bounds(pixels,2);if(red.IsEmpty||Math.Abs(red.Height-height)>1||blue.IsEmpty||blue.Top>=red.Bottom-20)throw new Exception("bottomless following paragraph does not follow changed floater");for(int y=(int)red.Top;y<red.Bottom;++y)for(int x=(int)red.Left;x<red.Right;++x){int at=(y*400+x)*4;if(pixels[at+3]>150&&pixels[at]>150&&pixels[at+1]<90&&pixels[at+2]<90)throw new Exception("resized inherited floater overlaps following glyphs");}scroll.ScrollToEnd();Pump(viewer);if(scroll.VerticalOffset<=0||scroll.VerticalOffset+scroll.ViewportHeight<scroll.ExtentHeight-1)throw new Exception("cross-paragraph bottomless scroll extent");scroll.ScrollToTop();Pump(viewer);GC.Collect(2,GCCollectionMode.Forced,true,true);if(!pixels.SequenceEqual(Raster(viewer)))throw new Exception("cross-paragraph bottomless regeneration/GC");}
   window.Close();Console.WriteLine($"original bottomless cross-paragraph {mode} {direction}: 110/180/110 resize, wrapped glyphs, scrolling and GC PASS");
  }
 }
 static void Columns(){
  const BindingFlags flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})foreach(int count in new[]{12,70}){
   var floater=Float(Brushes.Red,70,130,HorizontalAlignment.Left);var anchor=new Run("A");var first=new Paragraph(floater){Margin=new Thickness(0)};first.Inlines.Add(anchor);var body=new Run(string.Join(" ",Enumerable.Repeat("body",count))){Foreground=Brushes.Blue};var following=new Paragraph(body){Margin=new Thickness(0)};
   var doc=new FlowDocument(first){FontFamily=new FontFamily("Segoe UI"),FontSize=18,ColumnWidth=180,ColumnGap=20,PagePadding=new Thickness(10),FlowDirection=direction};doc.Blocks.Add(following);TextOptions.SetTextFormattingMode(doc,mode);var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)doc).DocumentPaginator;paginator.PageSize=new Size(400,260);var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,objects=0;
   for(;pages<15;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;var pixels=Raster(page.Visual);var red=Bounds(pixels,0);if(!red.IsEmpty){++objects;if(pages!=0||Math.Abs(red.Height-130)>1)throw new Exception("cross-paragraph column object geometry/source");var blue=Bounds(pixels,2);if(blue.IsEmpty||blue.Top>=red.Bottom-20)throw new Exception("column following paragraph does not wrap");}var view=((IServiceProvider)page).GetService(viewType);foreach(object segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();ranges.Add((pages,(TextPointer)t.GetProperty("Start",flags).GetValue(segment),(TextPointer)t.GetProperty("End",flags).GetValue(segment)));}GC.Collect(2,GCCollectionMode.Forced,true,true);if(!pixels.SequenceEqual(Raster(page.Visual)))throw new Exception("cross-paragraph column raster GC");}
   if(pages<1||pages>=15||objects!=1||!paginator.IsPageCountValid||count==12&&pages!=1||count==70&&pages<2)throw new Exception("cross-paragraph column pagination");foreach(var run in new[]{anchor,body})for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("cross-paragraph column source ownership");}using(var page=paginator.GetPage(0))if(Bounds(Raster(page.Visual),0).IsEmpty)throw new Exception("cross-paragraph column disposed page regeneration");Console.WriteLine($"original cross-paragraph columns {mode} {direction}, words={count}: {pages} pages, wrapped glyphs, exact source/page ownership and regeneration/GC PASS");
  }
 }
 [STAThread]static void Main(){
  var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})foreach(var alignment in new[]{HorizontalAlignment.Left,HorizontalAlignment.Center,HorizontalAlignment.Right})foreach(bool mixed in new[]{false,true}){
   var floater=Float(Brushes.Red,120,160,alignment);var anchor=new Run("anchor");var first=new Paragraph(floater){Margin=new Thickness(0)};first.Inlines.Add(anchor);
   var body=new Run(string.Join(" ",Enumerable.Range(0,100).Select(i=>$"body{i:D3}"))){Foreground=Brushes.Blue};var following=new Paragraph(body){Margin=new Thickness(0),FlowDirection=mixed?(direction==FlowDirection.LeftToRight?FlowDirection.RightToLeft:FlowDirection.LeftToRight):direction};
   var doc=new FlowDocument(first){FontFamily=new FontFamily("Segoe UI"),FontSize=18,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity,FlowDirection=direction};doc.Blocks.Add(following);TextOptions.SetTextFormattingMode(doc,mode);
   doc.IsOptimalParagraphEnabled=alignment==HorizontalAlignment.Center;
   var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)doc).DocumentPaginator;paginator.PageSize=new Size(400,260);var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,objectPage=-1,wrappedRows=0;Rect initial=Rect.Empty;
   for(;pages<30;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;var pixels=Raster(page.Visual);var red=Bounds(pixels,0);if(!red.IsEmpty){if(objectPage>=0)throw new Exception("cross-paragraph floater repeats");objectPage=pages;initial=red;var blue=Bounds(pixels,2);Console.WriteLine($"cross-paragraph {mode} {direction} {alignment}, mixed={mixed}: object={red}, following glyph bounds={blue}");if(blue.IsEmpty||blue.Top>=red.Bottom-20)throw new Exception("following paragraph does not wrap beside previous floater");
     for(int y=(int)red.Top;y<red.Bottom;++y){int count=0;for(int x=0;x<400;++x){int at=(y*400+x)*4;if(pixels[at+3]<150||pixels[at]<150||pixels[at+1]>90||pixels[at+2]>90)continue;if(x>=red.Left&&x<red.Right)throw new Exception("following paragraph glyph overlaps inherited floater");++count;}if(count>5)++wrappedRows;}
    }
    var view=((IServiceProvider)page).GetService(viewType);
    if(pages==0){for(int at=0;at<80;++at){var position=body.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var caret=(Rect)viewType.GetMethod("GetRectangleFromTextPosition").Invoke(view,new object[]{position});if(caret.IsEmpty||caret.Height<=0)throw new Exception("cross-paragraph caret absent");if(!red.IsEmpty&&caret.Top<red.Bottom&&caret.Bottom>red.Top&&caret.Left>red.Left+1&&caret.Left<red.Right-1)throw new Exception($"inherited obstacle caret overlaps object: at={at}, caret={caret}, object={red}, mixed={mixed}");if(viewType.GetMethod("GetTextPositionFromPoint").Invoke(view,new object[]{new Point(caret.Left+1,caret.Top+caret.Height/2),true})==null)throw new Exception("cross-paragraph point mapping");}}
    foreach(object segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();ranges.Add((pages,(TextPointer)t.GetProperty("Start",I).GetValue(segment),(TextPointer)t.GetProperty("End",I).GetValue(segment)));}
    GC.Collect(2,GCCollectionMode.Forced,true,true);if(!pixels.SequenceEqual(Raster(page.Visual)))throw new Exception("cross-paragraph pixels change after GC");
   }
   if(pages<2||pages>=30||objectPage!=0||wrappedRows<5||!paginator.IsPageCountValid)throw new Exception("cross-paragraph layout progress");
   foreach(var run in new[]{anchor,body})for(int i=0;i<run.Text.Length;++i){var position=run.ContentStart.GetPositionAtOffset(i,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("cross-paragraph source ownership");}
   using(var page=paginator.GetPage(0))if(Bounds(Raster(page.Visual),0)!=initial)throw new Exception("cross-paragraph disposed-page regeneration");
   Console.WriteLine($"original cross-paragraph {mode} {direction} {alignment}, mixed={mixed}: {pages} pages, {wrappedRows} actual wrapped glyph rows, source ownership/regeneration/GC PASS");
  }
  Clearing();Bottomless();Columns();
  Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original cross-paragraph floater wrapping and cleanup PASS");
 }
}
