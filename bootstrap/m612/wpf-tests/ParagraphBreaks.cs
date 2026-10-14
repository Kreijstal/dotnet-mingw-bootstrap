using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.TextFormatting;
class BreakRun : TextRunProperties {
 public override Typeface Typeface=>new Typeface("Arial");public override double FontRenderingEmSize=>24;public override double FontHintingEmSize=>24;
 public override TextDecorationCollection TextDecorations=>null;public override Brush ForegroundBrush=>Brushes.Black;public override Brush BackgroundBrush=>null;
 public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("en-US");public override TextEffectCollection TextEffects=>null;
}
class BreakParagraph : TextParagraphProperties {
 readonly FlowDirection direction;readonly TextAlignment alignment;
 public BreakParagraph(FlowDirection direction,TextAlignment alignment=TextAlignment.Left){this.direction=direction;this.alignment=alignment;}
 public override FlowDirection FlowDirection=>direction;public override TextAlignment TextAlignment=>alignment;public override double LineHeight=>0;
 public override bool FirstLineInParagraph=>true;public override TextRunProperties DefaultTextRunProperties=>new BreakRun();public override TextWrapping TextWrapping=>TextWrapping.Wrap;
 public override TextMarkerProperties TextMarkerProperties=>null;public override double Indent=>0;public override double DefaultIncrementalTab=>80;
}
class BreakSource : TextSource {
 public readonly string Text;readonly bool split;
 public BreakSource(string text,bool split=false){Text=text;this.split=split;}
 public override TextRun GetTextRun(int index)=>index<Text.Length?new TextCharacters(Text,index,split?1:Text.Length-index,new BreakRun()):new TextEndOfParagraph(1);
 public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("en-US"),new CharacterBufferRange(Text,0,Math.Min(limit,Text.Length))));
 public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
 static readonly BindingFlags Any=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
 static object Call(object obj,string name,params object[] arguments){try{return obj.GetType().GetMethod(name,Any).Invoke(obj,arguments);}catch(TargetInvocationException e){throw e.InnerException;}}
 static T Value<T>(object obj,string name)=>(T)obj.GetType().GetProperty(name,Any).GetValue(obj);
 static void Require(bool value,string why){if(!value)throw new Exception(why);}
 static void CandidateMatrix(TextFormattingMode mode,FlowDirection direction,bool split,string text,double width) {
  using var formatter=TextFormatter.Create(mode);var source=new BreakSource(text,split);var paragraph=new BreakParagraph(direction);var runs=new TextRunCache();
  object cache=Call(formatter,"CreateParagraphCache",source,0,width+60,paragraph,null,runs);TextLineBreak previous=null;int cp=0,rows=0,alternatives=0;
  try {
   while(cp<=text.Length){
    object[] args={cp,previous,IntPtr.Zero,width,0};var breaks=((IEnumerable)Call(cache,"FormatBreakpoints",args)).Cast<object>().ToArray();int best=(int)args[4];
    Require(breaks.Length>0&&best>=0&&best<breaks.Length,"nonempty feasible candidate set and valid best index");
    int last=0;var lengths=new HashSet<int>();
    foreach(object candidate in breaks){
     int length=Value<int>(candidate,"Length"),end=cp+length;Require(length>0&&end<=text.Length+1&&length>=last,"ordered source break positions");last=length;lengths.Add(length);
     if(end<text.Length){Require(!char.IsLowSurrogate(text[end]),"surrogate-safe break");var category=CharUnicodeInfo.GetUnicodeCategory(text,end);Require(category!=UnicodeCategory.NonSpacingMark&&category!=UnicodeCategory.SpacingCombiningMark,"grapheme-safe break");}
     using var line=(TextLine)Call(formatter,"RecreateLine",source,cp,length,width,paragraph,previous,runs);
     Require(line.Length==length,"reconstructed candidate preserves source length");
     Require(line.Width<=width+0.02||Value<bool>(candidate,"IsTruncated"),"actual reshaped candidate fits its width");
     var glyphs=line.GetIndexedGlyphRuns().ToArray();Require(glyphs.Sum(x=>x.TextSourceLength)==Math.Min(length,text.Length-cp)-text.Substring(cp,Math.Min(length,text.Length-cp)).Count(c=>c=='\u2028'),"candidate glyphs cover exact source");
     var visual=new DrawingVisual();using(var dc=visual.RenderOpen())line.Draw(dc,new Point(10,10),InvertAxes.None);
     var image=new RenderTargetBitmap(400,80,96,96,PixelFormats.Pbgra32);image.Render(visual);
     var pixels=new byte[400*80*4];image.CopyPixels(pixels,400*4,0);Require(pixels.Any(x=>x!=0),"candidate actual raster");
    }
    alternatives+=breaks.Length;
    if(cp==0&&text=="one two three four five six seven eight")Require(lengths.Count>=2,"enumerates multiple alternatives rather than only greedy line");
    object selected=breaks[best];int consumed=Value<int>(selected,"Length");var next=(TextLineBreak)Call(selected,"GetTextLineBreak");
    if(next!=null){var clone=next.Clone();next.Dispose();next=clone;}
    foreach(IDisposable candidate in breaks)candidate.Dispose();previous?.Dispose();previous=next;cp+=consumed;++rows;
    GC.Collect(2,GCCollectionMode.Forced,true,true);Require(rows<100,"paragraph break progress");
   }
   Require(cp==text.Length+1,"paragraph continuation covers all source plus EOP");
  } finally {previous?.Dispose();((IDisposable)cache).Dispose();}
  Console.WriteLine($"original paragraph cache {mode} {direction}, split={split}: {rows} rows, {alternatives} feasible variants, exact reconstruction/raster/grapheme/clone/GC PASS");
 }
 [STAThread] static void Main(){
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})foreach(bool split in new[]{false,true}) {
   CandidateMatrix(mode,direction,split,"one two three four five six seven eight",180);
   CandidateMatrix(mode,direction,split,"alpha a\u0301b emoji \U0001F642 beta gamma",160);
   CandidateMatrix(mode,direction,split,"abcdefghijklmno",45);
   CandidateMatrix(mode,direction,split,"first line\u2028second line again",130);
   CandidateMatrix(mode,direction,split,"abc שלום def مرحبا ghi",150);
  }
  Console.WriteLine("original managed paragraph sessions and feasible Unicode line breakpoints PASS");
 }
}
