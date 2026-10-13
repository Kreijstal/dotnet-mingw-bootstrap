using System;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.TextFormatting;
using System.Windows.Controls;
using System.Windows.Documents;
class ObjectRunProperties:TextRunProperties {
    public override Typeface Typeface=>new Typeface("Nirmala UI");
    public override double FontRenderingEmSize=>24;
    public override double FontHintingEmSize=>24;
    public override TextDecorationCollection TextDecorations=>null;
    public override Brush ForegroundBrush=>Brushes.Black;
    public override Brush BackgroundBrush=>null;
    public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("hi-IN");
    public override TextEffectCollection TextEffects=>null;
}
class ObjectParagraph:TextParagraphProperties {
    public override FlowDirection FlowDirection=>FlowDirection.LeftToRight;
    public override TextAlignment TextAlignment=>TextAlignment.Left;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new ObjectRunProperties();
    public override TextWrapping TextWrapping=>TextWrapping.Wrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
}
class BoxObject:TextEmbeddedObject {
    readonly bool fixedSize;
    public int Formats,Draws;
    public double Available,Width;
    public Point Origin;
    public bool FailFormat,FailDraw;
    public BoxObject(bool fixedSize=true) { this.fixedSize=fixedSize; }
    public override CharacterBufferReference CharacterBufferReference=>new CharacterBufferReference(string.Empty,0);
    public override int Length=>3;
    public override TextRunProperties Properties=>new ObjectRunProperties();
    public override LineBreakCondition BreakBefore=>LineBreakCondition.BreakDesired;
    public override LineBreakCondition BreakAfter=>LineBreakCondition.BreakDesired;
    public override bool HasFixedSize=>fixedSize;
    public override TextEmbeddedObjectMetrics Format(double remainingParagraphWidth) {
        ++Formats;Available=remainingParagraphWidth;
        if(FailFormat)throw new InvalidOperationException("object format failure");
        Width=fixedSize?70:Math.Min(60,remainingParagraphWidth/2);
        return new TextEmbeddedObjectMetrics(Width,30,20);
    }
    public override Rect ComputeBoundingBox(bool rightToLeft,bool sideways)=>new Rect(0,-20,Width,30);
    public override void Draw(DrawingContext drawing,Point origin,bool rightToLeft,bool sideways) {
        if(FailDraw)throw new InvalidOperationException("object draw failure");
        if(rightToLeft || sideways)throw new Exception("unexpected object flow");
        ++Draws;Origin=origin;drawing.DrawRectangle(Brushes.Blue,null,new Rect(origin.X,origin.Y-20,Width,30));
    }
}
class ObjectSource:TextSource {
    public readonly BoxObject Box;
    public ObjectSource(BoxObject box) { Box=box; }
    public override TextRun GetTextRun(int index)=>index==0?new TextCharacters("कि",new ObjectRunProperties()):index==2?Box:index==5?new TextCharacters("ab",new ObjectRunProperties()):index==7?new TextEndOfParagraph(1):throw new Exception("source split an atomic embedded object: "+index);
    public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("hi-IN"),new CharacterBufferRange("किXXXab",0,Math.Min(limit,7))));
    public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
    static void Near(double actual,double expected,string why) { if(Math.Abs(actual-expected)>0.02)throw new Exception(why+": "+actual+" vs "+expected); }
    [STAThread]
    static void Main() {
        using var formatter=TextFormatter.Create();
        foreach(bool fixedSize in new[]{true,false}) {
            var box=new BoxObject(fixedSize);var source=new ObjectSource(box);
            using var line=formatter.FormatLine(source,0,200,new ObjectParagraph(),null);
            if(line.Length!=8 || line.Height<30 || line.Baseline<20 || box.Formats!=1)throw new Exception("inline object metrics/format ownership");
            double first=line.GetDistanceFromCharacterHit(new CharacterHit(2,0));
            double last=line.GetDistanceFromCharacterHit(new CharacterHit(2,3));
            Near(last-first,box.Width,"atomic object caret width");Near(box.Available,200-first,"object remaining paragraph width");
            var leading=line.GetCharacterHitFromDistance(first+1);var trailing=line.GetCharacterHitFromDistance(last-1);
            if(leading.FirstCharacterIndex!=2 || leading.TrailingLength!=0 || trailing.FirstCharacterIndex!=2 || trailing.TrailingLength!=3)throw new Exception("object point hit splits backing span");
            var next=line.GetNextCaretCharacterHit(new CharacterHit(2,0));
            if(next.FirstCharacterIndex!=2 || next.TrailingLength!=3)throw new Exception("object caret navigation split");
            var bounds=line.GetTextBounds(2,3).ToArray();Near(bounds.Sum(b=>b.Rectangle.Width),box.Width,"object selection width");
            GC.Collect(2,GCCollectionMode.Forced,true,true);
            var runs=line.GetIndexedGlyphRuns().ToArray();
            if(runs.Sum(r=>r.TextSourceLength)!=4 || runs.Any(r=>r.TextSourceCharacterIndex>=2 && r.TextSourceCharacterIndex<5))throw new Exception("embedded object emitted fake text glyphs");
            var visual=new DrawingVisual();using(var drawing=visual.RenderOpen())line.Draw(drawing,new Point(10,10),InvertAxes.None);
            Near(box.Origin.X,10+first,"inline draw pen position");Near(box.Origin.Y,10+line.Baseline,"inline draw baseline");
            var bitmap=new RenderTargetBitmap(250,80,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);
            var pixels=new byte[250*80*4];bitmap.CopyPixels(pixels,250*4,0);int blue=0;
            for(int at=0;at<pixels.Length;at+=4)if(pixels[at]>200 && pixels[at+1]<40 && pixels[at+2]<40 && pixels[at+3]>200)++blue;
            if(blue<box.Width*25 || box.Draws!=1)throw new Exception("inline object raster missing");
            Console.WriteLine($"public inline object fixed={fixedSize}: width={box.Width:F3}, available={box.Available:F3}, {blue} blue pixels; metrics/caret/selection/enumeration/GC PASS");
        }
        var wrappedBox=new BoxObject();var wrappedSource=new ObjectSource(wrappedBox);int cp=0,count=0;
        while(cp<8) {
            using var line=formatter.FormatLine(wrappedSource,cp,80,new ObjectParagraph(),null);
            int end=cp+line.Length;if(line.Length<=0 || end>8 || end>2&&end<5)throw new Exception("wrapped inline object split or source progress");
            if(line.Width>80.02)throw new Exception("avoidable inline overflow");
            cp=end;++count;if(count>10)throw new Exception("inline wrapping stalled");
        }
        if(count<2)throw new Exception("inline object wrapping did not break");
        var bad=new BoxObject{FailFormat=true};
        try { using var line=formatter.FormatLine(new ObjectSource(bad),0,200,new ObjectParagraph(),null);throw new Exception("object format exception was lost"); }
        catch(InvalidOperationException e) when(e.Message=="object format failure") { }
        var drawBad=new BoxObject{FailDraw=true};using var badLine=formatter.FormatLine(new ObjectSource(drawBad),0,200,new ObjectParagraph(),null);
        try { var visual=new DrawingVisual();using(var drawing=visual.RenderOpen())badLine.Draw(drawing,new Point(),InvertAxes.None);throw new Exception("object draw exception was lost"); }
        catch(InvalidOperationException e) when(e.Message=="object draw failure") { }
        var block=new TextBlock{FontFamily=new FontFamily("Nirmala UI"),FontSize=24,TextWrapping=TextWrapping.Wrap,Foreground=Brushes.Black};
        var border=new Border{Width=45,Height=20,Background=Brushes.Red};
        block.Inlines.Add(new Run("कि "));block.Inlines.Add(new InlineUIContainer(border));block.Inlines.Add(new Run(" ab"));
        block.Measure(new Size(200,80));block.Arrange(new Rect(0,0,200,80));
        GC.Collect(2,GCCollectionMode.Forced,true,true);
        var uiBitmap=new RenderTargetBitmap(220,80,96,96,PixelFormats.Pbgra32);uiBitmap.Render(block);
        var uiPixels=new byte[220*80*4];uiBitmap.CopyPixels(uiPixels,220*4,0);int red=0;
        for(int at=0;at<uiPixels.Length;at+=4)if(uiPixels[at+2]>200&&uiPixels[at+1]<40&&uiPixels[at]<40&&uiPixels[at+3]>200)++red;
        Near(border.ActualWidth,45,"InlineUIContainer actual child width");Near(border.ActualHeight,20,"InlineUIContainer actual child height");
        if(red<800 || block.GetPositionFromPoint(new Point(60,10),true)==null)throw new Exception("real InlineUIContainer raster/hit testing");
        Console.WriteLine($"original TextBlock InlineUIContainer layout/child/raster/point hit after GC: {red} red pixels PASS");
        Console.WriteLine("public original TextEmbeddedObject fixed/flexible format/draw, atomic wrapping, geometry, source mapping and callback exception propagation PASS");
    }
}
