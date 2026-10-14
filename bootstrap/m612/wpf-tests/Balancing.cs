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
    static unsafe int Native(string method,params object[] args) {
        var declaration=Pts.GetMethod(method,S);var parameters=declaration.GetParameters();
        for(int i=0;i<args.Length;++i)if(parameters[i].ParameterType.IsPointer&&args[i] is IntPtr pointer)args[i]=Pointer.Box(pointer.ToPointer(),parameters[i].ParameterType);
        int status=(int)declaration.Invoke(null,args);if(status!=0)throw new Exception(method+" status "+status);return status;
    }
    static int Ink(Visual visual) {
        var image=new RenderTargetBitmap(400,180,96,96,PixelFormats.Pbgra32);image.Render(visual);var bytes=new byte[400*180*4];image.CopyPixels(bytes,400*4,0);
        int ink=0;for(int i=0;i<bytes.Length;i+=4)if(bytes[i+3]>100&&bytes[i]<100&&bytes[i+1]<100&&bytes[i+2]<100)++ink;
        if(ink<100){Dump(visual,0);throw new Exception("original subtrack visual has no glyph raster: "+ink+", bounds="+VisualTreeHelper.GetDescendantBounds(visual));}return ink;
    }
    static byte[] Raster(Visual visual){var image=new RenderTargetBitmap(400,180,96,96,PixelFormats.Pbgra32);image.Render(visual);var bytes=new byte[400*180*4];image.CopyPixels(bytes,400*4,0);return bytes;}
    static void Dump(Visual visual,int depth){Console.WriteLine(new string(' ',depth)+visual.GetType().Name+" bounds="+VisualTreeHelper.GetDescendantBounds(visual)+" clip="+VisualTreeHelper.GetClip(visual));for(int i=0;i<VisualTreeHelper.GetChildrenCount(visual);++i)Dump((Visual)VisualTreeHelper.GetChild(visual,i),depth+1);}
    static void ReleaseMargin(object context,object value){if(value is IntPtr handle&&handle!=IntPtr.Zero)((IDisposable)Call(context,"HandleToObject",handle)).Dispose();}
    static object Struct(string name,params (string Name,object Value)[] fields) {
        var value=Activator.CreateInstance(Record(name));foreach(var field in fields)value.GetType().GetField(field.Name,I).SetValue(value,field.Value);return value;
    }
    static object[] NativeList(string method,string type,int capacity,IntPtr handle,IntPtr content) {
        int size=Marshal.SizeOf(Record(type));IntPtr memory=Marshal.AllocHGlobal(Math.Max(1,capacity)*size);
        try{object[] args={handle,content,capacity,memory,0};Native(method,args);return Enumerable.Range(0,(int)args[4]).Select(i=>Marshal.PtrToStructure(memory+i*size,Record(type))).ToArray();}finally{Marshal.FreeHGlobal(memory);}
    }
    static List<(TextPointer Start,TextPointer End)> Sources(object context,IntPtr handle,IntPtr track,bool subtrack,ref int count,ref int sum,ref int minimum) {
        object[] details={handle,track,null};Native(subtrack?"FsQuerySubtrackDetails":"FsQueryTrackDetails",details);
        object[] list={context,track,details[2],null};A.GetType("MS.Internal.PtsHost.PtsHelper",true).GetMethod(subtrack?"ParaListFromSubtrack":"ParaListFromTrack",S).Invoke(null,list);
        var ranges=new List<(TextPointer,TextPointer)>();
        foreach(var desc in (Array)list[3]) {
            int kind=(int)Get(desc,"idobj");IntPtr content=(IntPtr)Get(desc,"pfspara");
            if(kind==0)ranges.AddRange(Sources(context,handle,content,true,ref count,ref sum,ref minimum));
            else if(kind==-1) {
                object[] text={handle,content,null};Native("FsQueryTextDetails",text);int n=(int)Get(Get(Get(text[2],"u"),"full"),"cLines");
                foreach(var line in NativeList("FsQueryLineListSingle","FSLINEDESCRIPTIONSINGLE",n,handle,content)){int height=(int)Get(line,"dvrAscent")+(int)Get(line,"dvrDescent");++count;sum+=height;minimum=Math.Min(minimum,height);}
                var client=Call(context,"HandleToObject",Get(desc,"pfsparaclient"));foreach(var segment in (IEnumerable)Call(Call(client,"GetTextContentRange"),"GetTextSegments"))ranges.Add(((TextPointer)Get(segment,"Start"),(TextPointer)Get(segment,"End")));
            } else throw new Exception("unexpected source kind "+kind);
        }
        return ranges;
    }
    [STAThread]
    static void Main() {
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})
        foreach(var direction in new[]{FlowDirection.LeftToRight,FlowDirection.RightToLeft})
        foreach(int scenario in new[]{0,1,2,3}) {
            var run=new Run(string.Join("\n",Enumerable.Range(0,6).Select(i=>$"line{i:D2}")));
            var runs=new List<Run>{run};
            var text=new Paragraph(run){Margin=new Thickness(0),LineHeight=24,KeepTogether=scenario==2};
            Block block=scenario==1?new Section(text){Margin=new Thickness(0),Padding=new Thickness(0)}:text;
            var document=new FlowDocument(new Paragraph(new Run("Owner"))){FontFamily=new FontFamily("Segoe UI"),FontSize=20,Foreground=Brushes.Black,FlowDirection=direction,PagePadding=new Thickness(0),ColumnWidth=double.PositiveInfinity};TextOptions.SetTextFormattingMode(document,mode);
            var paginator=((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(360,180);using var seed=paginator.GetPage(0);
            var cache=Get(document,"StructuralCache");var context=Get(cache,"PtsContext");var handle=(IntPtr)Get(context,"Context");
            var cell=new TableCell(block){Padding=new Thickness(0),BorderThickness=new Thickness(0)};
            if(scenario==3){cell.Blocks.Clear();var first=new Paragraph(){Margin=new Thickness(0),KeepWithNext=true};var second=new Paragraph(){Margin=new Thickness(0)};string content=run.Text;var prefix=new Run(content[..(content.IndexOf("line03")-1)]);runs.Add(prefix);first.Inlines.Add(prefix);second.Inlines.Add(run);run.Text=content[content.IndexOf("line03")..];cell.Blocks.Add(first);cell.Blocks.Add(second);}
var table=new Table();var group=new TableRowGroup();var row=new TableRow();row.Cells.Add(cell);group.Rows.Add(row);table.RowGroups.Add(group);document.Blocks.Add(table);
            using var owner=(DocumentPage)Activator.CreateInstance(A.GetType("MS.Internal.PtsHost.FlowDocumentPage",true),I,null,new[]{cache},null);Call(owner,"SetSize",new Size(360,180));
            var paragraph=Activator.CreateInstance(A.GetType("MS.Internal.PtsHost.SubpageParagraph",true),I,null,new[]{(object)cell,cache},null);var clients=new List<object>();var pages=new List<IntPtr>();int columnSize=Marshal.SizeOf(Record("FSCOLUMNINFO"));IntPtr columns=Marshal.AllocHGlobal(2*columnSize);uint fswdir=direction==FlowDirection.LeftToRight?0u:4u;
            Marshal.StructureToPtr(Struct("FSCOLUMNINFO",("durBefore",0),("durWidth",Dpi(170))),columns,false);Marshal.StructureToPtr(Struct("FSCOLUMNINFO",("durBefore",Dpi(20)),("durWidth",Dpi(170))),columns+columnSize,false);
            try {
                int previousHeight=0;
                for(int balance=0;balance<2;++balance) {
                    object[] created={IntPtr.Zero};Call(paragraph,"CreateParaclient",created);var client=Call(context,"HandleToObject",created[0]);clients.Add(client);
                    object[] args=null;
                    using((IDisposable)Call(cache,"SetDocumentFormatContext",owner)) {
                        var format=Get(cache,"CurrentFormatContext");Call(format,"PushNewPageData",new Size(360,180),new Thickness(0),false,true);
                        try {
                            object[] properties={Activator.CreateInstance(Record("FSPAP"))};Call(paragraph,"GetParaProperties",properties);
                            args=new object[]{handle,IntPtr.Zero,0,Get(Get(paragraph,"_mainTextSegment"),"Handle"),IntPtr.Zero,0,1,fswdir,Dpi(360),Dpi(180),Rect(0,0,Dpi(360),Dpi(180)),2,columns,balance,0,IntPtr.Zero,IntPtr.Zero,0,IntPtr.Zero,IntPtr.Zero,0,Enum("FSKSUPPRESSHARDBREAKBEFOREFIRSTPARA",2),null,IntPtr.Zero,IntPtr.Zero,0,null,IntPtr.Zero,0};
                            Native("FsCreateSubpageFinite",args);pages.Add((IntPtr)args[23]);ReleaseMargin(context,args[27]);
                            if((IntPtr)args[24]!=IntPtr.Zero||(int)Get(args[22],"fForcedProgress")!=0)throw new Exception("completed balanced source loses content or requires forced progress");
                        }finally{Call(format,"PopPageData");}
                    }
                    var descriptions=NativeList("FsQuerySubpageBasicColumnList","FSTRACKDESCRIPTION",2,handle,pages[balance]);int height=(int)Get(Get(descriptions[0],"fsrc"),"dv");
                    if(descriptions.Length!=2||height!=(int)Get(Get(descriptions[1],"fsrc"),"dv")||height>Dpi(180))throw new Exception("balanced column geometry");
                    if(balance==0)previousHeight=height;else if(height>=previousHeight)throw new Exception("explicit balancing must reduce completed section height");
                    using((IDisposable)Call(cache,"SetDocumentArrangeContext",owner)) {
                        var arrange=Get(cache,"CurrentArrangeContext");Call(arrange,"PushNewPageData",Get(owner,"PageContext"),Rect(0,0,Dpi(360),Dpi(180)),true);
                        try{Call(client,"Arrange",pages[balance],Rect(0,0,Dpi(360),(int)args[25]),(int)args[28],fswdir);Call(client,"ValidateVisual",Enum("FSKUPDATE",2));Call(client,"UpdateViewport",Rect(0,0,Dpi(360),Dpi(180)));}finally{Call(arrange,"PopPageData");}
                    }
                    int lineCount=0,totalHeight=0,minHeight=int.MaxValue;var ranges=new List<(TextPointer Start,TextPointer End)>();var counts=new List<int>();
                    foreach(var desc in descriptions){int first=lineCount;ranges.AddRange(Sources(context,handle,(IntPtr)Get(desc,"pfstrack"),false,ref lineCount,ref totalHeight,ref minHeight));counts.Add(lineCount-first);}
                    if(lineCount!=6||balance==1&&(scenario>=2?counts[0]!=6||counts[1]!=0:counts[0]!=3||counts[1]!=3))throw new Exception("balanced original lines/keep-together allocation: "+string.Join("/",counts));
                    foreach(var source in runs)for(int at=0;at<source.Text.Length;++at){var position=source.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);if(ranges.Count(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0)!=1)throw new Exception("balanced column source owner at "+at);}
                    object[] metrics={handle,pages[balance],0u,0,0,0};Call(context,"Enter");try{Native("FsGetSubpageColumnBalancingInfo",metrics);}finally{Call(context,"Leave");}
                    if((uint)metrics[2]!=fswdir||(int)metrics[3]!=lineCount||(int)metrics[4]!=totalHeight||(int)metrics[5]!=minHeight)throw new Exception("original balancing metrics disagree with actual line records");
                    var raster=Raster((Visual)Get(client,"Visual"));int left=0,right=0;for(int y=0;y<180;++y)for(int x=0;x<360;++x){int at=(y*400+x)*4;if(raster[at+3]>100&&raster[at]<100&&raster[at+1]<100&&raster[at+2]<100){if(x<170)++left;if(x>=190)++right;}}
                    if(left+right<100||balance==1&&scenario<2&&(left<100||right<100))throw new Exception("original balanced glyph raster lacks actual columns");
                    GC.Collect(2,GCCollectionMode.Forced,true,true);if(!raster.SequenceEqual(Raster((Visual)Get(client,"Visual"))))throw new Exception("balanced glyph raster changed after GC");
                    Console.WriteLine($"original balanced subpage {mode} {direction}, scenario={scenario}, balance={balance}: height={height/300.0:F2}, lines={string.Join("/",counts)}, original metrics={lineCount}/{totalHeight}/{minHeight}, exact {runs.Sum(r=>r.Text.Length)} source chars, glyph columns={left}/{right} and GC PASS");
                }
            }finally{Marshal.FreeHGlobal(columns);Call(context,"Enter");try{foreach(var client in clients)((IDisposable)client).Dispose();((IDisposable)paragraph).Dispose();}finally{Call(context,"Leave");}}
            if(context.GetType().GetProperty("CallbackException",I).GetValue(context) is Exception failure)throw new Exception("original balancing callback",failure);
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original subpage balancing flags, real metrics/layout/source/render and dispatcher cleanup PASS");
    }
}
