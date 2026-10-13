using System;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.TextFormatting;
class RunProperties:TextRunProperties {
    public override Typeface Typeface=>new Typeface("Nirmala UI");
    public override double FontRenderingEmSize=>24;
    public override double FontHintingEmSize=>24;
    public override TextDecorationCollection TextDecorations=>null;
    public override Brush ForegroundBrush=>Brushes.Black;
    public override Brush BackgroundBrush=>null;
    public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("hi-IN");
    public override TextEffectCollection TextEffects=>null;
}
class Paragraph:TextParagraphProperties {
    public override FlowDirection FlowDirection=>FlowDirection.LeftToRight;
    public override TextAlignment TextAlignment=>TextAlignment.Left;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new RunProperties();
    public override TextWrapping TextWrapping=>TextWrapping.NoWrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
}
class Source:TextSource {
    public readonly string Text="कि";
    public override TextRun GetTextRun(int index)=>index<Text.Length?new TextCharacters(Text,index,Text.Length-index,new RunProperties()):new TextEndOfParagraph(1);
    public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,
        new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("hi-IN"),new CharacterBufferRange(Text,0,Math.Min(limit,Text.Length))));
    public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
    [STAThread]
    static void Main() {
        var source=new Source();
        using var formatter=TextFormatter.Create();
        using var line=formatter.FormatLine(source,0,200,new Paragraph(),null);
        if(line.Width<=0 || line.Height<=0 || line.Baseline<=0 || line.Length!=source.Text.Length+1)
            throw new Exception("callback-created shaped line metrics failed");
        var runs=line.GetIndexedGlyphRuns().ToArray();
        if(runs.Length==0 || runs.Sum(run=>run.TextSourceLength)!=source.Text.Length || runs.Any(run=>run.GlyphRun.GlyphIndices.Count==0))
            throw new Exception("original shaped line enumeration failed");
        GC.Collect(2,GCCollectionMode.Forced,true,true);
        var again=line.GetIndexedGlyphRuns().ToArray();
        if(again.Length!=runs.Length || again[0].GlyphRun.GlyphIndices.Count!=runs[0].GlyphRun.GlyphIndices.Count)
            throw new Exception("retained shaped line enumeration failed after GC");
        Console.WriteLine($"public source TextFormatter: length={line.Length}, width={line.Width:F3}, height={line.Height:F3}, {runs.Length} indexed glyph runs; original shaping/enumeration/GC PASS");
    }
}
