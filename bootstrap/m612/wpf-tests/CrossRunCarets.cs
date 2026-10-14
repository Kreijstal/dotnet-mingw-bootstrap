using System;
using System.Globalization;
using System.Linq;
using System.Windows.Media.Imaging;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.TextFormatting;
class CaretRunProperties:TextRunProperties {
    readonly Brush brush;public CaretRunProperties(Brush brush=null){this.brush=brush??Brushes.Black;}
    public override Typeface Typeface=>new Typeface("Nirmala UI");
    public override double FontRenderingEmSize=>24;
    public override double FontHintingEmSize=>24;
    public override TextDecorationCollection TextDecorations=>null;
    public override Brush ForegroundBrush=>brush;
    public override Brush BackgroundBrush=>null;
    public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("hi-IN");
    public override TextEffectCollection TextEffects=>null;
}
class CaretParagraph:TextParagraphProperties {
    readonly FlowDirection direction;
    public CaretParagraph(FlowDirection direction=FlowDirection.LeftToRight) { this.direction=direction; }
    public override FlowDirection FlowDirection=>direction;
    public override TextAlignment TextAlignment=>TextAlignment.Left;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new CaretRunProperties();
    public override TextWrapping TextWrapping=>TextWrapping.Wrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
}
class CaretSource:TextSource {
    public readonly string Text;readonly bool split,styled;
    public CaretSource(string text,bool split,bool styled=false) { Text=text;this.split=split;this.styled=styled; }
    public override TextRun GetTextRun(int index)=>index<Text.Length?new TextCharacters(Text,index,split?(char.IsHighSurrogate(Text[index])?2:1):Text.Length-index,new CaretRunProperties(styled?(index%2==0?Brushes.Red:Brushes.Blue):Brushes.Black)):new TextEndOfParagraph(1);
    public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("hi-IN"),new CharacterBufferRange(Text,0,Math.Min(limit,Text.Length))));
    public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
    static void Near(double actual,double expected,string message){if(Math.Abs(actual-expected)>0.05)throw new Exception(message+": "+actual+" vs "+expected);}
    [STAThread] static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
        foreach(bool styled in new[]{false,true})
        foreach(double column in new[]{1.0,400.0})
        foreach(var sample in new[]{"a\u0301b","\u0915\u093fb","\U0001f469\u200d\U0001f4bbx","\U0001f1e9\U0001f1eax"}) {
            using var formatter=TextFormatter.Create(mode);
            using var line=formatter.FormatLine(new CaretSource(sample,true,styled),0,column,new CaretParagraph(direction),null);
            Console.WriteLine($"split grapheme {mode} {direction}, styled={styled}, column={column}, {sample}: width={line.Width}");
            if(line.GetIndexedGlyphRuns().Sum(glyph=>glyph.TextSourceLength)!=Math.Min(sample.Length,line.Length))throw new Exception("split grapheme glyph enumeration loses source ownership");
            int length=StringInfo.GetNextTextElement(sample,0).Length;
            if(column==1&&line.Length!=length)throw new Exception("tiny-width wrap splits or skips a grapheme");
            double first=line.GetDistanceFromCharacterHit(new CharacterHit(0,0));
            double last=line.GetDistanceFromCharacterHit(new CharacterHit(0,length));
            Console.WriteLine($"split grapheme {mode}: first={first}, last={last}, next={line.GetNextCaretCharacterHit(new CharacterHit(0,0)).FirstCharacterIndex}/{line.GetNextCaretCharacterHit(new CharacterHit(0,0)).TrailingLength}");
            var next=line.GetNextCaretCharacterHit(new CharacterHit(0,0));
            if(next.FirstCharacterIndex!=0||next.TrailingLength!=length)throw new Exception("split grapheme has an interior forward caret");
            var previous=line.GetPreviousCaretCharacterHit(new CharacterHit(0,length));
            if(previous.FirstCharacterIndex!=0||previous.TrailingLength!=0)throw new Exception("split grapheme has an interior backward caret");
            for(int cp=0;cp<length;++cp){var hit=line.GetNextCaretCharacterHit(new CharacterHit(cp,0));if(hit.FirstCharacterIndex!=0||hit.TrailingLength!=length)throw new Exception("interior code-unit query loses its complete source cell");}
            var middle=line.GetCharacterHitFromDistance((first+last)/2+0.1);
            if(middle.FirstCharacterIndex!=0||(middle.TrailingLength!=0&&middle.TrailingLength!=length))throw new Exception("split grapheme point hit stops inside a text element");
            double width=line.GetTextBounds(0,length).Sum(b=>b.Rectangle.Width);Near(width,Math.Abs(last-first),"split grapheme selection width");
            GC.Collect(2,GCCollectionMode.Forced,true,true);Near(line.GetDistanceFromCharacterHit(new CharacterHit(0,length)),last,"split grapheme caret lifetime");
            var visual=new DrawingVisual();using(var drawing=visual.RenderOpen())line.Draw(drawing,new Point(column==1&&direction==FlowDirection.RightToLeft?200:10,10),InvertAxes.None);
            var bitmap=new RenderTargetBitmap(400,80,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);var pixels=new byte[400*80*4];bitmap.CopyPixels(pixels,1600,0);
            if(!pixels.Where((v,i)=>i%4==3).Any(alpha=>alpha>150))throw new Exception("split grapheme raster empty");
            Console.WriteLine("public split grapheme caret/point/selection/GC/raster PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            using var formatter=TextFormatter.Create(mode);
            using var line=formatter.FormatLine(new CaretSource("office",false),0,400,new CaretParagraph(),null);
            for(int cp=0;cp<6;++cp){var next=line.GetNextCaretCharacterHit(new CharacterHit(cp,0));if(next.FirstCharacterIndex!=cp||next.TrailingLength!=1)throw new Exception("ordinary same-run character carets changed");}
            Console.WriteLine("public ordinary same-run character carets PASS");
        }
    }
}
