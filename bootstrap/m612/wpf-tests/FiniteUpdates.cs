using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

class Check {
    const BindingFlags I=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;
    static object Get(object value,string name)=>value.GetType().GetProperty(name,I)?.GetValue(value)??value.GetType().GetField(name,I)?.GetValue(value)??throw new Exception("missing member "+name);
    static object Call(object value,string name,params object[] args)=>value.GetType().GetMethods(I).Single(m=>m.Name==name&&m.GetParameters().Length==args.Length).Invoke(value,args);
    static byte[] Raster(DocumentPage page) {
        var image=new RenderTargetBitmap(400,220,96,96,PixelFormats.Pbgra32);image.Render(page.Visual);
        var pixels=new byte[400*220*4];image.CopyPixels(pixels,1600,0);
        int ink=0;for(int i=0;i<pixels.Length;i+=4)if(pixels[i+3]>150&&pixels[i]<100&&pixels[i+1]<100&&pixels[i+2]<100)++ink;
        if(ink<50)throw new Exception("updated page has no real glyph ink");return pixels;
    }
    [STAThread]
    static void Main() {
        var assembly=typeof(FlowDocument).Assembly;
        var pageType=assembly.GetType("MS.Internal.PtsHost.FlowDocumentPage",true);
        var viewType=assembly.GetType("System.Windows.Documents.ITextView",true);
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool columns in new[]{false,true}) {
            var run=new Run("Original short source text.");
            var document=new FlowDocument(new Paragraph(run){Margin=new Thickness(0)}) {
                FontFamily=new FontFamily("Segoe UI"),FontSize=18,PagePadding=new Thickness(10),
                ColumnWidth=columns?180:double.PositiveInfinity,ColumnGap=20
            };
            TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;
            paginator.IsBackgroundPaginationEnabled=false;paginator.PageSize=new Size(400,220);
            var cache=Get(document,"StructuralCache");
            // Keep an original page outside the paginator's break table: that
            // table disposes cached pages on edits rather than updating them.
            using var page=(DocumentPage)Activator.CreateInstance(pageType,I,null,new[]{cache},null);
            var size=new Size(400,220);var margin=new Thickness(10);
            Call(page,"FormatFinite",size,margin,null);Call(page,"Arrange",size);
            var initial=Raster(page);var handle=(IntPtr)Get(Get(page,"_ptsPage"),"PageHandle");
            if(handle==IntPtr.Zero)throw new Exception("original finite page handle missing");
            foreach(int edit in new[]{0,1,2}) {
                run.Text=edit==0?"Edited source with more words and visibly different glyphs.":edit==1?string.Join(" ",Enumerable.Range(0,90).Select(i=>$"word{i:D3}")):"Short final source.";
                var result=Call(page,"FormatFinite",size,margin,null);Call(page,"Arrange",size);
                if((IntPtr)Get(Get(page,"_ptsPage"),"PageHandle")!=handle||!(bool)Get(page,"IncrementalUpdate"))throw new Exception("original finite update must preserve its native page handle");
                var pixels=Raster(page);if(pixels.SequenceEqual(initial))throw new Exception("finite update retained stale raster");initial=pixels;
                var ranges=new List<(TextPointer Start,TextPointer End)>();
                var view=((IServiceProvider)page).GetService(viewType);
                foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)) {
                    var t=segment.GetType();ranges.Add(((TextPointer)t.GetProperty("Start",I).GetValue(segment),(TextPointer)t.GetProperty("End",I).GetValue(segment)));
                }
                int covered=0;
                for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);int count=ranges.Count(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0);if(count>1)throw new Exception("updated source character repeated");covered+=count;}
                if(covered==0||edit!=1&&covered!=run.Text.Length||edit==1&&result==null)throw new Exception("updated source ranges and outgoing continuation disagree");
                if(edit==1) {
                    var extraPages=new List<DocumentPage>();var records=new List<object>();object incoming=result;int resumed=0;
                    try {
                        while(incoming!=null) {
                            if(++resumed>20)throw new Exception("updated continuation must reach document end");records.Add(incoming);
                            var next=(DocumentPage)Activator.CreateInstance(pageType,I,null,new[]{cache},null);extraPages.Add(next);
                            incoming=Call(next,"FormatFinite",size,margin,incoming);Call(next,"Arrange",size);Raster(next);
                            var nextView=((IServiceProvider)next).GetService(viewType);
                            foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(nextView)){var t=segment.GetType();ranges.Add(((TextPointer)t.GetProperty("Start",I).GetValue(segment),(TextPointer)t.GetProperty("End",I).GetValue(segment)));}
                        }
                        for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);if(ranges.Count(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0)!=1)throw new Exception("updated/resumed pages must cover every source character exactly once");}
                        Console.WriteLine($"original updated continuation {mode}, columns={columns}: {resumed+1} rendered pages, all {run.Text.Length} characters exactly once PASS");
                    } finally {foreach(var extra in extraPages)extra.Dispose();foreach(var record in records)((IDisposable)record).Dispose();}
                }
                GC.Collect(2,GCCollectionMode.Forced,true,true);Raster(page);
                Console.WriteLine($"original finite page update {mode}, columns={columns}, edit={edit}: stable native page, {covered}/{run.Text.Length} source characters, changed glyph raster, continuation={result!=null}, original arrange/visual/GC PASS");
            }
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original finite page update and dispatcher cleanup PASS");
    }
}
