using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
class Check {
    static Paragraph Para(string text,bool keep=false,Thickness? margin=null)=>new Paragraph(new Run(text)){Margin=margin??new Thickness(0),LineHeight=24,LineStackingStrategy=LineStackingStrategy.BlockLineHeight,MinOrphanLines=1,MinWidowLines=1,KeepWithNext=keep};
    static FlowDocument Doc(IEnumerable<Paragraph> blocks,bool nested,TextFormattingMode mode) {
        var doc=new FlowDocument{FontFamily=new FontFamily("Segoe UI"),FontSize=20,Foreground=Brushes.Black,PagePadding=new Thickness(10),ColumnWidth=double.PositiveInfinity};
        if(nested){var section=new Section{Margin=new Thickness(0)};foreach(var block in blocks)section.Blocks.Add(block);doc.Blocks.Add(section);}else foreach(var block in blocks)doc.Blocks.Add(block);
        TextOptions.SetTextFormattingMode(doc,mode);return doc;
    }
    static int Map(DynamicDocumentPaginator paginator,Paragraph paragraph,int offset)=>paginator.GetPageNumber(((Run)paragraph.Inlines.FirstInline).ContentStart.GetPositionAtOffset(offset,LogicalDirection.Forward));
    static int Pages(DynamicDocumentPaginator paginator) {
        int count=0;for(;count<12;++count){using var page=paginator.GetPage(count);if(page==DocumentPage.Missing)break;
            var bitmap=new RenderTargetBitmap(400,140,96,96,PixelFormats.Pbgra32);bitmap.Render(page.Visual);var pixels=new byte[400*140*4];bitmap.CopyPixels(pixels,400*4,0);
            int ink=0;for(int i=0;i<pixels.Length;i+=4)if(pixels[i+3]>100&&(pixels[i]<100||pixels[i+1]<100||pixels[i+2]<100))++ink;if(ink<100)throw new Exception("keep-chain page has no rendered text");}
        if(count>=12||!paginator.IsPageCountValid||paginator.PageCount!=count)throw new Exception("keep chain failed to make bounded progress");return count;
    }
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool nested in new[]{false,true})
        foreach(int length in new[]{1,3})
        foreach(bool keep in new[]{false,true}) {
            var prefix=Para(length==1?"Prefix one.\nPrefix two.\nPrefix three.\nPrefix four.":"Prefix one.\nPrefix two.");
            var headings=Enumerable.Range(0,length).Select(i=>Para($"Heading {i}.",keep)).ToArray();var body=Para("Body one.\nBody two.");
            var blocks=new[]{prefix}.Concat(headings).Append(body).ToArray();var doc=Doc(blocks,nested,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)doc).DocumentPaginator;paginator.PageSize=new Size(400,140);
            if(Pages(paginator)!=2||Map(paginator,prefix,0)!=0)throw new Exception("keep-chain prefix/page count mismatch");
            foreach(var heading in headings)if(Map(paginator,heading,0)!=(keep?1:0))throw new Exception("heading was orphaned or dropped during chain rollback");
            if(Map(paginator,body,0)!=1)throw new Exception("following body does not accompany the kept chain");
            foreach(var block in blocks){var run=(Run)block.Inlines.FirstInline;for(int at=0;at<run.Text.Length;++at){int page=Map(paginator,block,at);if(page<0||page>1||(block==prefix&&page!=0)||(keep&&block!=prefix&&page!=1))throw new Exception("keep-chain source character/page mapping");}}
            GC.Collect(2,GCCollectionMode.Forced,true,true);using(var regenerated=paginator.GetPage(1))if(regenerated==DocumentPage.Missing)throw new Exception("kept page was lost after GC");
            Console.WriteLine($"original KeepWithNext {mode}, nested={nested}, length={length}, keep={keep}: heading chain pages {(keep?1:0)}, body page 1, exact source/page mapping and glyph raster PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var blocks=Enumerable.Range(0,7).Select(i=>Para($"Long chain {i}.",true)).Append(Para("Body one.\nBody two.")).ToArray();var doc=Doc(blocks,true,mode);var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)doc).DocumentPaginator;paginator.PageSize=new Size(400,140);
            if(Pages(paginator)!=2||Map(paginator,blocks.Last(),0)!=1)throw new Exception("oversized keep chain does not relax constraints for bounded progress");
            Console.WriteLine($"original oversized KeepWithNext {mode}: 2 rendered pages, complete source and bounded progress PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool keep in new[]{false,true}) {
            var prefix=Para("Prefix one.\nPrefix two.\nPrefix three.",false,new Thickness(0,0,0,8));
            var heading=Para("Margin heading.",keep,new Thickness(0,3,0,5));var body=Para("Body one.\nBody two.",false,new Thickness(0,6,0,0));body.KeepTogether=true;
            var doc=Doc(new[]{prefix,heading,body},true,mode);var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)doc).DocumentPaginator;paginator.PageSize=new Size(400,140);
            int marginPages=Pages(paginator),headingPage=Map(paginator,heading,0),bodyPage=Map(paginator,body,0);if(marginPages!=2||headingPage!=(keep?1:0)||bodyPage!=1)throw new Exception($"margin keep={keep}, mode={mode}: pages={marginPages}, heading={headingPage}, body={bodyPage}");
            Console.WriteLine($"original margin KeepWithNext {mode}, keep={keep}: nonzero collapsed margins, chain deferral and source/page positions PASS");
            var multi=Para("Heading one.\nHeading two.",keep);doc=Doc(new[]{Para("Prefix one.\nPrefix two.\nPrefix three.\nPrefix four."),multi,Para("Body one.\nBody two.")},false,mode);
            paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)doc).DocumentPaginator;paginator.PageSize=new Size(400,140);
            if(Pages(paginator)!=2||Map(paginator,multi,0)!=(keep?1:0)||Map(paginator,multi,((Run)multi.Inlines.FirstInline).Text.Length-1)!=1)throw new Exception("KeepWithNext did not also keep its own paragraph unbroken");
            Console.WriteLine($"original self KeepWithNext {mode}, keep={keep}: multi-line heading {(keep?"intact":"split")} and exact start/end pages PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}) {
            var prefix=Para("Prefix one.\nPrefix two.\nPrefix three.\nPrefix four.");prefix.Foreground=Brushes.Red;
            var heading=Para("Kept heading.",true);heading.Foreground=Brushes.Blue;var body=Para("Body one.\nBody two.");body.Foreground=Brushes.Blue;
            var doc=Doc(new[]{prefix,heading,body},false,mode);doc.ColumnWidth=145;doc.ColumnGap=30;doc.IsColumnWidthFlexible=false;
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)doc).DocumentPaginator;paginator.PageSize=new Size(400,140);
            if(Pages(paginator)!=1)throw new Exception("keep chain failed to resume in the next column on the same page");
            using var page=paginator.GetPage(0);var image=new RenderTargetBitmap(400,140,96,96,PixelFormats.Pbgra32);image.Render(page.Visual);var pixels=new byte[400*140*4];image.CopyPixels(pixels,400*4,0);int blue=0,wrong=0;
            for(int y=0;y<140;++y)for(int x=0;x<400;++x){int at=(y*400+x)*4;if(pixels[at+3]>100&&pixels[at]>4*pixels[at+1]&&pixels[at]>4*pixels[at+2]){++blue;if(x<185||x>=330)++wrong;}}
            if(blue<200||wrong!=0)throw new Exception($"keep-chain column glyph placement: blue={blue}, wrong={wrong}");
            Console.WriteLine($"original column KeepWithNext {mode}: heading/body together in second column, source completion and glyph positions PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original keep-with-next heading chains and dispatcher cleanup PASS");
    }
}
