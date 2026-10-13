using System;
using System.Globalization;
using System.Linq;
using System.Windows.Controls;
using System.Windows.Media.Imaging;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.TextFormatting;
class WidthRunProperties:TextRunProperties {
    public override Typeface Typeface=>new Typeface("Nirmala UI");
    public override double FontRenderingEmSize=>24;
    public override double FontHintingEmSize=>24;
    public override TextDecorationCollection TextDecorations=>null;
    public override Brush ForegroundBrush=>Brushes.Black;
    public override Brush BackgroundBrush=>null;
    public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("hi-IN");
    public override TextEffectCollection TextEffects=>null;
}
class WidthParagraph:TextParagraphProperties {
    readonly bool wrap;
    public WidthParagraph(bool wrap=false) { this.wrap=wrap; }
    public override FlowDirection FlowDirection=>FlowDirection.LeftToRight;
    public override TextAlignment TextAlignment=>TextAlignment.Left;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new WidthRunProperties();
    public override TextWrapping TextWrapping=>wrap?TextWrapping.Wrap:TextWrapping.NoWrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
}
class WidthSource:TextSource {
    public readonly string Text;
    public WidthSource(string text="कि") { Text=text; }
    public override TextRun GetTextRun(int index)=>index<Text.Length?new TextCharacters(Text,index,Text.Length-index,new WidthRunProperties()):new TextEndOfParagraph(1);
    public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,
        new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("hi-IN"),new CharacterBufferRange(Text,0,Math.Min(limit,Text.Length))));
    public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
    static void Near(double actual,double expected,string why) { if(Math.Abs(actual-expected)>0.03)throw new Exception(why+": "+actual+" vs "+expected); }
    static double Width(TextFormatter formatter,string text) { using var line=formatter.FormatLine(new WidthSource(text),0,10000,new WidthParagraph(),null);return line.Width; }
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            using var formatter=TextFormatter.Create(mode);
            foreach(string text in new[]{"alpha beta gamma delta", "कि कि कि", "alpha\u00a0beta gamma", "a\nlongerword beta", "abcdefghijk", "alpha beta   ", "    ", ""}) {
                var source=new WidthSource(text);var cache=new TextRunCache();
                var widths=formatter.FormatMinMaxParagraphWidth(source,0,new WidthParagraph(true),cache);
                double expectedMin=text.Split(new[]{' ','\n'},StringSplitOptions.RemoveEmptyEntries).Select(word=>Width(formatter,word)).DefaultIfEmpty(0).Max();
                double expectedMax=text.Split('\n').Select(line=>Width(formatter,line)).DefaultIfEmpty(0).Max();
                Near(widths.MinWidth,expectedMin,"paragraph minimum widest unbroken word");Near(widths.MaxWidth,expectedMax,"paragraph maximum hard-line width");
                GC.Collect(2,GCCollectionMode.Forced,true,true);
                var again=formatter.FormatMinMaxParagraphWidth(source,0,new WidthParagraph(true),cache);Near(again.MinWidth,widths.MinWidth,"cached minimum across GC");Near(again.MaxWidth,widths.MaxWidth,"cached maximum across GC");
                if(widths.MinWidth<0 || widths.MinWidth>widths.MaxWidth+0.03)throw new Exception("invalid minimum/maximum order");
                Console.WriteLine($"public paragraph widths {mode}, {text.Length} units: min={widths.MinWidth:F3}, max={widths.MaxWidth:F3} PASS");
            }
            var formatted=new FormattedText("alpha beta gamma delta",CultureInfo.GetCultureInfo("en-US"),FlowDirection.LeftToRight,new Typeface("Nirmala UI"),24,Brushes.Black,null,mode,1);
            Near(formatted.MinWidth,new[]{"alpha","beta","gamma","delta"}.Select(word=>Width(formatter,word)).Max(),"FormattedText.MinWidth uses real word metrics");
        }
        var block=new TextBlock{FontFamily=new FontFamily("Nirmala UI"),FontSize=24,Text="alpha beta gamma delta",TextWrapping=TextWrapping.Wrap};
        block.Measure(new Size(double.PositiveInfinity,200));double maximum=block.DesiredSize.Width;
        block.Measure(new Size(90,200));block.Arrange(new Rect(0,0,90,200));GC.Collect(2,GCCollectionMode.Forced,true,true);
        var bitmap=new RenderTargetBitmap(120,200,96,96,PixelFormats.Pbgra32);bitmap.Render(block);
        if(maximum<=90 || block.DesiredSize.Height<=24 || block.GetPositionFromPoint(new Point(20,10),true)==null)throw new Exception("TextBlock intrinsic/wrapped layout and point hit");
        Console.WriteLine("original FormattedText.MinWidth and TextBlock intrinsic/wrapped layout/render/hit after GC PASS");
        Console.WriteLine("public paragraph min/max nominal/shaped/NBSP/hard-break/trailing/empty/cache/GC in both formatting modes PASS");
    }
}
