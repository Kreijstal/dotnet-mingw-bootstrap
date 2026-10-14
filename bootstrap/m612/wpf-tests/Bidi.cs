using System;
using System.Globalization;
using System.Linq;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media.Imaging;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.TextFormatting;
class BidiRunProperties:TextRunProperties {
    public override Typeface Typeface=>new Typeface("Segoe UI");
    public override double FontRenderingEmSize=>24;
    public override double FontHintingEmSize=>24;
    public override TextDecorationCollection TextDecorations=>null;
    public override Brush ForegroundBrush=>Brushes.Black;
    public override Brush BackgroundBrush=>null;
    public override CultureInfo CultureInfo=>CultureInfo.GetCultureInfo("he-IL");
    public override TextEffectCollection TextEffects=>null;
}
class BidiParagraph:TextParagraphProperties {
    readonly FlowDirection direction;
    readonly bool wrap;
    public BidiParagraph(bool wrap=false,FlowDirection direction=FlowDirection.LeftToRight) { this.wrap=wrap;this.direction=direction; }
    public override FlowDirection FlowDirection=>direction;
    public override TextAlignment TextAlignment=>TextAlignment.Left;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new BidiRunProperties();
    public override TextWrapping TextWrapping=>wrap?TextWrapping.Wrap:TextWrapping.NoWrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
}
class BidiSource:TextSource {
    public readonly string Text;
    public BidiSource(string text="कि") { Text=text; }
    public override TextRun GetTextRun(int index)=>index<Text.Length?new TextCharacters(Text,index,Text.Length-index,new BidiRunProperties()):new TextEndOfParagraph(1);
    public override TextSpan<CultureSpecificCharacterBufferRange> GetPrecedingText(int limit)=>new TextSpan<CultureSpecificCharacterBufferRange>(limit,
        new CultureSpecificCharacterBufferRange(CultureInfo.GetCultureInfo("he-IL"),new CharacterBufferRange(Text,0,Math.Min(limit,Text.Length))));
    public override int GetTextEffectCharacterIndexFromTextSourceCharacterIndex(int index)=>index;
}
class Check {
    [STAThread] static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
        foreach(string text in new[]{"אבג", "مرحبا", "abc אבג def", "אבג 123 דהו", "אבג   ", "שָׁלוֹם"}) {
            using var formatter=TextFormatter.Create(mode);
            using var line=formatter.FormatLine(new BidiSource(text),0,400,new BidiParagraph(false,direction),null);
            var visual=new DrawingVisual();using(var draw=visual.RenderOpen())line.Draw(draw,new Point(10,35),InvertAxes.None);
            var image=new RenderTargetBitmap(450,100,96,96,PixelFormats.Pbgra32);image.Render(visual);var bytes=new byte[450*100*4];image.CopyPixels(bytes,1800,0);
            int ink=0;for(int i=0;i<bytes.Length;i+=4)if(bytes[i+3]>150&&bytes[i]<70&&bytes[i+1]<70&&bytes[i+2]<70)++ink;
            if(ink<30)throw new Exception("bidi glyph raster missing");
            foreach(int index in Enumerable.Range(0,text.Length+1)) {
                double distance=line.GetDistanceFromCharacterHit(new System.Windows.Media.TextFormatting.CharacterHit(index,0));
                if(double.IsNaN(distance)||double.IsInfinity(distance))throw new Exception("bidi caret geometry missing");
            }
            var hits=Enumerable.Range(0,text.Length+1).Select(i=>line.GetDistanceFromCharacterHit(new CharacterHit(i,0))).ToArray();
            if(text=="אבג") {
                double end=line.GetDistanceFromCharacterHit(new CharacterHit(2,1));var edges=new[]{hits[0],hits[1],hits[2],end};
                for(int i=0;i<3;++i) {
                    if(direction==FlowDirection.LeftToRight?edges[i]<=edges[i+1]:edges[i]>=edges[i+1])throw new Exception("Hebrew caret direction is incorrect: "+direction+" "+string.Join(",",edges));
                    var hit=line.GetCharacterHitFromDistance((edges[i]+edges[i+1])/2);int insertion=hit.FirstCharacterIndex+hit.TrailingLength;if(insertion!=i&&insertion!=i+1)throw new Exception("Hebrew point/caret mapping: "+insertion+" vs "+i);
                }
            }
            foreach(var bound in line.GetTextBounds(0,text.Length))if(bound.Rectangle.Width<0||double.IsNaN(bound.Rectangle.Width))throw new Exception("bidi selection bounds invalid");
            int glyphCharacters=0;foreach(var glyph in line.GetIndexedGlyphRuns()){glyphCharacters+=glyph.TextSourceLength;if(glyph.TextSourceCharacterIndex<0||glyph.TextSourceCharacterIndex+glyph.TextSourceLength>text.Length)throw new Exception("bidi glyph enumeration source mapping");}
            if(glyphCharacters!=text.Length)throw new Exception("bidi glyph enumeration must retain every source character");
            if(text.EndsWith("   ")&&line.WidthIncludingTrailingWhitespace<=line.Width)throw new Exception("bidi trailing spaces must retain separate advance");
            Console.WriteLine($"bidi {mode} {direction} {text}: width={line.Width:F2}, ink={ink} PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft}) {
            using var formatter=TextFormatter.Create(mode);string text=string.Join(" ",Enumerable.Range(0,12).Select(i=>"אבג مرحبا abc123"));
            var source=new BidiSource(text);int offset=0,lines=0;TextLineBreak previous=null;
            try {
                while(offset<text.Length) {
                    if(++lines>40)throw new Exception("bidi wrapping must make progress");
                    using var line=formatter.FormatLine(source,offset,150,new BidiParagraph(true,direction),previous);
                    if(line.Length<=0)throw new Exception("bidi continuation consumed no source");
                    var next=line.GetTextLineBreak();previous?.Dispose();previous=next;
                    foreach(var bound in line.GetTextBounds(offset,Math.Min(line.Length,text.Length-offset)))if(bound.Rectangle.Width<0)throw new Exception("wrapped bidi selection geometry");
                    offset+=line.Length;GC.Collect(2,GCCollectionMode.Forced,true,true);
                }
                if(offset!=text.Length+1||lines<2)throw new Exception("bidi wrapping must cover exact source plus paragraph separator");
                Console.WriteLine($"bidi wrapped {mode} {direction}: {lines} lines, {text.Length} source characters and original continuation/selection/GC PASS");
            } finally {previous?.Dispose();}
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft}) {
            var block=new TextBlock{Text="Latin אבג 123 مرحبا\nאבג Latin דהו",FontFamily=new FontFamily("Segoe UI"),FontSize=24,TextWrapping=TextWrapping.Wrap,FlowDirection=direction};
            TextOptions.SetTextFormattingMode(block,mode);block.Measure(new Size(180,double.PositiveInfinity));block.Arrange(new Rect(0,0,180,block.DesiredSize.Height));
            var image=new RenderTargetBitmap(200,(int)Math.Ceiling(block.DesiredSize.Height)+2,96,96,PixelFormats.Pbgra32);image.Render(block);
            var pixels=new byte[200*image.PixelHeight*4];image.CopyPixels(pixels,800,0);int ink=0;for(int i=0;i<pixels.Length;i+=4)if(pixels[i+3]>150&&pixels[i]<70&&pixels[i+1]<70&&pixels[i+2]<70)++ink;
            if(ink<300||block.DesiredSize.Height<=24||block.GetPositionFromPoint(new Point(60,12),true)==null)throw new Exception("original bidi TextBlock wrapped layout/raster/hit test");
            GC.Collect(2,GCCollectionMode.Forced,true,true);Console.WriteLine($"original bidi TextBlock {mode} {direction}: wrapped extent {block.DesiredSize.Height:F1}, {ink} glyph pixels and original point hit/GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            using var formatter=TextFormatter.Create(mode);string text="Latin אבג 123 مرحبا";
            var intrinsic=formatter.FormatMinMaxParagraphWidth(new BidiSource(text),0,new BidiParagraph(true));
            double minimum=text.Split(' ').Select(word=>{using var line=formatter.FormatLine(new BidiSource(word),0,10000,new BidiParagraph(),null);return line.Width;}).Max();
            using var whole=formatter.FormatLine(new BidiSource(text),0,10000,new BidiParagraph(),null);
            if(Math.Abs(intrinsic.MinWidth-minimum)>0.05||Math.Abs(intrinsic.MaxWidth-whole.Width)>0.05)throw new Exception($"bidi intrinsic word boundaries: {intrinsic.MinWidth}/{minimum}, {intrinsic.MaxWidth}/{whole.Width}");
            Console.WriteLine($"bidi intrinsic {mode}: source word minimum {minimum:F2}, whole paragraph maximum {whole.Width:F2} PASS");
        }
        var viewType=typeof(TextBlock).Assembly.GetType("System.Windows.Documents.ITextView",true);
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var run=new System.Windows.Documents.Run(string.Join(" ",Enumerable.Range(0,35).Select(i=>"Latin אבג 123 مرحبا")));
            var document=new System.Windows.Documents.FlowDocument(new System.Windows.Documents.Paragraph(run){Margin=new Thickness(0)}){FontFamily=new FontFamily("Segoe UI"),FontSize=24,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(System.Windows.Documents.DynamicDocumentPaginator)((System.Windows.Documents.IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);
            var ranges=new System.Collections.Generic.List<(int Page,System.Windows.Documents.TextPointer Start,System.Windows.Documents.TextPointer End)>();int pages=0;
            for(;pages<30;++pages){using var page=paginator.GetPage(pages);if(page==System.Windows.Documents.DocumentPage.Missing)break;
                var image=new RenderTargetBitmap(400,220,96,96,PixelFormats.Pbgra32);image.Render(page.Visual);var pixels=new byte[400*220*4];image.CopyPixels(pixels,1600,0);int ink=0;for(int i=0;i<pixels.Length;i+=4)if(pixels[i+3]>150&&pixels[i]<70&&pixels[i+1]<70&&pixels[i+2]<70)++ink;if(ink<100)throw new Exception("bidi FlowDocument page has no glyph raster");
                var view=((IServiceProvider)page).GetService(viewType);foreach(var segment in (System.Collections.IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var type=segment.GetType();var flags=System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.Public|System.Reflection.BindingFlags.NonPublic;ranges.Add((pages,(System.Windows.Documents.TextPointer)type.GetProperty("Start",flags).GetValue(segment),(System.Windows.Documents.TextPointer)type.GetProperty("End",flags).GetValue(segment)));}
            }
            if(pages<2||pages>=30||!paginator.IsPageCountValid)throw new Exception("bidi FlowDocument must finish pagination");
            for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var matches=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(matches.Length!=1||paginator.GetPageNumber(position)!=matches[0].Page)throw new Exception("bidi FlowDocument source/page ownership");}
            using(var first=paginator.GetPage(0)){var image=new RenderTargetBitmap(400,220,96,96,PixelFormats.Pbgra32);image.Render(first.Visual);}GC.Collect(2,GCCollectionMode.Forced,true,true);
            Console.WriteLine($"original mixed bidi FlowDocument {mode}: {pages} rendered pages, all {run.Text.Length} source characters once, original page mapping/regeneration/GC PASS");
        }
        System.Windows.Threading.Dispatcher.CurrentDispatcher.InvokeShutdown();
    }
}
