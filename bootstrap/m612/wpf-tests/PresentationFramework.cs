using System;
using System.Linq;
using System.Reflection;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Rectangle=System.Windows.Shapes.Rectangle;
class Check {
    [STAThread]
    static void Main() {
        var assembly=typeof(Button).Assembly;
        var panel=new StackPanel();
        var button=new Button { Content="Source WPF",Width=123 };
        panel.Children.Add(button);
        if(panel.Children.Count!=1 || button.Width!=123 || (string)button.Content!="Source WPF") throw new Exception("source control/dependency-property behavior failed");
        Console.WriteLine("source PresentationFramework controls and dependency properties OK");
        var parsed=(StackPanel)XamlReader.Parse("<StackPanel xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\"><Button Content=\"Source XAML\" /></StackPanel>");
        if(parsed.Children.Count!=1 || (string)((Button)parsed.Children[0]).Content!="Source XAML") throw new Exception("source XAML parse failed");
        Console.WriteLine("source PresentationFramework XAML parser OK");
        var compiled=new Bootstrap.MarkupView();
        compiled.InitializeComponent();
        if(compiled.Children.Count!=1 || compiled.Sample.Width!=60 || ((SolidColorBrush)compiled.Sample.Fill).Color!=Colors.Red) throw new Exception("source compiled BAML view failed");
        Console.WriteLine("source markup compiler BAML loads and generated named field connects");
        var canvas=new Canvas { Width=120,Height=80 };
        canvas.Children.Add(new Rectangle { Width=60,Height=40,Fill=Brushes.Red });
        canvas.Measure(new Size(120,80));
        canvas.Arrange(new Rect(0,0,120,80));
        var bitmap=new RenderTargetBitmap(120,80,96,96,PixelFormats.Pbgra32);
        bitmap.Render(canvas);
        var pixel=new byte[4];
        bitmap.CopyPixels(new Int32Rect(10,10,1,1),pixel,4,0);
        if(pixel[0]!=0 || pixel[1]!=0 || pixel[2]!=255 || pixel[3]!=255) throw new Exception("source compositor red pixel mismatch: "+Convert.ToHexString(pixel));
        bitmap.CopyPixels(new Int32Rect(90,60,1,1),pixel,4,0);
        if(pixel.Any(value=>value!=0)) throw new Exception("source compositor background pixel mismatch: "+Convert.ToHexString(pixel));
        var encoder=new PngBitmapEncoder();
        encoder.Frames.Add(BitmapFrame.Create(bitmap));
        var path=Path.Combine(AppContext.BaseDirectory,"source-wpf-render.png");
        using(var file=File.Create(path)) encoder.Save(file);
        Console.WriteLine("source WPF compositor rendered verified red pixels: "+path);
        try { Console.WriteLine($"source PresentationFramework CLR load: {assembly.GetTypes().Length} types"); }
        catch(ReflectionTypeLoadException e) {
            foreach(var error in e.LoaderExceptions.Select(x=>x.Message).Distinct()) Console.WriteLine(error);
            throw;
        }
    }
}
