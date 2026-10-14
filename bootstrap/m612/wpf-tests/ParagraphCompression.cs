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
 public override Typeface Typeface=>new Typeface("Consolas");public override double FontRenderingEmSize=>24;public override double FontHintingEmSize=>24;
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
 static double At(TextLine line,int cp)=>line.GetDistanceFromCharacterHit(new CharacterHit(cp,0));
 static object Prop(object o,string name)=>o.GetType().GetProperty(name,I).GetValue(o);
 static readonly Type View=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
 static IEnumerable<object> ParaLines(object para){var p=para.GetType().GetProperty("Lines",I);if(p!=null){var values=(IEnumerable)p.GetValue(para);if(values!=null)foreach(object line in values)yield return line;}p=para.GetType().GetProperty("Paragraphs",I);if(p!=null)foreach(object child in (IEnumerable)p.GetValue(para))foreach(var line in ParaLines(child))yield return line;}
 static IEnumerable<object> Lines(DocumentPage page){object view=((IServiceProvider)page).GetService(View);foreach(object column in (IEnumerable)Prop(view,"Columns"))foreach(object para in (IEnumerable)Prop(column,"Paragraphs"))foreach(object line in ParaLines(para))yield return line;}
 static List<int> Format(string body,double width,bool optimal,TextFormattingMode mode,FlowDirection direction,double pageHeight=400,bool hyphenation=false,bool justified=false,bool columns=false){
  var run=new Run(body);var document=new FlowDocument(new Paragraph(run){Margin=new Thickness(0)}){FontFamily=new FontFamily("Consolas"),FontSize=24,PagePadding=new Thickness(10),ColumnWidth=columns?width:double.PositiveInfinity,ColumnGap=20,IsColumnWidthFlexible=false,IsOptimalParagraphEnabled=optimal,IsHyphenationEnabled=hyphenation,TextAlignment=justified?TextAlignment.Justify:TextAlignment.Left,FlowDirection=direction};TextOptions.SetTextFormattingMode(document,mode);
  var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(columns?width*2+40:width+20,pageHeight);
  var cuts=new List<int>();var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,total=0,carets=0,hyphens=0,expanded=0;
  for(;pages<20;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;
   hyphens+=Markers(page.Visual);
   object view=((IServiceProvider)page).GetService(View);var local=new List<(TextPointer Start,TextPointer End)>();
   foreach(object segment in (IEnumerable)View.GetProperty("TextSegments").GetValue(view)){var start=(TextPointer)Prop(segment,"Start");var end=(TextPointer)Prop(segment,"End");ranges.Add((pages,start,end));local.Add((start,end));}
   for(int at=0;at<Math.Min(24,body.Length);++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);if(!local.Any(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0))continue;var rect=(Rect)View.GetMethod("GetRectangleFromTextPosition").Invoke(view,new object[]{position});if(rect.IsEmpty||rect.Height<=0)throw new Exception("optimized caret geometry");if(View.GetMethod("GetTextPositionFromPoint").Invoke(view,new object[]{new Point(rect.Left+1,rect.Top+rect.Height/2),true})==null)throw new Exception("optimized point hit");++carets;}
   foreach(object line in Lines(page)){var end=(TextPointer)Prop(line,"EndPosition");int cp=run.ContentStart.GetOffsetToPosition(end);cuts.Add(Math.Clamp(cp,0,body.Length));if(justified&&cp<body.Length){var box=(Rect)Prop(line,"LayoutBox");if(box.Width>width+0.02)throw new Exception("optimized justification exceeds column");if(Math.Abs(box.Width-width)>0.02)throw new Exception("mixed provider capacity did not fill a nonfinal line: "+box.Width);++expanded;}}
   var bitmap=new RenderTargetBitmap((int)Math.Ceiling(columns?width*2+40:width+20),(int)Math.Ceiling(pageHeight),96,96,PixelFormats.Pbgra32);bitmap.Render(page.Visual);var bytes=new byte[bitmap.PixelWidth*bitmap.PixelHeight*4];bitmap.CopyPixels(bytes,bitmap.PixelWidth*4,0);int ink=bytes.Count(x=>x>100);if(ink<100)throw new Exception("no real optimal paragraph raster");total+=ink;GC.Collect(2,GCCollectionMode.Forced,true,true);
  }
  if(pages==0||pages>=20||cuts.Count==0||cuts.Last()!=body.Length)throw new Exception($"optimal paragraph exact completion: optimal={optimal}, pages={pages}, cuts={string.Join(",",cuts)}, expected={body.Length}");
  for(int at=0;at<body.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("optimized source/page identity");}
  if(justified&&expanded==0)throw new Exception("optimized justification never fills a nonfinal line");
  if(hyphenation&&hyphens==0)throw new Exception("optimized dictionary break loses inserted hyphen on reconstruction");
  using(var page=paginator.GetPage(0))if(!Lines(page).Any())throw new Exception("disposed optimal page regeneration");
  Console.WriteLine($"public paragraph optimal={optimal} {mode} {direction}: cuts={string.Join(",",cuts)}, pages={pages}, ink={total}, carets={carets}, hyphens={hyphens}, expanded={expanded}, exact {body.Length} source/page positions PASS");return cuts;
 }
 static int Markers(Drawing d){if(d is GlyphRunDrawing g)return g.GlyphRun.Characters.Count(c=>c=='-');return d is DrawingGroup group?group.Children.Sum(Markers):0;}
 static int Markers(Visual v){int n=Markers(VisualTreeHelper.GetDrawing(v));for(int i=0;i<VisualTreeHelper.GetChildrenCount(v);++i)n+=Markers((Visual)VisualTreeHelper.GetChild(v,i));return n;}
 [STAThread] static void Main(){
  int cases=0;
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
  foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
  foreach(bool split in new[]{false,true})
  foreach(string prefix in new[]{"aa bb cc","कि कि कि"})
  foreach(bool terminal in new[]{false,true}) {
   using var formatter=TextFormatter.Create(mode);
   var source=new MeasureSource(terminal?prefix:prefix+" dd ee ff gg hh ii jj",split);var paragraph=new MeasureParagraph(direction,TextAlignment.Justify);
   using var nominal=formatter.FormatLine(new MeasureSource(prefix,split),0,500,new MeasureParagraph(direction),null);
   Near(nominal.GetTextBounds(0,prefix.Length).Sum(b=>b.Rectangle.Width),nominal.Width,"nominal split-grapheme enclosing selection");
   double target=nominal.Width-4;
   using var compressed=(TextLine)formatter.GetType().GetMethod("RecreateLine",I).Invoke(formatter,new object[]{source,0,prefix.Length+1,target,paragraph,null,new TextRunCache()});
   Near(compressed.Width,target,"mixed reconstruction must actually compress");
   if(compressed.GetIndexedGlyphRuns().Sum(g=>g.TextSourceLength)!=(terminal?prefix.Length:prefix.Length+1))throw new Exception("compression glyph source ownership");
   if(compressed.Length!=prefix.Length+1)throw new Exception("compression lost retained source");
   Near(compressed.GetTextBounds(0,prefix.Length).Sum(b=>b.Rectangle.Width),target,"compressed selection covers exact visible text");
   for(int cp=0;cp<prefix.Length;){var next=compressed.GetNextCaretCharacterHit(new CharacterHit(cp,0));int end=next.FirstCharacterIndex+next.TrailingLength;if(end<=cp||end>prefix.Length+1)throw new Exception("compressed grapheme progress");var back=compressed.GetPreviousCaretCharacterHit(next);if(back.FirstCharacterIndex+back.TrailingLength!=cp)throw new Exception("compressed reverse caret");var rect=compressed.GetTextBounds(cp,end-cp).First().Rectangle;var hit=compressed.GetCharacterHitFromDistance(rect.Left+rect.Width/2);if(hit.FirstCharacterIndex!=cp||(hit.TrailingLength!=0&&hit.TrailingLength!=end-cp))throw new Exception("compressed point hit changed grapheme ownership");cp=end;}
   var visual=new DrawingVisual();using(var dc=visual.RenderOpen())compressed.Draw(dc,new Point(10,10),InvertAxes.None);var bitmap=new RenderTargetBitmap(500,90,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);var bytes=new byte[500*90*4];bitmap.CopyPixels(bytes,500*4,0);if(bytes.Count(b=>b>100)<50)throw new Exception("missing compressed text raster");
   GC.Collect(2,GCCollectionMode.Forced,true,true);Near(compressed.Width,target,"compressed line survives GC");
   Console.WriteLine($"public paragraph compression {mode} {direction}, split={split}, terminal={terminal}, prefix={prefix}: {nominal.Width}->{compressed.Width}, caret/selection/raster/GC PASS");++cases;
  }
  int paragraphs=0;
  foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
  foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
  foreach(string word in new[]{"aa","कि"})
  foreach(bool columns in new[]{false,true}) {
   using var formatter=TextFormatter.Create(mode);string prefix=string.Join(" ",Enumerable.Repeat(word,3));
   using var nominal=formatter.FormatLine(new MeasureSource(prefix),0,500,new MeasureParagraph(direction),null);
   double width=nominal.Width-4;string body=string.Join(" ",Enumerable.Repeat(word,25));
   var cuts=Format(body,width,true,mode,direction,140,false,true,columns);int begin=0,compressed=0;
   foreach(int end in cuts){string segment=body.Substring(begin,end-begin).TrimEnd();using var line=formatter.FormatLine(new MeasureSource(segment),0,10000,new MeasureParagraph(direction),null);if(line.Width>width+0.02)++compressed;begin=end;}
   if(compressed==0)throw new Exception("optimal paragraph never selected a genuinely compressed word boundary");
   Console.WriteLine($"public optimal compression {mode} {direction}, word={word}, columns={columns}: {compressed} nominally overflowing rows selected PASS");++paragraphs;
  }
  Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine($"public paragraph compression {cases} direct and {paragraphs} optimal cases PASS");
 }
}
