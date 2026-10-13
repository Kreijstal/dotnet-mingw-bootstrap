using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
class Check {
    const BindingFlags I=BindingFlags.Instance|BindingFlags.NonPublic|BindingFlags.Public;
    const BindingFlags S=BindingFlags.Static|BindingFlags.NonPublic|BindingFlags.Public;
    static readonly Assembly A=typeof(FlowDocument).Assembly;
    static readonly Type Pts=A.GetType("MS.Internal.PtsHost.UnsafeNativeMethods.PTS",true);
    static Type Record(string name)=>A.GetType(Pts.FullName+"+"+name,true);
    static object Get(object value,string name)=>value.GetType().GetProperty(name,I)?.GetValue(value)??value.GetType().GetField(name,I)?.GetValue(value)??throw new Exception("missing member "+name);
    static object Call(object value,string name,params object[] args)=>value.GetType().GetMethods(I).Single(m=>m.Name==name&&m.GetParameters().Length==args.Length).Invoke(value,args);
    static object Enum(string name,int value)=>System.Enum.ToObject(Record(name),value);
    static object Rect(int u,int v,int du,int dv) {
        var r=Activator.CreateInstance(Record("FSRECT"));foreach(var pair in new[]{("u",u),("v",v),("du",du),("dv",dv)})r.GetType().GetField(pair.Item1,I).SetValue(r,pair.Item2);return r;
    }
    static int Dpi(double value)=>(int)Math.Round(value*300);
    static int Native(string method,params object[] args) {
        int status=(int)Pts.GetMethod(method,S).Invoke(null,args);if(status!=0)throw new Exception(method+" status "+status);return status;
    }
    static int Ink(Visual visual) {
        var image=new RenderTargetBitmap(400,180,96,96,PixelFormats.Pbgra32);image.Render(visual);var bytes=new byte[400*180*4];image.CopyPixels(bytes,400*4,0);
        int ink=0;for(int i=0;i<bytes.Length;i+=4)if(bytes[i+3]>100&&bytes[i]<100&&bytes[i+1]<100&&bytes[i+2]<100)++ink;
        if(ink<100){Dump(visual,0);throw new Exception("original subpage visual has no glyph raster: "+ink+", bounds="+VisualTreeHelper.GetDescendantBounds(visual));}return ink;
    }
    static byte[] Raster(Visual visual){var image=new RenderTargetBitmap(400,180,96,96,PixelFormats.Pbgra32);image.Render(visual);var bytes=new byte[400*180*4];image.CopyPixels(bytes,400*4,0);return bytes;}
    static void Dump(Visual visual,int depth){Console.WriteLine(new string(' ',depth)+visual.GetType().Name+" bounds="+VisualTreeHelper.GetDescendantBounds(visual)+" clip="+VisualTreeHelper.GetClip(visual));for(int i=0;i<VisualTreeHelper.GetChildrenCount(visual);++i)Dump((Visual)VisualTreeHelper.GetChild(visual,i),depth+1);}
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        {
            var run=new Run(string.Join(" ",Enumerable.Range(0,100).Select(i=>$"sub{i:D3}")));
            var document=new FlowDocument(new Paragraph(new Run("Owner format context"))){FontFamily=new FontFamily("Segoe UI"),FontSize=20,Foreground=Brushes.Black,PagePadding=new Thickness(0),ColumnWidth=double.PositiveInfinity};
            TextOptions.SetTextFormattingMode(document,mode);
            var paginator=((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,180);
            using var seedPage=paginator.GetPage(0);var cache=Get(document,"StructuralCache");var context=Get(cache,"PtsContext");var handle=(IntPtr)Get(context,"Context");
            // Exercise the original cell subpage path directly while its table
            // object handler is still being implemented separately.
            var cell=new TableCell(new Paragraph(run){Margin=new Thickness(0)}){Padding=new Thickness(0),BorderThickness=new Thickness(0)};
            var table=new Table();var group=new TableRowGroup();var row=new TableRow();row.Cells.Add(cell);group.Rows.Add(row);table.RowGroups.Add(group);document.Blocks.Add(table);
            // Inserting the table invalidates the paginator's previous page.
            // Use an original, live page as the owner of scoped operation data.
            using var ownerPage=(DocumentPage)Activator.CreateInstance(A.GetType("MS.Internal.PtsHost.FlowDocumentPage",true),I,null,new[]{cache},null);Call(ownerPage,"SetSize",new Size(400,180));
            var paragraph=Activator.CreateInstance(A.GetType("MS.Internal.PtsHost.SubpageParagraph",true),I,null,new[]{(object)cell,cache},null);
            IntPtr incoming=IntPtr.Zero;int pages=0,totalInk=0;var ranges=new List<(TextPointer Start,TextPointer End)>();
            try {
                for(;pages<20;++pages) {
                    object[] clientArgs={IntPtr.Zero};Call(paragraph,"CreateParaclient",clientArgs);var client=Call(context,"HandleToObject",clientArgs[0]);
                    object[] args={client,incoming,1,IntPtr.Zero,0,1,0u,Rect(0,0,Dpi(400),Dpi(180)),null,Enum("FSKCLEAR",0),Enum("FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA",2),null,IntPtr.Zero,IntPtr.Zero,0,null,IntPtr.Zero,null,0};
                    using((IDisposable)Call(cache,"SetDocumentFormatContext",ownerPage)) {
                        var format=Get(cache,"CurrentFormatContext");Call(format,"PushNewPageData",new Size(400,180),new Thickness(0),false,true);
                        try {
                            object[] properties={Activator.CreateInstance(Record("FSPAP"))};Call(paragraph,"GetParaProperties",properties);Call(paragraph,"FormatParaFinite",args);
                            if(incoming!=IntPtr.Zero)Native("FsDestroySubpageBreakRecord",handle,incoming);
                            if(pages==0&&(IntPtr)args[13]!=IntPtr.Zero){object[] clone={handle,args[13],IntPtr.Zero};Native("FsDuplicateSubpageBreakRecord",clone);Native("FsDestroySubpageBreakRecord",handle,args[13]);args[13]=clone[2];}
                            object[] details={handle,args[12],null};Native("FsQuerySubpageDetails",details);if((int)Get(details[2],"fSimple")!=1)throw new Exception("original subpage column selection");
                        }finally{Call(format,"PopPageData");}
                    }
                    incoming=(IntPtr)args[13];var subpage=(IntPtr)args[12];
                    if(subpage==IntPtr.Zero)throw new Exception("original subpage callback returned no formatted object");
                    using((IDisposable)Call(cache,"SetDocumentArrangeContext",ownerPage)) {
                        var arrange=Get(cache,"CurrentArrangeContext");Call(arrange,"PushNewPageData",Get(ownerPage,"PageContext"),Rect(0,0,Dpi(400),Dpi(180)),true);
                        try {Call(client,"Arrange",subpage,Rect(0,0,Dpi(400),(int)args[14]),(int)args[18],0u);Call(client,"ValidateVisual",Enum("FSKUPDATE",2));Call(client,"UpdateViewport",Rect(0,0,Dpi(400),Dpi(180)));}
                        finally{Call(arrange,"PopPageData");}
                        foreach(var segment in (IEnumerable)Call(Call(client,"GetTextContentRange"),"GetTextSegments"))ranges.Add(((TextPointer)Get(segment,"Start"),(TextPointer)Get(segment,"End")));
                        totalInk+=Ink((Visual)Get(client,"Visual"));((IDisposable)client).Dispose();
                    }
                    GC.Collect(2,GCCollectionMode.Forced,true,true);
                    if(incoming==IntPtr.Zero){++pages;break;}
                }
                if(pages<2||pages>=20)throw new Exception("original subpage continuation did not finish");
                for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);int owners=ranges.Count(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0);if(owners!=1)throw new Exception($"subpage source character {at} has {owners} owners");}
                if(context.GetType().GetProperty("CallbackException",I).GetValue(context) is Exception failure)throw new Exception("original subpage cleanup callback",failure);
                Console.WriteLine($"original subpage clients {mode}: {pages} finite rendered chunks, {run.Text.Length} source characters covered exactly once, {totalInk} glyph pixels, independent cloned continuations, original arrange/viewport/visual/client disposal and GC PASS");
                {
                    object[] clientArgs={IntPtr.Zero};Call(paragraph,"CreateParaclient",clientArgs);var client=Call(context,"HandleToObject",clientArgs[0]);
                    object[] args={client,1,0u,0,Dpi(400),0,null,Enum("FSKCLEAR",0),1,null,IntPtr.Zero,0,null,IntPtr.Zero,null,0,0};
                    using((IDisposable)Call(cache,"SetDocumentFormatContext",ownerPage)) {
                        var format=Get(cache,"CurrentFormatContext");Call(format,"PushNewPageData",new Size(400,double.PositiveInfinity),new Thickness(0),false,false);
                        try {Call(paragraph,"FormatParaBottomless",args);}finally{Call(format,"PopPageData");}
                    }
                    byte[] beforeRaster;
                    if((int)args[11]<=Dpi(180)||(int)args[16]!=1)throw new Exception("original bottomless subpage must retain full content extent");
                    using((IDisposable)Call(cache,"SetDocumentArrangeContext",ownerPage)) {
                        var arrange=Get(cache,"CurrentArrangeContext");Call(arrange,"PushNewPageData",Get(ownerPage,"PageContext"),Rect(0,0,Dpi(400),(int)args[11]),false);
                        try {Call(client,"Arrange",args[10],Rect(0,0,Dpi(400),(int)args[11]),(int)args[15],0u);Call(client,"ValidateVisual",Enum("FSKUPDATE",2));Call(client,"UpdateViewport",Rect(0,0,Dpi(400),Dpi(180)));Ink((Visual)Get(client,"Visual"));
                            var range=Call(client,"GetTextContentRange");for(int at=0;at<run.Text.Length;++at)if(!(bool)Call(range,"Contains",run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward),true))throw new Exception("bottomless subpage loses source text");
                            beforeRaster=Raster((Visual)Get(client,"Visual"));
                        }finally{Call(arrange,"PopPageData");}
                    }
                    Console.WriteLine($"original bottomless subpage clients {mode}: full extent {(int)args[11]/300.0:F1}, all {run.Text.Length} source characters, viewport glyphs and original disposal PASS");
                    int previousHeight=(int)args[11];IntPtr stable=(IntPtr)args[10];
                    run.Text+=" "+string.Join(" ",Enumerable.Range(100,80).Select(i=>$"edit{i:D3}"));
                    object[] update={stable,client,1,0u,0,Dpi(300),0,null,Enum("FSKCLEAR",0),1,null,0,null,IntPtr.Zero,null,0,0};
                    using((IDisposable)Call(cache,"SetDocumentFormatContext",ownerPage)) {
                        var format=Get(cache,"CurrentFormatContext");Call(format,"PushNewPageData",new Size(300,double.PositiveInfinity),new Thickness(0),true,false);
                        try {Call(paragraph,"UpdateBottomlessPara",update);}finally{Call(format,"PopPageData");}
                    }
                    if((int)update[11]<=previousHeight||(int)update[16]!=1||(IntPtr)Get(Get(client,"_paraHandle"),"Value")!=stable)throw new Exception("original bottomless update must retain handle and grow full extent");
                    using((IDisposable)Call(cache,"SetDocumentArrangeContext",ownerPage)) {
                        var arrange=Get(cache,"CurrentArrangeContext");Call(arrange,"PushNewPageData",Get(ownerPage,"PageContext"),Rect(0,0,Dpi(300),(int)update[11]),false);
                        try {
                            Call(client,"Arrange",stable,Rect(0,0,Dpi(300),(int)update[11]),(int)update[15],0u);Call(client,"ValidateVisual",Enum("FSKUPDATE",2));Call(client,"UpdateViewport",Rect(0,0,Dpi(300),Dpi(180)));Ink((Visual)Get(client,"Visual"));
                            if(beforeRaster.SequenceEqual(Raster((Visual)Get(client,"Visual"))))throw new Exception("updated original subpage retained stale viewport glyphs");
                            var range=Call(client,"GetTextContentRange");for(int at=0;at<run.Text.Length;++at)if(!(bool)Call(range,"Contains",run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward),true))throw new Exception("updated original subpage loses source text");
                            ((IDisposable)client).Dispose();
                        }finally{Call(arrange,"PopPageData");}
                    }
                    if(context.GetType().GetProperty("CallbackException",I).GetValue(context) is Exception updateFailure)throw new Exception("original updated subpage cleanup callback",updateFailure);
                    GC.Collect(2,GCCollectionMode.Forced,true,true);
                    Console.WriteLine($"original bottomless subpage update {mode}: stable handle, width 400->300, extent {previousHeight/300.0:F1}->{(int)update[11]/300.0:F1}, all {run.Text.Length} characters, changed glyph raster and original cleanup PASS");
                }
            }finally{Call(context,"Enter");try{if(incoming!=IntPtr.Zero)Native("FsDestroySubpageBreakRecord",handle,incoming);((IDisposable)paragraph).Dispose();}finally{Call(context,"Leave");}}
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original subpage callbacks, visuals and dispatcher cleanup PASS");
    }
}
