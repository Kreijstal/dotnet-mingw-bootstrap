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
    readonly bool wrap;
    public Paragraph(bool wrap=false) { this.wrap=wrap; }
    public override FlowDirection FlowDirection=>FlowDirection.LeftToRight;
    public override TextAlignment TextAlignment=>TextAlignment.Left;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new RunProperties();
    public override TextWrapping TextWrapping=>wrap?TextWrapping.Wrap:TextWrapping.NoWrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
}
class Source:TextSource {
    public readonly string Text;
    public Source(string text="कि") { Text=text; }
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
        double start=line.GetDistanceFromCharacterHit(new CharacterHit(0,0));
        double endDistance=line.GetDistanceFromCharacterHit(new CharacterHit(0,2));
        if(Math.Abs(start)>0.01 || Math.Abs(endDistance-line.Width)>0.01)
            throw new Exception("Indic cluster caret geometry does not match shaped width");
        var leading=line.GetCharacterHitFromDistance(line.Width*0.1);
        var trailing=line.GetCharacterHitFromDistance(line.Width*0.9);
        if(leading.FirstCharacterIndex!=0 || leading.TrailingLength!=0 || trailing.FirstCharacterIndex!=0 || trailing.TrailingLength!=2)
            throw new Exception("Indic point hit test splits the shaping cluster");
        var nextHit=line.GetNextCaretCharacterHit(new CharacterHit(0,0));
        var previousHit=line.GetPreviousCaretCharacterHit(new CharacterHit(0,2));
        if(nextHit.FirstCharacterIndex!=0 || nextHit.TrailingLength!=2 || previousHit.FirstCharacterIndex!=0 || previousHit.TrailingLength!=0)
            throw new Exception("Indic caret navigation splits the shaping cluster");
        var bounds=line.GetTextBounds(0,2).ToArray();
        if(bounds.Length==0 || Math.Abs(bounds.Sum(bound=>bound.Rectangle.Width)-line.Width)>0.01)
            throw new Exception("Indic selection rectangle does not match shaped width");
        Console.WriteLine("public Indic cluster caret distance, leading/trailing point hits, forward/backward caret navigation and selection bounds PASS");
        Console.WriteLine($"public source TextFormatter: length={line.Length}, width={line.Width:F3}, height={line.Height:F3}, {runs.Length} indexed glyph runs; original shaping/enumeration/GC PASS");
        foreach(string text in new[]{"alpha beta gamma delta", "कि कि कि कि", "कि"}) {
            var wrapped=new Source(text);
            int position=0,count=0; TextLineBreak previous=null;
            try {
                while(position<=text.Length) {
                    using var next=formatter.FormatLine(wrapped,position,text=="कि"?1:65,new Paragraph(true),previous);
                    if(next.Length<=0 || next.Height<=0) throw new Exception("wrapped line did not advance");
                    var glyphRuns=next.GetIndexedGlyphRuns().ToArray();
                    if(position<text.Length && glyphRuns.Length==0) throw new Exception("wrapped text lost glyphs");
                    if(position+next.Length<=text.Length && next.Width>65.01 && text!="कि") throw new Exception("wrapped line exceeds width");
                    int end=position+next.Length;
                    if(end<text.Length && (char.IsLowSurrogate(text[end]) || CharUnicodeInfo.GetUnicodeCategory(text,end)==UnicodeCategory.SpacingCombiningMark || CharUnicodeInfo.GetUnicodeCategory(text,end)==UnicodeCategory.NonSpacingMark))
                        throw new Exception("cluster split during wrapping");
                    previous?.Dispose(); previous=next.GetTextLineBreak();
                    if(previous!=null) { using var clone=previous.Clone(); }
                    position+=next.Length; ++count;
                    if(count>50) throw new Exception("wrapping did not terminate");
                }
            } finally { previous?.Dispose(); }
            if(position!=text.Length+1 || (text.Length>2 && count<2)) throw new Exception("successive wrapped lines did not cover paragraph exactly");
            Console.WriteLine($"public wrapping: {count} lines cover {text.Length} UTF16 units plus paragraph end; glyphs and cluster progress PASS");
        }
    }
}
