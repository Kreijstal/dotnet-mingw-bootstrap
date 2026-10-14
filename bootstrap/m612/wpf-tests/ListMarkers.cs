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
class MarkerRun : TextRunProperties {
 readonly bool marker;public MarkerRun(bool marker=false){this.marker=marker;}
 public override Typeface Typeface=>new Typeface("Arial");public override double FontRenderingEmSize=>24;public override double FontHintingEmSize=>24;
 public override TextDecorationCollection TextDecorations=>null;public override Brush ForegroundBrush=>marker?Brushes.Red:Brushes.Black;public override Brush BackgroundBrush=>null;
 public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("en-US");public override TextEffectCollection TextEffects=>null;
}
class MarkerSource : TextSource {
 public readonly string Text;readonly bool marker,split;
 public MarkerSource(string text,bool marker=false,bool split=false){Text=text;this.marker=marker;this.split=split;}
 public override TextRun GetTextRun(int index)=>index<Text.Length?new TextCharacters(Text,index,split?1:Text.Length-index,new MarkerRun(marker)):new TextEndOfParagraph(1);
 public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("en-US"),new CharacterBufferRange(Text,0,Math.Min(limit,Text.Length))));
 public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class MarkerProperties : TextMarkerProperties {
 readonly double offset;readonly TextSource source;
 public MarkerProperties(string text,double offset,bool split=false){this.offset=offset;source=new MarkerSource(text,true,split);}
 public override double Offset=>offset;public override TextSource TextSource=>source;
}
class MarkerParagraph : TextParagraphProperties {
 readonly FlowDirection direction;readonly TextMarkerProperties marker;readonly double indent;readonly bool first,wrap;
 public MarkerParagraph(FlowDirection d,TextMarkerProperties marker=null,double indent=0,bool first=true,bool wrap=false){direction=d;this.marker=marker;this.indent=indent;this.first=first;this.wrap=wrap;}
 public override FlowDirection FlowDirection=>direction;public override TextAlignment TextAlignment=>TextAlignment.Left;public override double LineHeight=>0;
 public override bool FirstLineInParagraph=>first;public override TextRunProperties DefaultTextRunProperties=>new MarkerRun();public override TextWrapping TextWrapping=>wrap?TextWrapping.Wrap:TextWrapping.NoWrap;
 public override TextMarkerProperties TextMarkerProperties=>marker;public override double Indent=>indent;
}
class Check {
 const BindingFlags I=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
 static (int Red,int Black,int MinRed,int MaxRed) Ink(Visual visual,int w=650,int h=150){var bitmap=new RenderTargetBitmap(w,h,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);var bytes=new byte[w*h*4];bitmap.CopyPixels(bytes,w*4,0);int red=0,black=0,min=w,max=-1;for(int y=0;y<h;++y)for(int x=0;x<w;++x){int at=(y*w+x)*4;if(bytes[at+3]<120)continue;if(bytes[at+2]>150&&bytes[at]<80&&bytes[at+1]<80){++red;min=Math.Min(min,x);max=Math.Max(max,x);}if(bytes[at]<80&&bytes[at+1]<80&&bytes[at+2]<80)++black;}return(red,black,min,max);}
 static int MarkerCharacters(Drawing d){if(d is GlyphRunDrawing g&&g.ForegroundBrush is SolidColorBrush brush&&brush.Color==Colors.Red)return g.GlyphRun.Characters.Count;return d is DrawingGroup group?group.Children.Sum(MarkerCharacters):0;}
 static int MarkerCharacters(Visual v){int n=MarkerCharacters(VisualTreeHelper.GetDrawing(v));for(int i=0;i<VisualTreeHelper.GetChildrenCount(v);++i)n+=MarkerCharacters((Visual)VisualTreeHelper.GetChild(v,i));return n;}
 static DrawingVisual Draw(TextLine line){var v=new DrawingVisual();using(var d=v.RenderOpen())line.Draw(d,new Point(150,40),InvertAxes.None);return v;}
 static void Near(double a,double b,string why){if(Math.Abs(a-b)>0.02)throw new Exception(why+": "+a+" vs "+b);}
 [STAThread] static void Main(){
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})foreach(bool split in new[]{false,true})foreach(string symbol in new[]{"•","42.","iv.","שלום"})foreach(double indent in new[]{0.0,40.0}) {
   using var formatter=TextFormatter.Create(mode);var source=new MarkerSource("alpha beta gamma");var marker=new MarkerProperties(symbol,indent==0?-20:10,split);
   using var plain=formatter.FormatLine(source,0,400,new MarkerParagraph(direction,indent:indent),null);
   using var line=formatter.FormatLine(source,0,400,new MarkerParagraph(direction,marker,indent),null);
   if(line.Length!=plain.Length)throw new Exception("marker consumes body source");Near(line.Width,plain.Width,"marker changes measured width");Near(line.Start,plain.Start,"marker changes body indent");Near(line.Height,plain.Height,"marker changes line height");Near(line.Baseline,plain.Baseline,"marker changes baseline");
   var glyphs=line.GetIndexedGlyphRuns().ToArray();if(glyphs.Sum(g=>g.TextSourceLength)!=source.Text.Length||glyphs.Any(g=>g.TextSourceCharacterIndex<0))throw new Exception("marker leaks into backing-source glyph enumeration");
   for(int cp=0;cp<source.Text.Length;++cp)Near(line.GetDistanceFromCharacterHit(new CharacterHit(cp,0)),plain.GetDistanceFromCharacterHit(new CharacterHit(cp,0)),"marker moves caret");
   var ink=Ink(Draw(line));if(ink.Red<10||ink.Black<100)throw new Exception($"marker or body missing raster: {ink}");
   using var empty=formatter.FormatLine(new MarkerSource(""),0,400,new MarkerParagraph(direction,marker,indent),null);using var emptyPlain=formatter.FormatLine(new MarkerSource(""),0,400,new MarkerParagraph(direction,indent:indent),null);if(empty.Length!=emptyPlain.Length||Ink(Draw(empty)).Red<10)throw new Exception("empty marked line source/raster");Near(empty.Width,emptyPlain.Width,"marker changes empty-line indent/width");Near(empty.Height,emptyPlain.Height,"marker changes empty-line height");
   Console.WriteLine($"original marker {mode} {direction} {symbol}, split={split}, indent={indent}: red={ink.Red}, body={ink.Black}, unchanged source/width/height/carets PASS");
  }
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft}) {
   using var formatter=TextFormatter.Create(mode);var source=new MarkerSource("alpha beta gamma delta epsilon zeta");var paragraph=new MarkerParagraph(direction,new MarkerProperties("42.",-20),0,true,true);
   object cache=formatter.GetType().GetMethod("CreateParagraphCache",I).Invoke(formatter,new object[]{source,0,140.0,paragraph,null,new TextRunCache()});TextLineBreak previous=null;int cp=0,rows=0;
   try{while(cp<=source.Text.Length){object[] args={cp,previous,IntPtr.Zero,140.0,0};var candidates=((IEnumerable)cache.GetType().GetMethod("FormatBreakpoints",I).Invoke(cache,args)).Cast<object>().ToArray();var selected=candidates[(int)args[4]];int length=(int)selected.GetType().GetProperty("Length",I).GetValue(selected);using var line=(TextLine)formatter.GetType().GetMethod("RecreateLine",I).Invoke(formatter,new object[]{source,cp,length,140.0,new MarkerParagraph(direction,paragraph.TextMarkerProperties,0,cp==0,true),previous,new TextRunCache()});if(line.Length!=length||line.Width>140.02)throw new Exception("marked cache source/width");var ink=Ink(Draw(line));if(cp==0?ink.Red<10:ink.Red!=0)throw new Exception($"marked cache repeats or loses first marker: mode={mode}, direction={direction}, cp={cp}, length={length}, red={ink.Red}, previous={previous!=null}");using var plain=(TextLine)formatter.GetType().GetMethod("RecreateLine",I).Invoke(formatter,new object[]{source,cp,length,140.0,new MarkerParagraph(direction,first:cp==0,wrap:true),previous,new TextRunCache()});Near(line.GetDistanceFromCharacterHit(new CharacterHit(cp,0)),plain.GetDistanceFromCharacterHit(new CharacterHit(cp,0)),"cached marker displaces main-text origin");var next=(TextLineBreak)selected.GetType().GetMethod("GetTextLineBreak",I).Invoke(selected,null);if(next!=null){var clone=next.Clone();next.Dispose();next=clone;}foreach(IDisposable candidate in candidates)candidate.Dispose();previous?.Dispose();previous=next;cp+=length;if(++rows>20)throw new Exception("marked cache progress");}}
   finally{previous?.Dispose();((IDisposable)cache).Dispose();}
   if(cp!=source.Text.Length+1||rows<2)throw new Exception("marked cache exact paragraph coverage");Console.WriteLine($"original marked paragraph cache {mode} {direction}: {rows} reconstructed rows, single marker, exact body source and cloned continuation PASS");
  }
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})foreach(bool optimal in new[]{false,true})foreach(var style in new[]{TextMarkerStyle.Disc,TextMarkerStyle.Decimal,TextMarkerStyle.LowerRoman}) {
   var list=new System.Windows.Documents.List{MarkerStyle=style,StartIndex=8,Foreground=Brushes.Red,Margin=new Thickness(0),Padding=new Thickness(45,0,0,0)};var runs=new List<Run>();
   for(int n=0;n<4;++n){var run=new Run($"Item {n}: alpha beta gamma delta epsilon zeta.");runs.Add(run);var item=new ListItem(new Paragraph(run){Foreground=Brushes.Black,Margin=new Thickness(0)});list.ListItems.Add(item);}
   var doc=new FlowDocument(list){FontFamily=new FontFamily("Arial"),FontSize=24,FlowDirection=direction,IsOptimalParagraphEnabled=optimal,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};TextOptions.SetTextFormattingMode(doc,mode);
   var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)doc).DocumentPaginator;paginator.PageSize=new Size(300,180);var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,red=0,markerChars=0;
   for(;pages<20;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;var ink=Ink(page.Visual,300,180);red+=ink.Red;markerChars+=MarkerCharacters(page.Visual);if(ink.Black<20)throw new Exception("public list body raster missing");object view=((IServiceProvider)page).GetService(viewType);foreach(object segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();ranges.Add((pages,(TextPointer)t.GetProperty("Start",I).GetValue(segment),(TextPointer)t.GetProperty("End",I).GetValue(segment)));}GC.Collect(2,GCCollectionMode.Forced,true,true);}
   if(markerChars!=(style==TextMarkerStyle.Disc?4:style==TextMarkerStyle.Decimal?10:13))throw new Exception($"markers repeated or lost across pages: style={style}, characters={markerChars}");
   if(pages<2||pages>=20||red<30||!paginator.IsPageCountValid)throw new Exception($"public list markers absent: pages={pages}, red={red}");int count=0;
   foreach(var run in runs)for(int n=0;n<run.Text.Length;++n){var pos=run.ContentStart.GetPositionAtOffset(n,LogicalDirection.Forward);var owners=ranges.Where(r=>pos.CompareTo(r.Start)>=0&&pos.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(pos)!=owners[0].Page)throw new Exception("list marker corrupts source/page identity");++count;}
   using(var page=paginator.GetPage(0))if(Ink(page.Visual,300,180).Red<10)throw new Exception("disposed list page loses marker regeneration");Console.WriteLine($"public list {style} {mode} {direction}, optimal={optimal}: {pages} pages, red={red}, markerChars={markerChars}, exact {count} source chars/page mapping/regeneration/GC PASS");
  }
  Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original source marker callback, separate negative CPs, public bullets/numbering and cleanup PASS");
 }
}
