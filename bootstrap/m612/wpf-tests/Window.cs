using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Ellipse=System.Windows.Shapes.Ellipse;

class Check {
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern int GetWindowLongW(IntPtr hwnd,int index);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr hwnd,uint flags);
    [STAThread]
    static int Main(string[] args) {
        bool text=args.Contains("--text");
        bool indic=args.Contains("--indic");
        var app=new Application();
        app.DispatcherUnhandledException+=(s,e)=>{
            Console.Error.WriteLine(e.Exception); e.Handled=true; app.Shutdown(1);
        };
        var button=new Button { Width=160,Height=90,
            Content=text ? (object)(indic?"कि":"Source WPF text") : new Ellipse { Width=50,Height=50,Fill=Brushes.Blue } };
        if(indic) { button.FontFamily=new FontFamily("Nirmala UI"); button.FontSize=24; }
        int clicks=0;
        button.Click+=(s,e)=>clicks++;
        var panel=new Grid { Background=Brushes.White };
        panel.Children.Add(button);
        var window=new Window { Title="Source-built WPF 8",Width=320,Height=200,Content=panel };
        var timer=new DispatcherTimer { Interval=TimeSpan.FromMilliseconds(500) };
        timer.Tick+=(s,e)=>{
            timer.Stop();
            try {
                window.UpdateLayout();
                var hwnd=new WindowInteropHelper(window).Handle;
                Console.WriteLine($"window state: hwnd={hwnd}, valid={IsWindow(hwnd)}, style=0x{GetWindowLongW(hwnd,-16):X8}, visibility={window.Visibility}, visible={window.IsVisible}, loaded={window.IsLoaded}");
                for(var parent=GetAncestor(hwnd,1); parent!=IntPtr.Zero; parent=GetAncestor(parent,1))
                    Console.WriteLine($"native parent: hwnd={parent}, visible={IsWindowVisible(parent)}, style=0x{GetWindowLongW(parent,-16):X8}");
                if(!IsWindowVisible(hwnd)) {
                    // Process launchers can inherit SW_HIDE for the
                    // first native ShowWindow call. Exercise the WPF show path again.
                    window.Hide(); window.Show(); window.UpdateLayout();
                }
                if(!IsWindowVisible(hwnd)) throw new Exception("window is not visible after WPF hide/show");
                if(button.Template==null || VisualTreeHelper.GetChildrenCount(button)==0) throw new Exception("default button theme template missing");
                button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                if(clicks!=1) throw new Exception("button routed click failed");
                var bitmap=new RenderTargetBitmap((int)panel.ActualWidth,(int)panel.ActualHeight,96,96,PixelFormats.Pbgra32);
                bitmap.Render(panel);
                var pixels=new byte[bitmap.PixelWidth*bitmap.PixelHeight*4];
                bitmap.CopyPixels(pixels,bitmap.PixelWidth*4,0);
                if(!text) {
                    int blue=0;
                    for(int i=0;i<pixels.Length;i+=4) if(pixels[i]>240 && pixels[i+1]<15 && pixels[i+2]<15 && pixels[i+3]==255) blue++;
                    if(blue<100) throw new Exception("themed button content did not render");
                } else {
                    // Count dark glyph pixels in the central content area,
                    // excluding the control's border and background.
                    int dark=0;
                    for(int y=bitmap.PixelHeight/2-10;y<bitmap.PixelHeight/2+10;++y)
                        for(int x=bitmap.PixelWidth/2-60;x<bitmap.PixelWidth/2+60;++x) {
                            int i=(y*bitmap.PixelWidth+x)*4;
                            if(pixels[i]<80 && pixels[i+1]<80 && pixels[i+2]<80 && pixels[i+3]==255) ++dark;
                        }
                    if(dark<30) throw new Exception("themed button text glyph pixels did not render");
                }
                var encoder=new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
                var path=Path.Combine(AppContext.BaseDirectory,indic?"source-window-indic.png":text?"source-window-text.png":"source-window.png");
                using(var file=File.Create(path)) encoder.Save(file);
                Console.WriteLine("visible source WPF window, default Button template, routed click and rendered content OK: "+path);
                window.Close();
            } catch(Exception failure) { Console.Error.WriteLine(failure); app.Shutdown(1); }
        };
        timer.Start();
        try { return app.Run(window); }
        catch(Exception failure) { Console.Error.WriteLine(failure); return 1; }
    }
}
