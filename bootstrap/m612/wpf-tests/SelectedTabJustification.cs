using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.TextFormatting;
class TabRunProperties:TextRunProperties {
    public override Typeface Typeface=>new Typeface("Nirmala UI");
    public override double FontRenderingEmSize=>24;
    public override double FontHintingEmSize=>24;
    public override TextDecorationCollection TextDecorations=>null;
    public override Brush ForegroundBrush=>Brushes.Black;
    public override Brush BackgroundBrush=>null;
    public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("hi-IN");
    public override TextEffectCollection TextEffects=>null;
}
class TabParagraph:TextParagraphProperties {
    readonly IList<TextTabProperties> tabs;
    readonly bool wrap;
    readonly double increment;
    readonly TextAlignment alignment;
    public TabParagraph(IList<TextTabProperties> value=null,bool wrap=false,double increment=80,TextAlignment alignment=TextAlignment.Left) { tabs=value;this.wrap=wrap;this.increment=increment;this.alignment=alignment; }
    public override FlowDirection FlowDirection=>FlowDirection.LeftToRight;
    public override TextAlignment TextAlignment=>alignment;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new TabRunProperties();
    public override TextWrapping TextWrapping=>wrap?TextWrapping.Wrap:TextWrapping.NoWrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
    public override double DefaultIncrementalTab=>increment;
    public override IList<TextTabProperties> Tabs=>tabs;
}
class TabSource:TextSource {
    public readonly string Text;
    readonly bool split;
    public TabSource(string text="कि\tAB.CD",bool split=false) { Text=text;this.split=split; }
    public override TextRun GetTextRun(int index)=>index<Text.Length?new TextCharacters(Text,index,split && index>=3?1:Text.Length-index,new TabRunProperties()):new TextEndOfParagraph(1);
    public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("hi-IN"),new CharacterBufferRange(Text,0,Math.Min(limit,Text.Length))));
    public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
 static void Near(double actual,double expected,string why){if(Math.Abs(actual-expected)>0.02)throw new Exception(why+": "+actual+" vs "+expected);}
 static double At(TextLine line,int cp)=>line.GetDistanceFromCharacterHit(new CharacterHit(cp,0));
 [STAThread] static void Main(){
  int cases=0;
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
  foreach(bool split in new[]{false,true})
  foreach(var earlier in new[]{TextTabAlignment.Left,TextTabAlignment.Center,TextTabAlignment.Right,TextTabAlignment.Character})
  foreach(bool fallback in new[]{false,true}) {
   using var formatter=TextFormatter.Create(mode);
   var tabs=new List<TextTabProperties>{new TextTabProperties(earlier,100,0,'.')};
   if(!fallback){tabs.Add(new TextTabProperties(TextTabAlignment.Left,180,0,0));tabs.Add(new TextTabProperties(TextTabAlignment.Right,700,0,0));}
   var source=new TabSource("a\tAB\t aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt",split);
   var paragraph=new TabParagraph(tabs,true,180,TextAlignment.Justify);
   using var nominal=formatter.FormatLine(source,0,317,new TabParagraph(tabs,true,180),null);
   using var first=formatter.FormatLine(source,0,317,paragraph,null);
   if(first.Length!=nominal.Length||first.Length<=9||first.Length>=source.Text.Length)throw new Exception("retained justified tab field");
   Near(first.Width,317,"justified right edge");Near(At(first,2),At(nominal,2),"earlier aligned field start");Near(At(first,4),At(nominal,4),"earlier aligned field end");Near(At(first,5),180,"last actual left stop");
   Near(first.GetTextBounds(4,1).Sum(b=>b.Rectangle.Width),At(first,5)-At(first,4),"last tab selection");
   bool expanded=false;
   for(int cp=5;cp<first.Length-1;++cp){double begin=At(first,cp),end=At(first,cp+1);if(end>begin){var hit=first.GetCharacterHitFromDistance(begin+(end-begin)*0.2);if(hit.FirstCharacterIndex!=cp||hit.TrailingLength!=0)throw new Exception("expanded field point/caret ownership");}if(source.Text[cp]==' '&&end-begin>At(nominal,cp+1)-At(nominal,cp)+0.02)expanded=true;}
   if(!expanded)throw new Exception("no actual final-field space expanded: "+mode+", nominal="+nominal.Width+", justified="+first.Width+", length="+first.Length+", cells="+string.Join(";",Enumerable.Range(5,first.Length-6).Select(cp=>$"{cp}:{source.Text[cp]}:{At(first,cp+1)-At(first,cp)}/{At(nominal,cp+1)-At(nominal,cp)}")));
   var visual=new DrawingVisual();using(var drawing=visual.RenderOpen())first.Draw(drawing,new Point(10,10),InvertAxes.None);
   var bitmap=new RenderTargetBitmap(340,80,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);var pixels=new byte[340*80*4];bitmap.CopyPixels(pixels,340*4,0);if(pixels.Count(b=>b>100)<50)throw new Exception("no real justified tab raster");
   int position=first.Length,rows=1;TextLineBreak previous=first.GetTextLineBreak();
   try{while(position<=source.Text.Length){using var next=formatter.FormatLine(source,position,317,paragraph,previous);if(next.Length<=0||next.Width>317.02)throw new Exception("justified tab continuation");position+=next.Length;previous?.Dispose();previous=next.GetTextLineBreak();if(++rows>20)throw new Exception("continuation loop");}}finally{previous?.Dispose();}
   if(position!=source.Text.Length+1)throw new Exception("exact tabbed paragraph source completion");
   GC.Collect(2,GCCollectionMode.Forced,true,true);Near(At(first,5),180,"retained field after compacting GC");
   Console.WriteLine($"public selected tab justification {mode}, split={split}, earlier={earlier}, fallback={fallback}, rows={rows}: anchor, width, caret/point, selection, raster, source and GC PASS");++cases;
  }
  Console.WriteLine($"public selected tab justification {cases} cases PASS");
 }
}
