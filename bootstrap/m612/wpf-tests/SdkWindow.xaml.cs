using System;
using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
namespace SdkCheck {
    public partial class App : Application { }
    public partial class MainWindow : Window {
        public MainWindow() {
            InitializeComponent();
            Loaded+=(sender,args)=>Dispatcher.BeginInvoke(new Action(Check),DispatcherPriority.ApplicationIdle);
        }
        void Check() {
            try {
                if(Surface==null) throw new Exception("generated BAML field");
                string framework=typeof(Window).Assembly.Location;
                bool local=File.Exists(Path.Combine(AppContext.BaseDirectory,"PresentationFramework.dll"));
                if(local ? !string.Equals(Path.GetDirectoryName(framework),AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar),StringComparison.OrdinalIgnoreCase) :
                    !framework.Contains("8.0.0-bootstrap.wpf.8.0.0")) throw new Exception("unexpected framework location: "+framework);
                var bitmap=new RenderTargetBitmap(200,100,96,96,PixelFormats.Pbgra32);
                Surface.Measure(new Size(200,100)); Surface.Arrange(new Rect(0,0,200,100));
                bitmap.Render(Surface);
                var pixels=new byte[200*100*4]; bitmap.CopyPixels(pixels,200*4,0);
                int red=0;
                for(int at=0;at<pixels.Length;at+=4) if(pixels[at+2]>200 && pixels[at+1]<40 && pixels[at]<40 && pixels[at+3]>200) ++red;
                if(red<2000) throw new Exception("source framework raster pixels: "+red);
                var encoder=new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
                using(var output=File.Create(Path.Combine(AppContext.BaseDirectory,"wpf-sdk-check.png"))) encoder.Save(output);
                Console.WriteLine($"normal WPF SDK project: generated BAML field, {(local ? "self-contained" : "shared framework")}, window and raster pixels PASS");
                Application.Current.Shutdown(0);
            } catch(Exception e) { Console.Error.WriteLine(e); Application.Current.Shutdown(1); }
        }
    }
}
