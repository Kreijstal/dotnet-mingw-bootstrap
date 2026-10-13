using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.TextFormatting;
class DecorationProperties:TextRunProperties {
    readonly TextDecorationCollection decorations;
    public DecorationProperties(TextDecorationCollection value) { decorations=value; }
    public override Typeface Typeface=>new Typeface("Segoe UI");
    public override double FontRenderingEmSize=>32;
    public override double FontHintingEmSize=>32;
    public override TextDecorationCollection TextDecorations=>decorations;
    public override Brush ForegroundBrush=>Brushes.Black;
    public override Brush BackgroundBrush=>null;
    public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("en-US");
    public override TextEffectCollection TextEffects=>null;
}
class DecorationParagraph:TextParagraphProperties {
    readonly TextRunProperties properties;
    public DecorationParagraph(TextRunProperties value) { properties=value; }
    public override FlowDirection FlowDirection=>FlowDirection.LeftToRight;
    public override TextAlignment TextAlignment=>TextAlignment.Left;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>properties;
    public override TextWrapping TextWrapping=>TextWrapping.NoWrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
}
class DecorationSource:TextSource {
    public readonly string Text;
    readonly TextRunProperties properties;
    public DecorationSource(string text,TextRunProperties value) { Text=text; properties=value; }
    public override TextRun GetTextRun(int index)=>index<Text.Length?new TextCharacters(Text,index,Text.Length-index,properties):new TextEndOfParagraph(1);
    public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("en-US"),new CharacterBufferRange(Text,0,Math.Min(limit,Text.Length))));
    public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
    [STAThread]
    static void Main() {
        using var formatter=TextFormatter.Create();
        foreach(var location in new[]{TextDecorationLocation.Underline,TextDecorationLocation.Strikethrough,TextDecorationLocation.OverLine,TextDecorationLocation.Baseline}) {
            var decoration=new TextDecoration(location,new Pen(Brushes.Red,2),0,TextDecorationUnit.Pixel,TextDecorationUnit.Pixel);
            var collection=new TextDecorationCollection{decoration};
            var properties=new DecorationProperties(collection);
            var source=new DecorationSource("source text",properties);
            using var line=formatter.FormatLine(source,0,400,new DecorationParagraph(properties),null);
            using var plain=formatter.FormatLine(new DecorationSource(source.Text,new DecorationProperties(null)),0,400,new DecorationParagraph(new DecorationProperties(null)),null);
            if(Math.Abs(line.Width-plain.Width)>0.01 || Math.Abs(line.Height-plain.Height)>0.01) throw new Exception("decoration changes text metrics");
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            var visual=new DrawingVisual();
            using(var drawing=visual.RenderOpen()) line.Draw(drawing,new Point(10,10),InvertAxes.None);
            var bitmap=new RenderTargetBitmap(420,100,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);
            var pixels=new byte[420*100*4];bitmap.CopyPixels(pixels,420*4,0);
            int red=0,black=0;double redY=0;
            for(int at=0;at<pixels.Length;at+=4) {
                if(pixels[at+3]>200 && pixels[at+2]>180 && pixels[at+1]<50 && pixels[at]<50) { ++red;redY+=(at/4)/420; }
                if(pixels[at+3]>200 && pixels[at+2]<50 && pixels[at+1]<50 && pixels[at]<50) ++black;
            }
            if(red<line.Width || black<100) throw new Exception("decoration or glyph pixels missing: "+location+" "+red+" "+black);
            redY/=red;
            double baseline=10+line.Baseline;
            if(location==TextDecorationLocation.Underline && redY<=baseline || location==TextDecorationLocation.Strikethrough && redY>=baseline ||
               location==TextDecorationLocation.OverLine && redY>=baseline-10 || location==TextDecorationLocation.Baseline && Math.Abs(redY-baseline)>2)
                throw new Exception("decoration baseline geometry: "+location+" "+redY+" vs "+baseline);
            var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using(var file=File.Create(Path.Combine(AppContext.BaseDirectory,"decoration-"+location+".png"))) encoder.Save(file);
            Console.WriteLine($"public text decoration {location}: {red} red pixels, y={redY:F2}, baseline={baseline:F2} PASS");
        }
        Console.WriteLine("public source TextFormatter decoration metrics, custom pens, all locations, raster geometry and moving GC PASS");
    }
}
