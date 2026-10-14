using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
class Check {
    static void Pump(FrameworkElement element){element.UpdateLayout();var frame=new DispatcherFrame();Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle,new Action(()=>frame.Continue=false));Dispatcher.PushFrame(frame);element.UpdateLayout();}
    static byte[] Raster(Visual visual){var image=new RenderTargetBitmap(400,220,96,96,PixelFormats.Pbgra32);image.Render(visual);var bytes=new byte[400*220*4];image.CopyPixels(bytes,1600,0);return bytes;}
    static (int Red,int Blue,int Black,int Green) Ink(byte[] bytes){int red=0,blue=0,black=0,green=0;for(int i=0;i<bytes.Length;i+=4){if(bytes[i+3]<150)continue;if(bytes[i+2]>200&&bytes[i]<70&&bytes[i+1]<70)++red;if(bytes[i]>180&&bytes[i+2]<70&&bytes[i+1]<70)++blue;if(bytes[i]<70&&bytes[i+1]<70&&bytes[i+2]<70)++black;if(bytes[i+1]>180&&bytes[i]<70&&bytes[i+2]<70)++green;}return(red,blue,black,green);}
    static Table Make(List<Run> runs,int rows,bool explicitColumns,int scenario=0){var table=new Table(){CellSpacing=2,Margin=new Thickness(0)};if(explicitColumns){table.Columns.Add(new TableColumn(){Width=new GridLength(120)});table.Columns.Add(new TableColumn(){Width=new GridLength(260)});}if(scenario==2){table.Margin=new Thickness(6,4,6,4);table.BorderBrush=Brushes.Black;table.BorderThickness=new Thickness(2);table.Padding=new Thickness(6);}
        var group=new TableRowGroup();table.RowGroups.Add(group);for(int r=0;r<rows;++r){if(scenario==2&&r==6){group=new TableRowGroup();table.RowGroups.Add(group);}var row=new TableRow();group.Rows.Add(row);for(int c=0;c<(scenario==2&&r==0?1:2);++c){var run=new Run($"{(c==0?"left":"right")}{r:D2}");runs.Add(run);row.Cells.Add(new TableCell(new Paragraph(run){Margin=new Thickness(0)}){ColumnSpan=scenario==2&&r==0?2:1,Padding=new Thickness(4),Background=c==0?Brushes.Red:Brushes.Blue,BorderBrush=Brushes.Black,BorderThickness=new Thickness(1)});}}return table;}
    [STAThread] static void Main(){var viewType=typeof(FlowDocument).Assembly.GetType("System.Windows.Documents.ITextView",true);
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display})foreach(int scenario in new[]{0,1,2,3,4,5}){
            bool explicitColumns=scenario==1;
            var runs=new List<Run>();var table=Make(runs,12,explicitColumns,scenario);if(scenario==5)table.RowGroups[0].Rows[0].Cells[0].Blocks.Add(Make(runs,2,false));var document=new FlowDocument(table){FontFamily=new FontFamily("Segoe UI"),FontSize=16,Foreground=Brushes.Lime,PagePadding=new Thickness(10),ColumnWidth=scenario==4?160:double.PositiveInfinity,ColumnGap=20};if(scenario==3){var prefix=new Run("prefix zero\nprefix one\nprefix two\nprefix three\nprefix four\nprefix five");runs.Add(prefix);document.Blocks.InsertBefore(table,new Paragraph(prefix){Margin=new Thickness(0),LineHeight=24});}TextOptions.SetTextFormattingMode(document,mode);
            var paginator=(DynamicDocumentPaginator)((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);var ranges=new List<(int Page,TextPointer Start,TextPointer End)>();int pages=0,totalInk=0;
            for(;pages<20;++pages){using var page=paginator.GetPage(pages);if(page==DocumentPage.Missing)break;var ink=Ink(Raster(page.Visual));if((scenario!=3||pages>0)&&(ink.Red<500||ink.Blue<500||ink.Black<50||ink.Green<30))throw new Exception("table cells must render actual backgrounds, borders and glyphs");totalInk+=ink.Black+ink.Green;
                var view=((IServiceProvider)page).GetService(viewType);foreach(var segment in (IEnumerable)viewType.GetProperty("TextSegments").GetValue(view)){var t=segment.GetType();var flags=BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic;ranges.Add((pages,(TextPointer)t.GetProperty("Start",flags).GetValue(segment),(TextPointer)t.GetProperty("End",flags).GetValue(segment)));}GC.Collect(2,GCCollectionMode.Forced,true,true);
            }
            if(pages<(scenario==4?1:2)||pages>=20||!paginator.IsPageCountValid)throw new Exception("table row pagination must complete");
            foreach(var run in runs)for(int at=0;at<run.Text.Length;++at){var position=run.ContentStart.GetPositionAtOffset(at,LogicalDirection.Forward);var owners=ranges.Where(r=>position.CompareTo(r.Start)>=0&&position.CompareTo(r.End)<0).ToArray();if(owners.Length!=1||paginator.GetPageNumber(position)!=owners[0].Page)throw new Exception("table exact cell-character coverage/page mapping at "+at);}
            using(var page=paginator.GetPage(scenario==3?1:0)){var ink=Ink(Raster(page.Visual));if(ink.Red<500||ink.Blue<500||ink.Black<50||ink.Green<30)throw new Exception("disposed table page must regenerate original cells");}
            Console.WriteLine($"original public table {mode}, scenario={scenario}: {pages} pages, {runs.Sum(r=>r.Text.Length)} cell characters covered exactly once, {totalInk} border/glyph pixels, original source/page mapping/regeneration/GC PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}){
            var document=new FlowDocument(new Table(){Margin=new Thickness(0)}){PagePadding=new Thickness(10)};TextOptions.SetTextFormattingMode(document,mode);var paginator=((IDocumentPaginatorSource)document).DocumentPaginator;paginator.PageSize=new Size(400,220);using var page=paginator.GetPage(0);if(page==DocumentPage.Missing||paginator.GetPage(1)!=DocumentPage.Missing)throw new Exception("empty table completes exactly one page");Console.WriteLine($"original empty table {mode}: finite empty content completes without a phantom continuation PASS");
        }
        foreach(var mode in new[]{TextFormattingMode.Ideal,TextFormattingMode.Display}){
            var runs=new List<Run>();var table=Make(runs,18,false);var document=new FlowDocument(table){FontFamily=new FontFamily("Segoe UI"),FontSize=16,Foreground=Brushes.Lime,PagePadding=new Thickness(10)};TextOptions.SetTextFormattingMode(document,mode);
            var viewer=new FlowDocumentScrollViewer{Document=document,IsToolBarVisible=false};var window=new Window{Content=viewer,Width=420,Height=260,ShowInTaskbar=false};window.Show();Pump(window);var scroll=(ScrollViewer)viewer.Template.FindName("PART_ContentHost",viewer);double old=scroll.ExtentHeight;var before=Raster(viewer);runs[0].Text+="\nnew cell line\nmore cell text";Pump(window);double afterHeight=scroll.ExtentHeight;var after=Raster(viewer);if(afterHeight<=old||before.SequenceEqual(after))throw new Exception("bottomless table cell edit must update row height, full extent and raster");scroll.ScrollToEnd();Pump(window);if(scroll.VerticalOffset<=0)throw new Exception("table must scroll through its full content");window.Close();Console.WriteLine($"original scrolling table {mode}: cell edit changes row height/extent {old:F1}->{afterHeight:F1}, glyph raster and end scrolling PASS");
        }
        Dispatcher.CurrentDispatcher.InvokeShutdown();Console.WriteLine("original public table layout, pagination, scrolling and dispatcher cleanup PASS");
    }
}
