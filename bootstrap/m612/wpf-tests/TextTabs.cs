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
    public TabParagraph(IList<TextTabProperties> value=null,bool wrap=false) { tabs=value;this.wrap=wrap; }
    public override FlowDirection FlowDirection=>FlowDirection.LeftToRight;
    public override TextAlignment TextAlignment=>TextAlignment.Left;
    public override double LineHeight=>0;
    public override bool FirstLineInParagraph=>true;
    public override TextRunProperties DefaultTextRunProperties=>new TabRunProperties();
    public override TextWrapping TextWrapping=>wrap?TextWrapping.Wrap:TextWrapping.NoWrap;
    public override TextMarkerProperties TextMarkerProperties=>null;
    public override double Indent=>0;
    public override double DefaultIncrementalTab=>80;
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
    static void Near(double actual,double expected,string why) {
        if(Math.Abs(actual-expected)>0.02) throw new Exception(why+": "+actual+" vs "+expected);
    }
    static double Distance(TextLine line,int cp)=>line.GetDistanceFromCharacterHit(new CharacterHit(cp,0));
    [STAThread]
    static void Main() {
        var resources=new System.Resources.ResourceManager("PresentationCore.g",typeof(TextFormatter).Assembly);
        foreach(string font in new[]{"globalmonospace","globalsansserif","globalserif","globaluserinterface"}) {
            using var stream=resources.GetStream("fonts/"+font+".compositefont");
            if(stream==null || stream.Length<1000)throw new Exception("missing source composite font "+font);
        }
        Console.WriteLine("all four original PresentationCore composite font resources present PASS");
        using var formatter=TextFormatter.Create();
        using var field=formatter.FormatLine(new TabSource("AB.CD"),0,400,new TabParagraph(),null);
        using var prefix=formatter.FormatLine(new TabSource("AB"),0,400,new TabParagraph(),null);
        foreach(bool split in new[]{false,true}) {
            foreach(var alignment in new[]{TextTabAlignment.Left,TextTabAlignment.Center,TextTabAlignment.Right,TextTabAlignment.Character}) {
                double offset=alignment==TextTabAlignment.Left?0:alignment==TextTabAlignment.Center?field.Width/2:alignment==TextTabAlignment.Right?field.Width:prefix.Width;
                foreach(int leader in new[]{0,(int)'.'}) {
                    var paragraph=new TabParagraph(new[]{new TextTabProperties(alignment,160,leader,'.')});
                    using var line=formatter.FormatLine(new TabSource(split:split),0,400,paragraph,null);
                    Near(Distance(line,3),160-offset,"aligned field start "+alignment);
                    Near(line.Width,160-offset+field.Width,"aligned total width "+alignment);
                    double first=Distance(line,2),last=Distance(line,3);
                    var before=line.GetCharacterHitFromDistance(first+1);
                    var after=line.GetCharacterHitFromDistance(last-1);
                    if(before.FirstCharacterIndex!=2 || before.TrailingLength!=0 || after.FirstCharacterIndex!=2 || after.TrailingLength!=1) throw new Exception("tab caret cell does not span gap");
                    var bounds=line.GetTextBounds(2,1).ToArray();
                    Near(bounds.Sum(b=>b.Rectangle.Width),last-first,"tab selection geometry");
                    GC.Collect(2,GCCollectionMode.Forced,true,true);
                    var glyphs=line.GetIndexedGlyphRuns().ToArray();
                    if(glyphs.Sum(run=>run.TextSourceLength)!=8) throw new Exception("tab glyph enumeration lost backing positions");
                    var visual=new DrawingVisual();using(var drawing=visual.RenderOpen())line.Draw(drawing,new Point(10,10),InvertAxes.None);
                    var bitmap=new RenderTargetBitmap(400,80,96,96,PixelFormats.Pbgra32);bitmap.Render(visual);
                    var pixels=new byte[400*80*4];bitmap.CopyPixels(pixels,400*4,0);
                    int gapPixels=0;
                    for(int y=0;y<80;++y)for(int x=(int)Math.Ceiling(first+12);x<(int)Math.Floor(last+8);++x)if(pixels[(y*400+x)*4+3]>180)++gapPixels;
                    if(leader==0?gapPixels!=0:gapPixels<10)throw new Exception("tab leader raster gap: "+leader+" "+gapPixels);
                    var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));
                    using(var file=File.Create(Path.Combine(AppContext.BaseDirectory,$"tab-{alignment}-{leader}-{split}.png")))encoder.Save(file);
                    Console.WriteLine($"public tab {alignment}, leader={leader}, split={split}: start={last:F3}, gap pixels={gapPixels} PASS");
                }
            }
        }
        using(var incremental=formatter.FormatLine(new TabSource("कि\tA\tB"),0,400,new TabParagraph(),null)) {
            Near(Distance(incremental,3),80,"first incremental stop");Near(Distance(incremental,5),160,"second incremental stop");
        }
        var wrapped=new TabSource("कि\talpha beta gamma");int position=0,count=0;
        while(position<=wrapped.Text.Length) {
            using var line=formatter.FormatLine(wrapped,position,125,new TabParagraph(wrap:true),null);
            if(line.Length<=0 || line.Width>125.02)throw new Exception("tab wrapping progress or column");
            position+=line.Length;++count;if(count>30)throw new Exception("tab wrapping did not end");
        }
        if(position!=wrapped.Text.Length+1 || count<2)throw new Exception("tab wrapping coverage");
        Console.WriteLine("public TextFormatter aligned/incremental tabs, cross-run fields, leaders, caret/selection/enumeration, raster, wrapping and moving GC PASS");
    }
}
