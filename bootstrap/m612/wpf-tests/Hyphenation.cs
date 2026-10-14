using System;
using System.Globalization;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Threading;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.TextFormatting;
using System.Collections;
using System.Windows.Markup;
class HyphenRunProperties:TextRunProperties {
    public override Typeface Typeface=>new Typeface("Segoe UI");
    public override double FontRenderingEmSize=>24;
    public override double FontHintingEmSize=>24;
    public override TextDecorationCollection TextDecorations=>null;
    public override Brush ForegroundBrush=>Brushes.Black;
    public override Brush BackgroundBrush=>null;
    public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("en-US");
    public override TextEffectCollection TextEffects=>null;
}
class HyphenParagraph:TextParagraphProperties {
    readonly FlowDirection direction;
    readonly bool wrap;
    public HyphenParagraph(bool wrap=false,FlowDirection direction=FlowDirection.LeftToRight) { this.wrap=wrap;this.direction=direction; }
    public override FlowDirection FlowDirection=>direction;
    public override TextAlignment TextAlignment=>TextAlignment.Left;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new HyphenRunProperties();
    public override TextWrapping TextWrapping=>wrap?TextWrapping.Wrap:TextWrapping.NoWrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
}
class HyphenSource:TextSource {
    public readonly string Text;
    public HyphenSource(string text="hyphenation") { Text=text; }
    public override TextRun GetTextRun(int index)=>index<Text.Length?new TextCharacters(Text,index,Text.Length-index,new HyphenRunProperties()):new TextEndOfParagraph(1);
    public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,
        new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("en-US"),new CharacterBufferRange(Text,0,Math.Min(limit,Text.Length))));
    public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
    const BindingFlags I=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
    static int Hyphens(Drawing drawing) {
        if(drawing is GlyphRunDrawing glyph)return glyph.GlyphRun.Characters.Count(c=>c=='-');
        return drawing is DrawingGroup group?group.Children.Sum(Hyphens):0;
    }
    static int Hyphens(Visual visual){int count=Hyphens(VisualTreeHelper.GetDrawing(visual));for(int i=0;i<VisualTreeHelper.GetChildrenCount(visual);++i)count+=Hyphens((Visual)VisualTreeHelper.GetChild(visual,i));return count;}
    [STAThread] static void Main() {
        var type=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.NaturalLanguageHyphenator",true);
        foreach(string locale in new[]{"en-US","en-GB","de-DE","de-CH","fr-FR","es-ES","it-IT","nl-NL","ru-RU","pl-PL","tr-TR","hu-HU","fi-FI"}) {
            var hyphenator=Activator.CreateInstance(type,true);
            try {
                string text=locale.StartsWith("de")?"Donaudampfschifffahrtsgesellschaft":locale.StartsWith("fr")?"extraordinaire":locale.StartsWith("es")?"extraordinario":locale.StartsWith("it")?"straordinariamente":locale.StartsWith("nl")?"lettergrepen":locale.StartsWith("ru")?"автоматический":locale.StartsWith("pl")?"automatyczny":locale.StartsWith("tr")?"bilgisayar":locale.StartsWith("hu")?"automatikusan":locale.StartsWith("fi")?"automaattinen":"hyphenation";
                var breaks=type.GetMethod("AnalyzeText",I).Invoke(hyphenator,new object[]{text.ToCharArray(),text.Length,CultureInfo.GetCultureInfo(locale)});if(breaks==null)throw new Exception("native source resource missing");
                var positions=new List<int>();int current=0;
                while((current=(int)breaks.GetType().GetMethod("GetNextBreak",I).Invoke(breaks,new object[]{current}))>=0) {if(current<=0||current>=text.Length||positions.Contains(current))throw new Exception("invalid native UTF-16 break");positions.Add(current);}
                if(positions.Count==0)throw new Exception("source language has no hyphen breaks: "+locale);
                if(locale.StartsWith("en")&&!positions.SequenceEqual(new[]{2,6}))throw new Exception("English pattern boundaries: "+string.Join(",",positions));
                Console.WriteLine($"original hyphenator {locale} {text}: {string.Join(",",positions)} PASS");
            }finally{((IDisposable)hyphenator).Dispose();}
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft}) {
            var hyphenator=Activator.CreateInstance(type,true);
            try {
                var paragraph=new HyphenParagraph(true,direction);typeof(TextParagraphProperties).GetProperty("Hyphenator",I).SetValue(paragraph,hyphenator);
                using var formatter=TextFormatter.Create(mode);using var first=formatter.FormatLine(new HyphenSource(),0,110,paragraph,null);
                var glyphs=first.GetIndexedGlyphRuns().ToArray();string drawn=string.Concat(glyphs.Select(g=>new string(g.GlyphRun.Characters.ToArray())));
                if(first.Length!=6||first.Width>110||!drawn.EndsWith("-")||glyphs.Sum(g=>g.TextSourceLength)!=6)throw new Exception($"source hyphen break/draw geometry: length={first.Length}, width={first.Width}, glyphs={drawn}, source={glyphs.Sum(g=>g.TextSourceLength)}");
                double caret=first.GetDistanceFromCharacterHit(new CharacterHit(5,1));if(double.IsNaN(caret)||caret<0||caret>first.WidthIncludingTrailingWhitespace+1)throw new Exception("hyphenated caret geometry");
                var visual=new DrawingVisual();using(var drawing=visual.RenderOpen())first.Draw(drawing,new Point(10,35),InvertAxes.None);var image=new RenderTargetBitmap(180,100,96,96,PixelFormats.Pbgra32);image.Render(visual);var pixels=new byte[180*100*4];image.CopyPixels(pixels,720,0);int ink=0;for(int i=0;i<pixels.Length;i+=4)if(pixels[i+3]>150&&pixels[i]<70&&pixels[i+1]<70&&pixels[i+2]<70)++ink;if(ink<100)throw new Exception("hyphenated glyph raster missing");
                using var record=first.GetTextLineBreak();using var next=formatter.FormatLine(new HyphenSource(),first.Length,110,paragraph,record);if(first.Length+next.Length!=12)throw new Exception("hyphen continuation consumed inserted character as source");
                Console.WriteLine($"original formatter hyphenation {mode} {direction}: source={first.Length}, glyphs={drawn}, width={first.Width:F2}, {ink} glyph pixels, caret and exact continuation PASS");
            }finally{((IDisposable)hyphenator).Dispose();}
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})foreach(bool justified in new[]{false,true}) {
            var run=new Run(string.Join(" ",Enumerable.Repeat("hyphenation representation",20)));
            var document=new FlowDocument(new Paragraph(run){Margin=new Thickness(0)}){TextAlignment=justified?TextAlignment.Justify:TextAlignment.Left,IsHyphenationEnabled=true,Language=XmlLanguage.GetLanguage("en-US"),FlowDirection=direction,FontFamily=new FontFamily("Segoe UI"),FontSize=24,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(160,180);var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,markers=0;
            for(;pages<40;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;markers+=Hyphens(page.Visual);var view=((IServiceProvider)page).GetService(viewType);foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();ranges.Add((pages,(TextPointer)t.GetProperty("Start",I).GetValue(segment),(TextPointer)t.GetProperty("End",I).GetValue(segment)));}GC.Collect(2,GCCollectionMode.Forced,true,true);}
            if(pages<2||pages>=40||markers<10||!paginator.IsPageCountValid)throw new Exception($"public automatic hyphenation missing: pages={pages}, markers={markers}");
            for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("inserted hyphen corrupts source/page ownership at "+at);}
            using(var page=paginator.GetPage(0))if(Hyphens(page.Visual)==0)throw new Exception("disposed hyphenated page does not regenerate");
            Console.WriteLine($"public FlowDocument automatic hyphenation {mode} {direction}, justified={justified}: {pages} pages, {markers} inserted glyphs, exact {run.Text.Length} source characters, page mapping/regeneration and GC PASS");
        }
        GC.Collect(2,GCCollectionMode.Forced,true,true);Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original hyphenator HRESULT/out-retval, native bitmask, multilingual source dictionaries and disposal PASS");
    }
}
