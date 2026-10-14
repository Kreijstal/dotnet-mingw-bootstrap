using System;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;
class SceneVisual:DrawingVisual { public SceneVisual(){VisualBitmapScalingMode=BitmapScalingMode.NearestNeighbor;VisualEdgeMode=EdgeMode.Aliased;} }
class Check {
 const int Size=160;
 static byte[] Render(Visual visual,string name){var bitmap=new RenderTargetBitmap(Size,Size,96,96,PixelFormats.Pbgra32);var root=new ContainerVisual();root.Children.Add(visual);bitmap.Render(root);root.Children.Remove(visual);var bytes=new byte[Size*Size*4];bitmap.CopyPixels(bytes,Size*4,0);var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(bitmap));using(var file=File.Create(System.IO.Path.Combine(AppContext.BaseDirectory,name+".png")))png.Save(file);return bytes;}
 static uint Pixel(byte[] bytes,int x,int y)=>BitConverter.ToUInt32(bytes,(y*Size+x)*4);
 static int Alpha(byte[] bytes,int x,int y)=>bytes[(y*Size+x)*4+3];
 static void Require(bool value,string reason){if(!value)throw new Exception(reason);}
 static Canvas Host(UIElement element){var canvas=new Canvas{Width=Size,Height=Size};Canvas.SetLeft(element,30);Canvas.SetTop(element,30);canvas.Children.Add(element);canvas.Measure(new Size(Size,Size));canvas.Arrange(new Rect(0,0,Size,Size));canvas.UpdateLayout();return canvas;}
 [STAThread]static void Main(){
  RenderOptions.ProcessRenderMode=System.Windows.Interop.RenderMode.SoftwareOnly;Require(RenderOptions.ProcessRenderMode==System.Windows.Interop.RenderMode.SoftwareOnly,"software rendering policy");
  Console.WriteLine("drawing opacity/transform/clip start");
  var visual=new SceneVisual();
  using(var dc=visual.RenderOpen()){
   dc.PushOpacity(0.5);dc.DrawRectangle(Brushes.Red,null,new Rect(10,10,30,30));dc.Pop();
   dc.PushTransform(new TranslateTransform(20,50));dc.PushClip(new RectangleGeometry(new Rect(10,0,30,50)));
   var triangle=new StreamGeometry();using(var g=triangle.Open()){g.BeginFigure(new Point(0,0),true,true);g.LineTo(new Point(60,0),true,false);g.LineTo(new Point(0,60),true,false);}triangle.Freeze();dc.DrawGeometry(Brushes.Lime,null,triangle);dc.Pop();dc.Pop();
  }
  var simple=Render(visual,"opacity-clip");Require(Pixel(simple,20,20)==0x80800000,"premultiplied half-opacity interior");Require(Pixel(simple,45,20)==0,"opacity rectangle leaked");Require(Pixel(simple,35,60)==0xFF00FF00&&Pixel(simple,25,60)==0&&Pixel(simple,65,60)==0&&Pixel(simple,50,100)==0,"transformed triangle clip geometry");
  Console.WriteLine("drawing exact opacity/transformed clip PASS");
  Console.WriteLine("drawing brushes start");
  var tile=BitmapSource.Create(2,2,96,96,PixelFormats.Bgra32,null,new byte[]{0,0,255,255,0,255,0,255,255,0,0,255,255,255,255,255},8);tile.Freeze();
  var brush=new ImageBrush(tile){ViewportUnits=BrushMappingMode.Absolute,Viewport=new Rect(0,0,8,8),TileMode=TileMode.Tile,Stretch=Stretch.Fill};brush.Freeze();
  var brushes=new SceneVisual();
  using(var dc=brushes.RenderOpen()){
   dc.DrawRectangle(brush,null,new Rect(0,0,40,40));
   var gradient=new LinearGradientBrush(Colors.Red,Colors.Blue,new Point(0,0),new Point(1,0)){ColorInterpolationMode=ColorInterpolationMode.SRgbLinearInterpolation};gradient.Freeze();dc.DrawRectangle(gradient,null,new Rect(10,60,100,30));
  }
  var colors=Render(brushes,"brushes");Console.WriteLine($"tile pixels: {Pixel(colors,1,1):X8} {Pixel(colors,5,1):X8} {Pixel(colors,1,5):X8} {Pixel(colors,5,5):X8}");Require(Pixel(colors,1,1)==0xFFFF0000&&Pixel(colors,5,1)==0xFF00FF00&&Pixel(colors,1,5)==0xFF0000FF&&Pixel(colors,5,5)==0xFFFFFFFF,"nearest-neighbor tiled image colors");
  for(int y=1;y<30;y+=8)for(int x=1;x<30;x+=8)Require(Pixel(colors,x,y)==Pixel(colors,1,1),"tiled image periodicity");
  int redLeft=(int)((Pixel(colors,12,70)>>16)&255),redRight=(int)((Pixel(colors,107,70)>>16)&255);uint middle=Pixel(colors,60,70);
  Require(redLeft>240&&redRight<15&&(middle>>24)==255&&((middle>>8)&255)==0&&Math.Abs((int)((middle>>16)&255)-(int)(middle&255))<=3,"sRGB gradient endpoints and midpoint");
  Console.WriteLine("drawing tiled source bitmap and linear gradient PASS");
  Console.WriteLine("drawing opacity mask/visual brush start");
  var masked=new Rectangle{Width=40,Height=40,Fill=Brushes.Blue,OpacityMask=new LinearGradientBrush(Colors.Transparent,Colors.White,new Point(0,0),new Point(1,0))};var maskHost=Host(masked);var mask=Render(maskHost,"opacity-mask");
  Require(Alpha(mask,31,50)<30&&Alpha(mask,50,50)>100&&Alpha(mask,50,50)<160&&Alpha(mask,68,50)>220,"opacity mask gradient coverage");
  foreach(int x in new[]{31,40,50,60,68})Require((Pixel(mask,x,50)&0xFFFF00)==0&&(int)(Pixel(mask,x,50)&255)==Alpha(mask,x,50),"opacity mask preserves premultiplied blue");
  var source=new Grid{Width=8,Height=8,Background=Brushes.Orange};source.Children.Add(new Border{Width=4,HorizontalAlignment=HorizontalAlignment.Left,Background=Brushes.Cyan});source.Measure(new Size(8,8));source.Arrange(new Rect(0,0,8,8));source.UpdateLayout();
  var liveBrush=new VisualBrush(source){AutoLayoutContent=false,ViewboxUnits=BrushMappingMode.Absolute,Viewbox=new Rect(0,0,8,8),ViewportUnits=BrushMappingMode.Absolute,Viewport=new Rect(0,0,8,8),TileMode=TileMode.Tile,Stretch=Stretch.Fill};var liveVisual=new SceneVisual();using(var dc=liveVisual.RenderOpen())dc.DrawRectangle(liveBrush,null,new Rect(0,0,24,24));var live=Render(liveVisual,"visual-brush");
  Require(Pixel(live,1,1)==0xFF00FFFF&&Pixel(live,5,1)==0xFFFFA500&&Pixel(live,9,9)==0xFF00FFFF,"retained visual brush vector tile");
  source.Background=Brushes.Lime;source.UpdateLayout();var changed=Render(liveVisual,"visual-brush-changed");Require(Pixel(changed,5,1)==0xFF00FF00&&Pixel(changed,1,1)==0xFF00FFFF,"visual brush source invalidation");
  Console.WriteLine("drawing opacity mask and retained visual brush invalidation PASS");
  Console.WriteLine("drawing blur start");
  var blurred=new Rectangle{Width=40,Height=40,Fill=Brushes.Red,Effect=new BlurEffect{Radius=8,KernelType=KernelType.Gaussian,RenderingBias=RenderingBias.Quality}};
  var blurHost=Host(blurred);var blur=Render(blurHost,"blur");Require(Alpha(blur,50,50)>240&&Alpha(blur,71,50)>0&&Alpha(blur,90,50)==0,"software Gaussian blur interior/halo/outside");
  Require((Pixel(blur,71,50)&0x00FFFF)==0,"blur introduced non-red channels");
  Console.WriteLine("drawing software Gaussian blur PASS");
  Console.WriteLine("drawing shadow start");
  var shadowed=new Rectangle{Width=40,Height=40,Fill=Brushes.Red,Effect=new DropShadowEffect{Color=Colors.Black,Opacity=0.7,BlurRadius=4,ShadowDepth=8,Direction=0,RenderingBias=RenderingBias.Quality}};
  var shadowHost=Host(shadowed);var shadow=Render(shadowHost,"shadow");Require(Pixel(shadow,50,50)==0xFFFF0000,"drop shadow replaced source fill");Require(Alpha(shadow,75,50)>50&&(Pixel(shadow,75,50)&0xFFFFFF)==0&&Alpha(shadow,24,50)==0,"software drop shadow direction and separate black halo");
  Console.WriteLine("drawing software drop shadow PASS");
  ((DropShadowEffect)shadowed.Effect).ShadowDepth=14;shadowHost.UpdateLayout();var moved=Render(shadowHost,"shadow-moved");Require(Alpha(moved,82,50)>50&&Alpha(shadow,82,50)==0,"effect property invalidation moves the shadow");
  GC.Collect(2,GCCollectionMode.Forced,true,true);var retained=Render(shadowHost,"shadow-after-gc");Require(retained.SequenceEqual(moved),"retained effect changed across compacting GC");
  Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("public source software renderer brushes, transformed clips, opacity masks, blur/drop-shadow, invalidation and retained GC PASS");
 }
}
