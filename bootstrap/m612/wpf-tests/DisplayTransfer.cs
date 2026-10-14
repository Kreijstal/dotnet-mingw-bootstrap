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
        if(ink<100){Dump(visual,0);throw new Exception("original subtrack visual has no glyph raster: "+ink+", bounds="+VisualTreeHelper.GetDescendantBounds(visual));}return ink;
    }
    static byte[] Raster(Visual visual){var image=new RenderTargetBitmap(400,180,96,96,PixelFormats.Pbgra32);image.Render(visual);var bytes=new byte[400*180*4];image.CopyPixels(bytes,400*4,0);return bytes;}
    static void Dump(Visual visual,int depth){Console.WriteLine(new string(' ',depth)+visual.GetType().Name+" bounds="+VisualTreeHelper.GetDescendantBounds(visual)+" clip="+VisualTreeHelper.GetClip(visual));for(int i=0;i<VisualTreeHelper.GetChildrenCount(visual);++i)Dump((Visual)VisualTreeHelper.GetChild(visual,i),depth+1);}
    static void ReleaseMargin(object context,object value){if(value is IntPtr handle&&handle!=IntPtr.Zero)((IDisposable)Call(context,"HandleToObject",handle)).Dispose();}
    static object RawVisual(object client) {
        for(var type=client.GetType();type!=null;type=type.BaseType) {
            var field=type.GetField("_visual",I|BindingFlags.DeclaredOnly);if(field!=null)return field.GetValue(client);
        }
        throw new Exception("missing original visual field");
    }
    static List<object> Children(object context,IntPtr handle,IntPtr content,bool page) {
        IntPtr track=content;
        if(page) {
            object[] query={handle,content,null};Native("FsQuerySubpageDetails",query);
            track=(IntPtr)Get(Get(Get(Get(query[2],"u"),"simple"),"trackdescr"),"pfstrack");
        }
        object[] details={handle,track,null};Native(page?"FsQueryTrackDetails":"FsQuerySubtrackDetails",details);
        object[] list={context,track,details[2],null};A.GetType("MS.Internal.PtsHost.PtsHelper",true).GetMethod(page?"ParaListFromTrack":"ParaListFromSubtrack",S).Invoke(null,list);
        var children=new List<object>();
        foreach(var desc in (Array)list[3]) {
            children.Add(Call(context,"HandleToObject",Get(desc,"pfsparaclient")));
            int kind=(int)Get(desc,"idobj");if(kind==0||kind==1)children.AddRange(Children(context,handle,(IntPtr)Get(desc,"pfspara"),kind==1));
        }
        return children;
    }
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool page in new[]{false,true}) {
            var run=new Run("Same width display transfer text");
            var nested=new Section(new Paragraph(run){Margin=new Thickness(0)}){Margin=new Thickness(0),Padding=new Thickness(0)};
            var document=new FlowDocument(new Paragraph(new Run("Owner"))){FontFamily=new FontFamily("Segoe UI"),FontSize=20,Foreground=Brushes.Black,PagePadding=new Thickness(0),ColumnWidth=double.PositiveInfinity};
            TextOptions.SetTextFormattingMode(document,mode);
            var paginator=((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,180);using var seed=paginator.GetPage(0);
            var cache=Get(document,"StructuralCache");var context=Get(cache,"PtsContext");var handle=(IntPtr)Get(context,"Context");var host=Get(context,"_ptsHost");
            object element=nested;
            if(page){var cell=new TableCell(nested){Padding=new Thickness(0),BorderThickness=new Thickness(0)};var table=new Table();var group=new TableRowGroup();var row=new TableRow();row.Cells.Add(cell);group.Rows.Add(row);table.RowGroups.Add(group);document.Blocks.Add(table);element=cell;}
            else document.Blocks.Add(nested);
            using var owner=(DocumentPage)Activator.CreateInstance(A.GetType("MS.Internal.PtsHost.FlowDocumentPage",true),I,null,new[]{cache},null);Call(owner,"SetSize",new Size(400,180));
            var paragraph=Activator.CreateInstance(A.GetType("MS.Internal.PtsHost."+(page?"SubpageParagraph":"ContainerParagraph"),true),I,null,new[]{element,cache},null);
            var clients=new List<object>();var contents=new List<IntPtr>();var heights=new List<int>();var tops=new List<int>();var names=new List<IntPtr>();
            string prefix=page?"Subpage":"Subtrack";
            int Host(string method,params object[] args) {
                Call(context,"Enter");try{return (int)Call(host,method,args);}finally{Call(context,"Leave");}
            }
            void Format() {
                object[] created={IntPtr.Zero};Call(paragraph,"CreateParaclient",created);var client=Call(context,"HandleToObject",created[0]);clients.Add(client);names.Add((IntPtr)created[0]);
                object[] args=page?new object[]{client,1,0u,0,Dpi(400),0,null,Enum("FSKCLEAR",0),1,null,IntPtr.Zero,0,null,IntPtr.Zero,null,0,0}:new object[]{client,0,IntPtr.Zero,1,0u,0,Dpi(400),0,null,Enum("FSKCLEAR",0),1,null,IntPtr.Zero,0,null,IntPtr.Zero,null,0,0};
                using((IDisposable)Call(cache,"SetDocumentFormatContext",owner)) {
                    var format=Get(cache,"CurrentFormatContext");Call(format,"PushNewPageData",new Size(400,double.PositiveInfinity),new Thickness(0),false,false);
                    try{object[] properties={Activator.CreateInstance(Record("FSPAP"))};Call(paragraph,"GetParaProperties",properties);Call(paragraph,"FormatParaBottomless",args);contents.Add((IntPtr)args[page?10:12]);heights.Add((int)args[page?11:13]);tops.Add((int)args[page?15:17]);ReleaseMargin(context,args[page?13:15]);}finally{Call(format,"PopPageData");}
                }
            }
            void Arrange(int index) {
                using((IDisposable)Call(cache,"SetDocumentArrangeContext",owner)) {
                    var arrange=Get(cache,"CurrentArrangeContext");Call(arrange,"PushNewPageData",Get(owner,"PageContext"),Rect(0,0,Dpi(400),Math.Max(Dpi(180),heights[index])),false);
                    try{Call(clients[index],"Arrange",contents[index],Rect(0,0,Dpi(400),heights[index]),tops[index],0u);Get(clients[index],"Visual");Call(clients[index],"ValidateVisual",Enum("FSKUPDATE",2));Call(clients[index],"UpdateViewport",Rect(0,0,Dpi(400),Dpi(180)));}finally{Call(arrange,"PopPageData");}
                }
            }
            void Compare(int first,int second,int expected) {
                object[] args={names[first],contents[first],names[second],contents[second],0u,null,0};int status=Host(prefix+"ComparePara",args);
                if(status!=0||Convert.ToInt32(args[5])!=expected||(int)args[6]!=0)throw new Exception(prefix+" original compare callback "+status+"/"+args[5]);
            }
            try {
                Format();Arrange(0);Ink((Visual)Get(clients[0],"Visual"));var before=Raster((Visual)Get(clients[0],"Visual"));
                Compare(0,0,0);
                // Equal geometry cannot establish equal content: change only the brush.
                run.Foreground=Brushes.Red;
                // This independently constructed paragraph is outside the paginator's
                // invalidation tree; apply the original format-cache invalidation.
                Call(paragraph,"InvalidateFormatCache");Format();Compare(0,1,1);
                if(heights[0]!=heights[1])throw new Exception("brush-only edit changed geometry");
                var oldChildren=Children(context,handle,contents[0],page);var newChildren=Children(context,handle,contents[1],page);
                if(oldChildren.Count!=newChildren.Count||oldChildren.Count==0)throw new Exception("no matched source children");
                var oldVisuals=oldChildren.Select(RawVisual).ToArray();if(oldVisuals.Any(v=>v==null))throw new Exception("old source child did not render");
                var oldRoot=RawVisual(clients[0]);
                if(Host("TransferDisplayInfo",IntPtr.Zero,names[0],names[1])!=0||RawVisual(clients[0])!=null||!ReferenceEquals(RawVisual(clients[1]),oldRoot))throw new Exception("original outer visual identity/ownership transfer");
                if(Host(prefix+"TransferDisplayInfoPara",contents[0],contents[1])!=0)throw new Exception("original child transfer");
                for(int i=0;i<oldChildren.Count;++i)if(RawVisual(oldChildren[i])!=null||!ReferenceEquals(RawVisual(newChildren[i]),oldVisuals[i]))throw new Exception("child visual identity/ownership did not transfer");
                if(Host(prefix+"TransferDisplayInfoPara",contents[0],contents[1])!=0)throw new Exception("repeat transfer");
                for(int i=0;i<newChildren.Count;++i)if(!ReferenceEquals(RawVisual(newChildren[i]),oldVisuals[i]))throw new Exception("repeat transfer erased new visual");
                Arrange(1);var after=Raster((Visual)Get(clients[1],"Visual"));int red=0;for(int i=0;i<after.Length;i+=4)if(after[i+2]>100&&after[i+1]<100&&after[i]<100&&after[i+3]>100)++red;
                if(red<100||before.SequenceEqual(after))throw new Exception("moved visual retained stale brush pixels: "+red);
                Call(context,"Enter");try{if(!page)Native("FsDestroySubtrack",handle,contents[0]);((IDisposable)clients[0]).Dispose();contents[0]=IntPtr.Zero;}finally{Call(context,"Leave");}
                GC.Collect(2,GCCollectionMode.Forced,true,true);Arrange(1);if(!after.SequenceEqual(Raster((Visual)Get(clients[1],"Visual"))))throw new Exception("old client disposal corrupted transferred display");
                var range=Call(clients[1],"GetTextContentRange");for(int at=0;at<run.Text.Length;++at)if(!(bool)Call(range,"Contains",run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward),true))throw new Exception("transferred content loses source text");
                run.Text="Updated content after prior owner disposal";Call(paragraph,"InvalidateFormatCache");Format();Compare(1,2,1);
                if(Host("TransferDisplayInfo",IntPtr.Zero,names[1],names[2])!=0||Host(prefix+"TransferDisplayInfoPara",contents[1],contents[2])!=0)throw new Exception("second generation display transfer");
                Arrange(2);if(after.SequenceEqual(Raster((Visual)Get(clients[2],"Visual"))))throw new Exception("second generation retains stale text");
                Console.WriteLine($"original {prefix} {mode}: compare self/content, recursive visual identity/ownership, repeat transfer, same-width brush edit ({red} red pixels), old disposal/GC, source coverage and second-generation text edit PASS");
            }finally {
                Call(context,"Enter");try{for(int i=0;i<contents.Count;++i)if(contents[i]!=IntPtr.Zero){if(!page)Native("FsDestroySubtrack",handle,contents[i]);((IDisposable)clients[i]).Dispose();}((IDisposable)paragraph).Dispose();}finally{Call(context,"Leave");}
            }
            if(context.GetType().GetProperty("CallbackException",I).GetValue(context) is Exception failure)throw new Exception("original display transfer callback",failure);
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original managed comparison/display transfer and dispatcher cleanup PASS");
    }
}
