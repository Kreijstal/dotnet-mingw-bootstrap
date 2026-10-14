using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.TextFormatting;
using System.Windows.Threading;
class MeasureRun : TextRunProperties {
 public override Typeface Typeface=>new Typeface(new FontFamily(new Uri(AppContext.BaseDirectory), "./#Bootstrap Space Cluster"),FontStyles.Normal,FontWeights.Normal,FontStretches.Normal);public override double FontRenderingEmSize=>24;public override double FontHintingEmSize=>24;
 public override TextDecorationCollection TextDecorations=>null;public override Brush ForegroundBrush=>Brushes.Black;public override Brush BackgroundBrush=>null;
 public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("en-US");public override TextEffectCollection TextEffects=>null;
}
class MeasureParagraph : TextParagraphProperties {
 readonly FlowDirection direction;readonly TextAlignment alignment;
 public MeasureParagraph(FlowDirection direction,TextAlignment alignment=TextAlignment.Left){this.direction=direction;this.alignment=alignment;}
 public override FlowDirection FlowDirection=>direction;public override TextAlignment TextAlignment=>alignment;public override double LineHeight=>0;
 public override bool FirstLineInParagraph=>true;public override TextRunProperties DefaultTextRunProperties=>new MeasureRun();public override TextWrapping TextWrapping=>TextWrapping.Wrap;
 public override TextMarkerProperties TextMarkerProperties=>null;public override double Indent=>0;public override double DefaultIncrementalTab=>80;
}
class MeasureSource : TextSource {
 public readonly string Text;readonly bool split;
 public MeasureSource(string text,bool split=false){Text=text;this.split=split;}
 public override TextRun GetTextRun(int index)=>index<Text.Length?new TextCharacters(Text,index,split?1:Text.Length-index,new MeasureRun()):new TextEndOfParagraph(1);
 public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("en-US"),new CharacterBufferRange(Text,0,Math.Min(limit,Text.Length))));
 public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
 const BindingFlags I=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
 static void Near(double actual,double expected,string why){if(Math.Abs(actual-expected)>0.02)throw new Exception(why+": "+actual+" / "+expected);}
 [STAThread]static void Main(){
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
   using var formatter=TextFormatter.Create(mode);var paragraph=new MeasureParagraph(FlowDirection.LeftToRight,TextAlignment.Justify);string prefix="क क क";
   using var nominal=formatter.FormatLine(new MeasureSource(prefix),0,500,new MeasureParagraph(FlowDirection.LeftToRight),null);
   var original=nominal.GetIndexedGlyphRuns().ToArray();
   if(original.Sum(g=>g.GlyphRun.GlyphIndices.Count)!=7)throw new Exception("source font GSUB did not create two glyphs per single space");
   if(original.Any(g=>!g.GlyphRun.GlyphTypeface.FamilyNames.Values.Contains("Bootstrap Space Cluster")))throw new Exception("source font was replaced by fallback");
   double target=nominal.Width-14;
   using var line=(TextLine)formatter.GetType().GetMethod("RecreateLine",I).Invoke(formatter,new object[]{new MeasureSource(prefix+" क क क"),0,6,target,paragraph,null,new TextRunCache()});
   Near(line.Width,target,"multi-glyph space compression fills the real column");
   Near(line.GetTextBounds(0,5).Sum(b=>b.Rectangle.Width),target,"multi-glyph compressed selection");
   var runs=line.GetIndexedGlyphRuns().ToArray();if(runs.Sum(g=>g.TextSourceLength)!=6||runs.Sum(g=>g.GlyphRun.GlyphIndices.Count)!=9)throw new Exception("multi-glyph compression lost original source/cluster glyphs");
   if(runs.Any(g=>g.GlyphRun.AdvanceWidths.Any(a=>a<0))||!runs.Any(g=>g.GlyphRun.AdvanceWidths.Any(a=>a==0)))throw new Exception("compression did not consume a complete edge glyph without negative advances");
   for(int cp=0;cp<5;++cp){var next=line.GetNextCaretCharacterHit(new CharacterHit(cp,0));if(next.FirstCharacterIndex!=cp||next.TrailingLength!=1)throw new Exception("multi-glyph space introduced a source caret");var rect=line.GetTextBounds(cp,1).First().Rectangle;var hit=line.GetCharacterHitFromDistance(rect.Left+rect.Width*0.2);if(hit.FirstCharacterIndex!=cp||hit.TrailingLength!=0)throw new Exception("multi-glyph compressed point ownership");}
   var visual=new DrawingVisual();using(var dc=visual.RenderOpen())line.Draw(dc,new Point(10,10),InvertAxes.None);var bitmap=new RenderTargetBitmap(150,70,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);var bytes=new byte[150*70*4];bitmap.CopyPixels(bytes,150*4,0);if(bytes.Count(b=>b>150)<100)throw new Exception("source font raster absent");
   GC.Collect(2,GCCollectionMode.Forced,true,true);Near(line.Width,target,"multi-glyph line survives compacting GC");
   Console.WriteLine($"public multi-glyph space {mode}: {nominal.Width}->{line.Width}, original GSUB/source clusters, actual glyph advances, selection/carets/point/raster/GC PASS");
  }
  Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("public source-font multi-glyph space compression PASS");
 }
}
