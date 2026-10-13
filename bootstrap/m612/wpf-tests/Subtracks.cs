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
    static List<(TextPointer Start,TextPointer End)> Ranges(object context,IntPtr handle,IntPtr track) {
        object[] details={handle,track,null};Native("FsQuerySubtrackDetails",details);
        object[] list={context,track,details[2],null};A.GetType("MS.Internal.PtsHost.PtsHelper",true).GetMethod("ParaListFromSubtrack",S).Invoke(null,list);
        var ranges=new List<(TextPointer,TextPointer)>();
        foreach(var desc in (Array)list[3]) {
            int kind=(int)Get(desc,"idobj");
            if(kind==0)ranges.AddRange(Ranges(context,handle,(IntPtr)Get(desc,"pfspara")));
            else if(kind==-1){var client=Call(context,"HandleToObject",Get(desc,"pfsparaclient"));foreach(var segment in (IEnumerable)Call(Call(client,"GetTextContentRange"),"GetTextSegments"))ranges.Add(((TextPointer)Get(segment,"Start"),(TextPointer)Get(segment,"End")));}
            else throw new Exception("unexpected paragraph kind in source-coverage fixture: "+kind);
        }
        return ranges;
    }
    static void Coverage(object context,IntPtr handle,IntPtr track,Run run) {
        var ranges=Ranges(context,handle,track);
        for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);if(ranges.Count(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0)!=1)throw new Exception("source text must belong to exactly one real native text range at "+at);}
    }
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(bool padded in new[]{false,true}) {
            var run=new Run(string.Join(" ",Enumerable.Range(0,100).Select(i=>$"sub{i:D3}")));
            var section=new Section(new Paragraph(run){Margin=new Thickness(0)}){Margin=padded?new Thickness(12,8,10,6):new Thickness(0),Padding=padded?new Thickness(4):new Thickness(0)};
            var document=new FlowDocument(section){FontFamily=new FontFamily("Segoe UI"),FontSize=20,Foreground=Brushes.Black,PagePadding=new Thickness(0),ColumnWidth=double.PositiveInfinity};TextOptions.SetTextFormattingMode(document,mode);
            var paginator=((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,180);using var seedPage=paginator.GetPage(0);
            var cache=Get(document,"StructuralCache");var context=Get(cache,"PtsContext");var handle=(IntPtr)Get(context,"Context");
            using var ownerPage=(DocumentPage)Activator.CreateInstance(A.GetType("MS.Internal.PtsHost.FlowDocumentPage",true),I,null,new[]{cache},null);Call(ownerPage,"SetSize",new Size(400,180));
            var paragraph=Activator.CreateInstance(A.GetType("MS.Internal.PtsHost.ContainerParagraph",true),I,null,new[]{(object)section,cache},null);
            object[] clients={IntPtr.Zero};Call(paragraph,"CreateParaclient",clients);var client=Call(context,"HandleToObject",clients[0]);IntPtr track=IntPtr.Zero;
            try {
                object[] args={client,0,IntPtr.Zero,1,0u,Dpi(10),Dpi(380),Dpi(10),null,Enum("FSKCLEAR",0),1,null,IntPtr.Zero,0,null,IntPtr.Zero,null,0,0};
                using((IDisposable)Call(cache,"SetDocumentFormatContext",ownerPage)) {
                    var format=Get(cache,"CurrentFormatContext");Call(format,"PushNewPageData",new Size(400,double.PositiveInfinity),new Thickness(0),false,false);
                    try {Call(paragraph,"FormatParaBottomless",args);track=(IntPtr)args[12];ReleaseMargin(context,args[15]);args[15]=IntPtr.Zero;}finally{Call(format,"PopPageData");}
                }
                if(track==IntPtr.Zero||(int)args[13]<=Dpi(180))throw new Exception("original container must retain full bottomless extent");
                byte[] before;
                using((IDisposable)Call(cache,"SetDocumentArrangeContext",ownerPage)) {
                    var arrange=Get(cache,"CurrentArrangeContext");Call(arrange,"PushNewPageData",Get(ownerPage,"PageContext"),Rect(0,0,Dpi(400),(int)args[13]+Dpi(10)),false);
                    try {Call(client,"Arrange",track,Rect(Dpi(10),Dpi(10),Dpi(380),(int)args[13]),(int)args[17],0u);Get(client,"Visual");Call(client,"ValidateVisual",Enum("FSKUPDATE",2));Call(client,"UpdateViewport",Rect(0,0,Dpi(400),Dpi(180)));Ink((Visual)Get(client,"Visual"));Coverage(context,handle,track,run);before=Raster((Visual)Get(client,"Visual"));}finally{Call(arrange,"PopPageData");}
                }
                int oldHeight=(int)args[13];run.Text+=" "+string.Join(" ",Enumerable.Range(100,80).Select(i=>$"edit{i:D3}"));
                object[] update={track,client,0,IntPtr.Zero,1,0u,Dpi(20),Dpi(280),Dpi(20),null,Enum("FSKCLEAR",0),1,null,0,null,IntPtr.Zero,null,0,0};
                using((IDisposable)Call(cache,"SetDocumentFormatContext",ownerPage)) {
                    var format=Get(cache,"CurrentFormatContext");Call(format,"PushNewPageData",new Size(300,double.PositiveInfinity),new Thickness(0),true,false);
                    try {Call(paragraph,"UpdateBottomlessPara",update);ReleaseMargin(context,update[15]);update[15]=IntPtr.Zero;}finally{Call(format,"PopPageData");}
                }
                if((int)update[13]<=oldHeight||(int)update[18]!=1||(IntPtr)Get(Get(client,"_paraHandle"),"Value")!=track)throw new Exception("updated container must retain identity and grow extent");
                object[] details={handle,track,null};Native("FsQuerySubtrackDetails",details);var rect=Get(details[2],"fsrc");
                if((int)Get(rect,"u")!=Dpi(20+(padded?16:0))||(int)Get(rect,"v")!=Dpi(20+(padded?4:0))||(int)Get(rect,"du")!=Dpi(280-(padded?30:0)))throw new Exception("updated original margin/padding geometry");
                using((IDisposable)Call(cache,"SetDocumentArrangeContext",ownerPage)) {
                    var arrange=Get(cache,"CurrentArrangeContext");Call(arrange,"PushNewPageData",Get(ownerPage,"PageContext"),Rect(0,0,Dpi(300),(int)update[13]+Dpi(20)),false);
                    try {Call(client,"Arrange",track,Rect(Dpi(20),Dpi(20),Dpi(280),(int)update[13]),(int)update[17],0u);Get(client,"Visual");Call(client,"ValidateVisual",Enum("FSKUPDATE",2));Call(client,"UpdateViewport",Rect(0,0,Dpi(300),Dpi(180)));Ink((Visual)Get(client,"Visual"));Coverage(context,handle,track,run);if(before.SequenceEqual(Raster((Visual)Get(client,"Visual"))))throw new Exception("updated container must change its real glyph raster");}finally{Call(arrange,"PopPageData");}
                }
                byte[] unshifted=Raster((Visual)Get(client,"Visual"));
                using((IDisposable)Call(cache,"SetDocumentArrangeContext",ownerPage)) {
                    var arrange=Get(cache,"CurrentArrangeContext");Call(arrange,"PushNewPageData",Get(ownerPage,"PageContext"),Rect(0,0,Dpi(300),(int)update[13]+Dpi(35)),false);
                    try {
                        var host=Get(context,"_ptsHost");if((int)Call(host,"SubtrackSynchronizeBottomlessPara",track,clients[0],IntPtr.Zero,0u,Dpi(15))!=0)throw new Exception("original subtrack synchronization callback");
                        object[] shifted={handle,track,null};Native("FsQuerySubtrackDetails",shifted);if((int)Get(Get(shifted[2],"fsrc"),"v")!=Dpi(35+(padded?4:0)))throw new Exception("original synchronized track coordinates");
                        Call(client,"Arrange",track,Rect(Dpi(20),Dpi(35),Dpi(280),(int)update[13]),(int)update[17],0u);Get(client,"Visual");Call(client,"ValidateVisual",Enum("FSKUPDATE",2));Call(client,"UpdateViewport",Rect(0,0,Dpi(300),Dpi(180)));Ink((Visual)Get(client,"Visual"));Coverage(context,handle,track,run);
                        if(unshifted.SequenceEqual(Raster((Visual)Get(client,"Visual"))))throw new Exception("synchronized original track must move its viewport glyphs");
                    }finally{Call(arrange,"PopPageData");}
                }
                Console.WriteLine($"original bottomless subtrack update/sync {mode}, padded={padded}: stable identity, moved/resized geometry, extent {oldHeight/300.0:F1}->{(int)update[13]/300.0:F1}, {run.Text.Length} characters covered exactly once through actual text records, updated and shifted viewport glyphs PASS");
            }finally{Call(context,"Enter");try{if(track!=IntPtr.Zero)Native("FsDestroySubtrack",handle,track);((IDisposable)client).Dispose();((IDisposable)paragraph).Dispose();}finally{Call(context,"Leave");}}
            if(context.GetType().GetProperty("CallbackException",I).GetValue(context) is Exception failure)throw new Exception("original subtrack cleanup callback",failure);GC.Collect(2,GCCollectionMode.Forced,true,true);
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original container subtrack creation/update/render/source/cleanup PASS");
    }
}
