using System;
using System.Globalization;
using System.Linq;
using System.IO;
using System.Windows.Media.Imaging;
using System.Windows.Controls;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.TextFormatting;
class JustifyRunProperties:TextRunProperties {
    public override Typeface Typeface=>new Typeface("Nirmala UI");
    public override double FontRenderingEmSize=>24;
    public override double FontHintingEmSize=>24;
    public override TextDecorationCollection TextDecorations=>null;
    public override Brush ForegroundBrush=>Brushes.Black;
    public override Brush BackgroundBrush=>null;
    public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("hi-IN");
    public override TextEffectCollection TextEffects=>null;
}
class JustifyParagraph:TextParagraphProperties {
    readonly bool justify;
    public JustifyParagraph(bool justify=true) { this.justify=justify; }
    public override FlowDirection FlowDirection=>FlowDirection.LeftToRight;
    public override TextAlignment TextAlignment=>justify?TextAlignment.Justify:TextAlignment.Left;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new JustifyRunProperties();
    public override TextWrapping TextWrapping=>TextWrapping.Wrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
}
class JustifySource:TextSource {
    public readonly string Text;
    public JustifySource(string text="कि") { Text=text; }
    public override TextRun GetTextRun(int index)=>index<Text.Length?new TextCharacters(Text,index,Text.Length-index,new JustifyRunProperties()):new TextEndOfParagraph(1);
    public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,
        new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("hi-IN"),new CharacterBufferRange(Text,0,Math.Min(limit,Text.Length))));
    public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
    static void Near(double actual,double expected,string why,double tolerance=0.03) { if(Math.Abs(actual-expected)>tolerance)throw new Exception(why+": "+actual+" vs "+expected); }
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            using var formatter=TextFormatter.Create(mode);
            foreach(string text in new[]{"alpha beta gamma delta epsilon zeta", "कि कि कि कि कि कि कि कि", "abcdefghijklmnopqrstuv", "alpha beta\ngamma delta"}) {
                var source=new JustifySource(text);int cp=0,lines=0,expanded=0;
                while(cp<=text.Length) {
                    using var line=formatter.FormatLine(source,cp,133,new JustifyParagraph(),null);
                    using var plain=formatter.FormatLine(source,cp,133,new JustifyParagraph(false),null);
                    if(line.Length!=plain.Length || line.Length<=0 || cp+line.Length>text.Length+1)throw new Exception("justification changes source coverage/break selection");
                    int end=Math.Min(text.Length,cp+line.Length),visible=end;
                    while(visible>cp && (text[visible-1]==' ' || text[visible-1]=='\n'))--visible;
                    bool hard=end>cp&&text[end-1]=='\n',last=cp+line.Length>text.Length;
                    bool space=Enumerable.Range(cp,Math.Max(0,visible-cp)).Any(index=>text[index]==' ');
                    if(!last && !hard && space) {
                        Near(line.Width,133,"justified visible width");
                        int at=Enumerable.Range(cp,visible-cp).First(index=>text[index]==' ');
                        double adjusted=line.GetTextBounds(at,1).Sum(bound=>bound.Rectangle.Width);
                        double ordinary=plain.GetTextBounds(at,1).Sum(bound=>bound.Rectangle.Width);
                        if(adjusted<=ordinary)throw new Exception("interword expansion did not reach retained selection geometry: "+adjusted+" vs "+ordinary);
                        ++expanded;
                    } else Near(line.Width,plain.Width,"final/hard/no-opportunity line stays ragged");
                    GC.Collect(2,GCCollectionMode.Forced,true,true);
                    var runs=line.GetIndexedGlyphRuns().ToArray();
                    Near(runs.Sum(run=>run.GlyphRun.AdvanceWidths.Sum()),line.WidthIncludingTrailingWhitespace,"retained glyph advances match justified width",mode==TextFormattingMode.Ideal?runs.Sum(run=>run.GlyphRun.GlyphIndices.Count)*24.0/2000+0.01:1.0);
                    if(visible<text.Length && visible>=cp)Near(line.GetDistanceFromCharacterHit(new CharacterHit(visible,0)),line.Width,"justified caret endpoint");
                    var visual=new DrawingVisual();using(var drawing=visual.RenderOpen())line.Draw(drawing,new Point(10,10),InvertAxes.None);
                    var bitmap=new RenderTargetBitmap(180,80,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);
                    var pixels=new byte[180*80*4];bitmap.CopyPixels(pixels,180*4,0);
                    if(!pixels.Where((value,index)=>index%4==3).Any(alpha=>alpha>150))throw new Exception("justified raster has no glyph pixels");
                    cp+=line.Length;++lines;if(lines>30)throw new Exception("justification did not terminate");
                }
                if(cp!=text.Length+1 || (text.Contains("alpha beta gamma") || text.StartsWith("कि"))&&expanded==0)throw new Exception("justified paragraph expansion/source coverage");
                Console.WriteLine($"public justification {mode}, {text.Length} source units: {lines} lines, {expanded} expanded; metrics/caret/selection/enumeration/raster/GC PASS");
            }
        }
        var block=new TextBlock{FontFamily=new FontFamily("Nirmala UI"),FontSize=24,TextAlignment=TextAlignment.Justify,TextWrapping=TextWrapping.Wrap,Text="alpha beta gamma delta epsilon zeta",Foreground=Brushes.Black};
        block.Measure(new Size(133,240));block.Arrange(new Rect(0,0,133,240));
        GC.Collect(2,GCCollectionMode.Forced,true,true);
        var image=new RenderTargetBitmap(160,240,96,96,PixelFormats.Pbgra32);image.Render(block);
        var bytes=new byte[160*240*4];image.CopyPixels(bytes,160*4,0);
        if(bytes.Where((value,index)=>index%4==3).Count(alpha=>alpha>150)<300 || block.GetPositionFromPoint(new Point(100,15),true)==null)throw new Exception("original justified TextBlock raster/point hit");
        Console.WriteLine("original justified TextBlock measure/render/point hit across GC PASS");
        Console.WriteLine("public original TextFormatter nominal/shaped justification, final/hard lines, no-space fallback, both formatting modes and source coverage PASS");
    }
}
