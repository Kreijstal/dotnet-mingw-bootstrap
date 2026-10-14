using System;
using System.Reflection;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.TextFormatting;
using System.Windows.Controls;
using System.Windows.Documents;
class JustifiedRunProperties:TextRunProperties {
    public override Typeface Typeface=>new Typeface("Nirmala UI");
    public override double FontRenderingEmSize=>24;
    public override double FontHintingEmSize=>24;
    public override TextDecorationCollection TextDecorations=>null;
    public override Brush ForegroundBrush=>Brushes.Black;
    public override Brush BackgroundBrush=>null;
    public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("hi-IN");
    public override TextEffectCollection TextEffects=>null;
}
class JustifiedParagraph:TextParagraphProperties {
    readonly System.Collections.Generic.IList<TextTabProperties> tabs;public JustifiedParagraph(bool tab=false){if(tab)tabs=new[]{new TextTabProperties(TextTabAlignment.Left,100,0,'.')};}
    public override System.Collections.Generic.IList<TextTabProperties> Tabs=>tabs;
    public override FlowDirection FlowDirection=>FlowDirection.LeftToRight;
    public override TextAlignment TextAlignment=>TextAlignment.Justify;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new JustifiedRunProperties();
    public override TextWrapping TextWrapping=>TextWrapping.Wrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
}
class ObjectNominalParagraph:JustifiedParagraph { public ObjectNominalParagraph(bool tab):base(tab){}public override TextAlignment TextAlignment=>TextAlignment.Left; }
class JustifiedBox:TextEmbeddedObject {
    readonly bool fixedSize;
    public int Formats,Draws;
    public double Available,Width,FirstAvailable;
    public Point Origin;
    public bool FailFormat,FailDraw;public int FailAt;
    public JustifiedBox(bool fixedSize=true) { this.fixedSize=fixedSize; }
    public override CharacterBufferReference CharacterBufferReference=>new CharacterBufferReference(string.Empty,0);
    public override int Length=>3;
    public override TextRunProperties Properties=>new JustifiedRunProperties();
    public override LineBreakCondition BreakBefore=>LineBreakCondition.BreakDesired;
    public override LineBreakCondition BreakAfter=>LineBreakCondition.BreakDesired;
    public override bool HasFixedSize=>fixedSize;
    public override TextEmbeddedObjectMetrics Format(double remainingParagraphWidth) {
        ++Formats;Available=remainingParagraphWidth;if(Formats==1)FirstAvailable=Available;
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
class JustifiedSource:TextSource {
 public readonly JustifiedBox Box;public readonly string Prefix,Suffix;public int End=>Prefix.Length+3+Suffix.Length;
 public JustifiedSource(JustifiedBox box,bool tab=false){Box=box;Prefix=tab?"कि\ta a ":"कि a ";Suffix=" b b c c d d e e f f g g h h i i j j k k l l m m n n o o p p q q r r s s t t";}
 public override TextRun GetTextRun(int index){if(index<Prefix.Length)return new TextCharacters(Prefix,index,Prefix.Length-index,new JustifiedRunProperties());if(index==Prefix.Length)return Box;if(index>=Prefix.Length+3&&index<End)return new TextCharacters(Suffix,index-Prefix.Length-3,End-index,new JustifiedRunProperties());if(index==End)return new TextEndOfParagraph(1);throw new Exception("source split atomic object");}
 public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit){string text=Prefix+"XXX"+Suffix;return new TextSpan<CultureSpecificCharacterBufferRange>(limit,new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("hi-IN"),new CharacterBufferRange(text,0,Math.Min(limit,text.Length))));}
 public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
 const BindingFlags I=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;const BindingFlags S=BindingFlags.Static|BindingFlags.Public|BindingFlags.NonPublic;
 static void Near(double actual,double expected,string message){if(Math.Abs(actual-expected)>0.02)throw new Exception(message+": "+actual+" / "+expected);}
 static (double First,double Width) RawCell(TextLine line,int cp){var type=line.GetType();int lscp=(int)type.GetMethod("GetInternalCp",I).Invoke(line,new object[]{cp});var subs=Array.CreateInstance(type.Assembly.GetType("MS.Internal.TextFormatting.LsQSubInfo",true),64);object[] args={lscp,subs,0,null};type.GetMethod("QueryLineCpPpoint",I).Invoke(line,args);object Field(object value,string name)=>value.GetType().GetField(name,I).GetValue(value);var cell=args[3];double Convert(object value)=>(double)type.Assembly.GetType("MS.Internal.TextFormatting.TextFormatterImp",true).GetMethod("IdealToRealWithNoRounding",S).Invoke(null,new object[]{System.Convert.ToDouble(value)});return(Convert(Field(Field(cell,"pointUvStartCell"),"x")),Convert(Field(cell,"dupCell")));}
 [STAThread]static void Main(){
  int cases=0;
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
  foreach(bool tab in new[]{false,true})
  foreach(bool optimal in new[]{false,true})
  foreach(bool fixedSize in new[]{false,true})
  foreach(bool compression in new[]{false,true}) {
   if(compression&&(!optimal||fixedSize))continue;
   using var formatter=TextFormatter.Create(mode);var box=new JustifiedBox(fixedSize);var source=new JustifiedSource(box,tab);var paragraph=new JustifiedParagraph(tab);double column=compression?(tab?280:200):500;
   if(compression){using var nominal=(TextLine)formatter.GetType().GetMethod("RecreateLine",I).Invoke(formatter,new object[]{new JustifiedSource(new JustifiedBox(false),tab),0,source.Prefix.Length+3+8,column,new ObjectNominalParagraph(tab),null,new TextRunCache()});if(nominal.Width<=column+0.02)throw new Exception("compression fixture has no nominal overflow");}
   using var line=optimal?(TextLine)formatter.GetType().GetMethod("RecreateLine",I).Invoke(formatter,new object[]{source,0,source.Prefix.Length+3+8,column,paragraph,null,new TextRunCache()}):formatter.FormatLine(source,0,column,paragraph,null);
   int cp=source.Prefix.Length;double first=line.GetDistanceFromCharacterHit(new CharacterHit(cp,0)),last=line.GetDistanceFromCharacterHit(new CharacterHit(cp,3));var raw=RawCell(line,cp);double Snap(double value)=>mode==TextFormattingMode.Display?Math.Round(value):value;
   Near(first,Snap(raw.First),"justified object leading edge");Near(last,Snap(raw.First+raw.Width),"justified object trailing edge");Near(raw.Width,box.Width,"object cell retains accepted metrics");
   if(!fixedSize){Near(box.Available,Snap(column-raw.First),"flexible object measured at final justified pen");Near(box.Width,box.Available/2,"position-dependent object accepted width");if(compression&&box.Available<=box.FirstAvailable+0.02)throw new Exception("combined compression did not move the flexible object before its nominal pen");}
   Near(line.Width,column,"settled justified right edge");Near(line.GetTextBounds(cp,3).Sum(b=>b.Rectangle.Width),last-first,"object selection");
   if(box.Formats<1||box.Formats>130||fixedSize&&box.Formats!=1)throw new Exception("bounded remeasurement and fixed-size caching");
   var next=line.GetNextCaretCharacterHit(new CharacterHit(cp,0));if(next.FirstCharacterIndex!=cp||next.TrailingLength!=3)throw new Exception("justified object caret split");
   var hit=line.GetCharacterHitFromDistance((first+last)/2);if(hit.FirstCharacterIndex!=cp||hit.TrailingLength!=0&&hit.TrailingLength!=3)throw new Exception("justified object point ownership");
   int glyphSources=line.GetIndexedGlyphRuns().Sum(g=>g.TextSourceLength);if(glyphSources!=line.Length-3-(line.Length>source.End?1:0))throw new Exception("justified object glyph/source ownership");
   var visual=new DrawingVisual();using(var dc=visual.RenderOpen())line.Draw(dc,new Point(10,10),InvertAxes.None);Near(box.Origin.X,first+10,"actual final object drawing pen");
   var bitmap=new RenderTargetBitmap(550,100,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);var pixels=new byte[550*100*4];bitmap.CopyPixels(pixels,550*4,0);int blue=0;for(int at=0;at<pixels.Length;at+=4)if(pixels[at]>200&&pixels[at+1]<40&&pixels[at+2]<40&&pixels[at+3]>200)++blue;if(blue<box.Width*20)throw new Exception("no real justified object raster");
   GC.Collect(2,GCCollectionMode.Forced,true,true);Near(line.GetDistanceFromCharacterHit(new CharacterHit(cp,0)),first,"settled object survives compacting GC");
   Console.WriteLine($"public justified object {mode}, tab={tab}, optimal={optimal}, fixed={fixedSize}, compression={compression}: pen={first}, width={box.Width}, available={box.Available}, formats={box.Formats}; exact geometry/source/raster/GC PASS");++cases;
  }
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
   using var formatter=TextFormatter.Create(mode);
   foreach(bool initial in new[]{false,true}) {
    var bad=new JustifiedBox(false){FailFormat=initial,FailAt=initial?0:2};bool rejected=false;
    try{using var line=formatter.FormatLine(new JustifiedSource(bad),0,500,new JustifiedParagraph(),null);}catch(InvalidOperationException e)when(e.Message=="object format failure"){rejected=true;}
    if(!rejected)throw new Exception("justified initial/reformat callback failure lost");bad.FailFormat=false;bad.FailAt=0;
    using var recovered=formatter.FormatLine(new JustifiedSource(bad),0,500,new JustifiedParagraph(),null);Near(recovered.Width,500,"failed remeasurement leaves formatter reusable");++cases;
   }
   var drawBad=new JustifiedBox(false){FailDraw=true};using var drawLine=formatter.FormatLine(new JustifiedSource(drawBad),0,500,new JustifiedParagraph(),null);bool drawRejected=false;
   try{var visual=new DrawingVisual();using(var dc=visual.RenderOpen())drawLine.Draw(dc,new Point(),InvertAxes.None);}catch(InvalidOperationException e)when(e.Message=="object draw failure"){drawRejected=true;}
   if(!drawRejected)throw new Exception("accepted justified object's drawing failure lost");++cases;
  }
  Console.WriteLine($"public justified flexible/fixed objects {cases} cases PASS");
 }
}
