using System;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.TextFormatting;
using System.Windows.Controls;
using System.Windows.Documents;
class AlignedRunProperties:TextRunProperties {
    public override Typeface Typeface=>new Typeface("Nirmala UI");
    public override double FontRenderingEmSize=>24;
    public override double FontHintingEmSize=>24;
    public override TextDecorationCollection TextDecorations=>null;
    public override Brush ForegroundBrush=>Brushes.Black;
    public override Brush BackgroundBrush=>null;
    public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("hi-IN");
    public override TextEffectCollection TextEffects=>null;
}
class AlignedParagraph:TextParagraphProperties {
    readonly System.Collections.Generic.IList<TextTabProperties> tabs;public AlignedParagraph(TextTabAlignment? alignment=null){if(alignment.HasValue)tabs=new[]{new TextTabProperties(alignment.Value,600,0,'.')};}
    public override System.Collections.Generic.IList<TextTabProperties> Tabs=>tabs;
    public override FlowDirection FlowDirection=>FlowDirection.LeftToRight;
    public override TextAlignment TextAlignment=>TextAlignment.Left;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new AlignedRunProperties();
    public override TextWrapping TextWrapping=>TextWrapping.Wrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
}
class AlignedBox:TextEmbeddedObject {
    readonly bool fixedSize;
    public int Formats,Draws;
    public double Available,Width;
    public Point Origin;
    public bool FailFormat,FailDraw;public int FailAt;
    public AlignedBox(bool fixedSize=true) { this.fixedSize=fixedSize; }
    public override CharacterBufferReference CharacterBufferReference=>new CharacterBufferReference(string.Empty,0);
    public override int Length=>3;
    public override TextRunProperties Properties=>new AlignedRunProperties();
    public override LineBreakCondition BreakBefore=>LineBreakCondition.BreakDesired;
    public override LineBreakCondition BreakAfter=>LineBreakCondition.BreakDesired;
    public override bool HasFixedSize=>fixedSize;
    public override TextEmbeddedObjectMetrics Format(double remainingParagraphWidth) {
        ++Formats;Available=remainingParagraphWidth;
        if(FailFormat||Formats==FailAt)throw new InvalidOperationException("object format failure");
        Width=fixedSize?70:remainingParagraphWidth/2;
        return new TextEmbeddedObjectMetrics(Width,30,20);
    }
    public override Rect ComputeBoundingBox(bool rightToLeft,bool sideways)=>new Rect(0,-20,Width,30);
    public override void Draw(DrawingContext drawing,Point origin,bool rightToLeft,bool sideways) {
        if(FailDraw)throw new InvalidOperationException("object draw failure");
        if(rightToLeft || sideways)throw new Exception("unexpected object flow");
        ++Draws;Origin=origin;drawing.DrawRectangle(Brushes.Blue,null,new Rect(origin.X,origin.Y-20,Width,30));
    }
}
class AlignedSource:TextSource {
    public readonly AlignedBox Box;readonly string plain;
    public AlignedSource(AlignedBox box){Box=box;}public AlignedSource(string text){plain=text;}
    public override TextRun GetTextRun(int index){
        if(plain!=null)return index<plain.Length?new TextCharacters(plain,index,plain.Length-index,new AlignedRunProperties()):new TextEndOfParagraph(1);
        if(index<3)return new TextCharacters("कि\t",index,3-index,new AlignedRunProperties());
        if(index==3)return Box;
        if(index>=6&&index<11)return new TextCharacters("AB.CD",index-6,11-index,new AlignedRunProperties());
        if(index==11)return new TextEndOfParagraph(1);throw new Exception("source split an atomic object");
    }
    public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit){string text=plain??"कि\tXXXAB.CD";return new TextSpan<CultureSpecificCharacterBufferRange>(limit,new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("hi-IN"),new CharacterBufferRange(text,0,Math.Min(limit,text.Length))));}
    public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
    static void Near(double actual,double expected,string message){if(Math.Abs(actual-expected)>0.02)throw new Exception(message+": "+actual+" vs "+expected);}
    [STAThread]static void Main(){
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var alignment in new[]{TextTabAlignment.Left,TextTabAlignment.Center,TextTabAlignment.Right,TextTabAlignment.Character}) {
            using var formatter=TextFormatter.Create(mode);
            using var suffix=formatter.FormatLine(new AlignedSource("AB.CD"),0,1000,new AlignedParagraph(),null);
            using var decimalPrefix=formatter.FormatLine(new AlignedSource("AB"),0,1000,new AlignedParagraph(),null);
            var box=new AlignedBox(false);
            using var line=formatter.FormatLine(new AlignedSource(box),0,800,new AlignedParagraph(alignment),null);
            double first=line.GetDistanceFromCharacterHit(new CharacterHit(3,0)),last=line.GetDistanceFromCharacterHit(new CharacterHit(3,3));
            double rawFirst=alignment==TextTabAlignment.Left?600:alignment==TextTabAlignment.Center?600-(box.Width+suffix.Width)/2:alignment==TextTabAlignment.Right?600-box.Width-suffix.Width:600-box.Width-decimalPrefix.Width;
            double Snap(double value)=>mode==TextFormattingMode.Display?Math.Round(value):value;
            Near(first,Snap(rawFirst),"aligned field leading edge");Near(last,Snap(rawFirst+box.Width),"aligned object trailing edge");
            Near(box.Available,Snap(800-rawFirst),"flexible object sees actual aligned remaining width");
            if(line.Length!=12||box.Formats<1||box.Formats>64)throw new Exception($"aligned object {alignment} format/source ownership or convergence bound: length={line.Length}, formats={box.Formats}");
            var next=line.GetNextCaretCharacterHit(new CharacterHit(3,0));if(next.FirstCharacterIndex!=3||next.TrailingLength!=3)throw new Exception("aligned object caret splits backing span");
            Near(line.GetTextBounds(3,3).Sum(b=>b.Rectangle.Width),last-first,"aligned object selection");
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            var glyphs=line.GetIndexedGlyphRuns().ToArray();if(glyphs.Sum(g=>g.TextSourceLength)!=8||glyphs.Any(g=>g.TextSourceCharacterIndex>=3&&g.TextSourceCharacterIndex<6))throw new Exception("aligned object emitted fake glyphs or lost source");
            var visual=new DrawingVisual();using(var drawing=visual.RenderOpen())line.Draw(drawing,new Point(10,10),InvertAxes.None);
            Near(box.Origin.X,first+10,"aligned object draw pen");
            var bitmap=new RenderTargetBitmap(900,100,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);var pixels=new byte[900*100*4];bitmap.CopyPixels(pixels,3600,0);
            int blue=0;for(int at=0;at<pixels.Length;at+=4)if(pixels[at]>200&&pixels[at+1]<40&&pixels[at+2]<40&&pixels[at+3]>200)++blue;
            if(blue<box.Width*25)throw new Exception("aligned object real raster missing");
            Console.WriteLine($"public aligned object {mode} {alignment}: first={first:F3}, width={box.Width:F3}, available={box.Available:F3}, formats={box.Formats}; caret/selection/source/raster/GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var alignment in new[]{TextTabAlignment.Left,TextTabAlignment.Center,TextTabAlignment.Right,TextTabAlignment.Character}) {
            using var formatter=TextFormatter.Create(mode);var box=new AlignedBox(false);var source=new AlignedSource(box);int cp=0,rows=0,objects=0;
            while(cp<12) {
                using var line=formatter.FormatLine(source,cp,650,new AlignedParagraph(alignment),null);int end=cp+line.Length;
                if(line.Length<=0||end>12||(end>3&&end<6)||line.Width>650.02)throw new Exception("aligned wrap splits source/object or exceeds column");
                if(cp<=3&&end>=6) {
                    ++objects;int suffixCount=Math.Clamp(end-6,0,5);string retained="AB.CD".Substring(0,suffixCount);
                    using var field=formatter.FormatLine(new AlignedSource(retained),0,1000,new AlignedParagraph(),null);
                    using var decimalPrefix=formatter.FormatLine(new AlignedSource(retained.Substring(0,Math.Min(retained.Length,2))),0,1000,new AlignedParagraph(),null);
                    double first=line.GetDistanceFromCharacterHit(new CharacterHit(3,0)),last=line.GetDistanceFromCharacterHit(new CharacterHit(3,3));
                    double raw=alignment==TextTabAlignment.Left?600:alignment==TextTabAlignment.Center?600-(box.Width+field.Width)/2:alignment==TextTabAlignment.Right?600-box.Width-field.Width:600-box.Width-decimalPrefix.Width;
                    double Snap(double value)=>mode==TextFormattingMode.Display?Math.Round(value):value;
                    Near(first,Snap(raw),"wrapped retained field origin");Near(last,Snap(raw+box.Width),"wrapped retained object endpoint");Near(box.Available,Snap(650-raw),"wrapped object remaining width");
                    Near(line.GetTextBounds(3,3).Sum(b=>b.Rectangle.Width),last-first,"wrapped object selection");
                    GC.Collect(2,GCCollectionMode.Forced,true,true);
                }
                cp=end;if(++rows>10)throw new Exception("aligned wrapping failed to terminate");
            }
            if(objects!=1||(alignment==TextTabAlignment.Left||alignment==TextTabAlignment.Center)&&rows<2)throw new Exception("aligned wrap loses/duplicates its object or did not wrap");
            Console.WriteLine($"public aligned object wrap {mode} {alignment}: {rows} rows, exact retained field/remaining width and source PASS");
        }
        using(var formatter=TextFormatter.Create()) {
            foreach(bool initial in new[]{false,true}) {
                var bad=new AlignedBox(false){FailFormat=initial,FailAt=initial?0:2};
                bool rejected=false;try{using var line=formatter.FormatLine(new AlignedSource(bad),0,800,new AlignedParagraph(TextTabAlignment.Center),null);}catch(InvalidOperationException e)when(e.Message=="object format failure"){rejected=true;}
                if(!rejected)throw new Exception("aligned object format failure was lost");
                bad.FailFormat=false;bad.FailAt=0;
                using var retried=formatter.FormatLine(new AlignedSource(bad),0,800,new AlignedParagraph(TextTabAlignment.Center),null);
                if(retried.Length!=12)throw new Exception("aligned object failure poisoned formatter reuse");
            }
            var drawBad=new AlignedBox(false){FailDraw=true};using var badLine=formatter.FormatLine(new AlignedSource(drawBad),0,800,new AlignedParagraph(TextTabAlignment.Right),null);
            bool rejectedDraw=false;try{var visual=new DrawingVisual();using(var drawing=visual.RenderOpen())badLine.Draw(drawing,new Point(),InvertAxes.None);}catch(InvalidOperationException e)when(e.Message=="object draw failure"){rejectedDraw=true;}
            if(!rejectedDraw)throw new Exception("aligned object drawing exception was lost");
            Console.WriteLine("public aligned object initial/reformat/draw exceptions and formatter reuse PASS");
        }
    }
}
