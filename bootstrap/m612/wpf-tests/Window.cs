using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Ellipse=System.Windows.Shapes.Ellipse;

class Check {
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern int GetWindowLongW(IntPtr hwnd,int index);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr hwnd,uint flags);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageW(IntPtr hwnd,uint message,IntPtr wparam,IntPtr lparam);
    [STAThread]
    static int Main(string[] args) {
        bool text=args.Contains("--text");
        bool indic=args.Contains("--indic");
        bool wrap=args.Contains("--wrap");
        bool edit=args.Contains("--edit");
        var app=new Application();
        app.DispatcherUnhandledException+=(s,e)=>{
            Console.Error.WriteLine(e.Exception); e.Handled=true; app.Shutdown(1);
        };
        var button=new Button { Width=160,Height=90,
            Content=text ? (object)(indic?"कि":"Source WPF text") : new Ellipse { Width=50,Height=50,Fill=Brushes.Blue } };
        if(indic) { button.FontFamily=new FontFamily("Nirmala UI"); button.FontSize=24; }
        TextBlock wrapped=null;
        if(wrap) {
            wrapped=new TextBlock { Text=indic?"कि कि कि कि कि कि":"alpha beta gamma delta", Width=90,
                FontFamily=new FontFamily("Nirmala UI"),FontSize=20,TextWrapping=TextWrapping.Wrap };
            button.Height=130; button.Content=wrapped;
        }
        int clicks=0;
        button.Click+=(s,e)=>clicks++;
        var panel=new Grid { Background=Brushes.White };
        TextBox editor=null;
        if(edit) {
            editor=new TextBox { Width=180,Height=130,VerticalAlignment=VerticalAlignment.Center,
                HorizontalAlignment=HorizontalAlignment.Center,FontFamily=new FontFamily("Nirmala UI"),
                FontSize=24,TextWrapping=TextWrapping.Wrap,AcceptsReturn=true,Text="कि alpha beta gamma" };
            panel.Children.Add(editor);
        } else panel.Children.Add(button);
        var window=new Window { Title="Source-built WPF 8",Width=320,Height=200,Content=panel };
        var timer=new DispatcherTimer { Interval=TimeSpan.FromMilliseconds(500) };
        bool edited=false;
        timer.Tick+=(s,e)=>{
            timer.Stop();
            try {
                window.UpdateLayout();
                var hwnd=new WindowInteropHelper(window).Handle;
                Console.WriteLine($"window state: hwnd={hwnd}, valid={IsWindow(hwnd)}, style=0x{GetWindowLongW(hwnd,-16):X8}, visibility={window.Visibility}, visible={window.IsVisible}, loaded={window.IsLoaded}");
                for(var parent=GetAncestor(hwnd,1); parent!=IntPtr.Zero; parent=GetAncestor(parent,1))
                    Console.WriteLine($"native parent: hwnd={parent}, visible={IsWindowVisible(parent)}, style=0x{GetWindowLongW(parent,-16):X8}");
                if(!IsWindowVisible(hwnd)) {
                    // Process launchers can inherit SW_HIDE for the
                    // first native ShowWindow call. Exercise the WPF show path again.
                    window.Hide(); window.Show(); window.UpdateLayout();
                }
                if(!IsWindowVisible(hwnd)) throw new Exception("window is not visible after WPF hide/show");
                if(edit&&!edited) {
                    if(editor.Template==null || VisualTreeHelper.GetChildrenCount(editor)==0) throw new Exception("default editor theme template missing");
                    string initial=editor.Text;
                    int changes=0; editor.TextChanged+=(sender,change)=>++changes;
                    editor.Focus(); editor.Select(3,5); editor.SelectedText="source";
                    if(editor.Text!="कि source beta gamma" || changes==0 || !editor.CanUndo) throw new Exception("TextBox edit/undo registration failed");
                    editor.Undo(); if(editor.Text!=initial || !editor.CanRedo) throw new Exception("TextBox undo failed");
                    editor.Redo(); if(editor.Text!="कि source beta gamma") throw new Exception("TextBox redo failed");
                    if(!editor.IsKeyboardFocused) throw new Exception("TextBox keyboard focus missing");
                    int inputs=0;
                    editor.AddHandler(TextCompositionManager.TextInputEvent,new TextCompositionEventHandler((sender,input)=>++inputs),true);
                    editor.CaretIndex=editor.Text.Length;
                    SendMessageW(hwnd,0x0102,new IntPtr('!'),new IntPtr(1));
                    if(editor.Text!="कि source beta gamma!" || inputs!=1) throw new Exception("native WM_CHAR did not reach TextBox text composition/editing");
                    editor.Undo(); if(editor.Text!="कि source beta gamma") throw new Exception("native character input undo failed");
                    editor.UpdateLayout();
                    var caret=editor.GetRectFromCharacterIndex(3);
                    if(caret.IsEmpty || caret.Height<=0) throw new Exception("TextBox caret rectangle missing");
                    int hit=editor.GetCharacterIndexFromPoint(new Point(caret.X+0.5,caret.Y+caret.Height/2),true);
                    if(hit!=3) throw new Exception("TextBox point hit does not match caret rectangle: "+hit);
                    editor.Select(0,2); if(editor.SelectedText!="कि") throw new Exception("TextBox shaped selection failed");
                    editor.UpdateLayout();
                    if(editor.LineCount<2) throw new Exception("TextBox did not wrap");
                    Console.WriteLine($"source TextBox: focus={editor.IsKeyboardFocused}, {changes} text changes, edit/undo/redo, native WM_CHAR composition/input/undo, {editor.LineCount} lines, caret/point geometry and Indic selection PASS");
                    // Let the normal render/selection invalidation queues run
                    // before capturing the edited control on the next tick.
                    edited=true; timer.Start(); return;
                } else if(!edit) {
                    if(button.Template==null || VisualTreeHelper.GetChildrenCount(button)==0) throw new Exception("default button theme template missing");
                    button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                    if(clicks!=1) throw new Exception("button routed click failed");
                }
                var bitmap=new RenderTargetBitmap((int)panel.ActualWidth,(int)panel.ActualHeight,96,96,PixelFormats.Pbgra32);
                bitmap.Render(panel);
                var pixels=new byte[bitmap.PixelWidth*bitmap.PixelHeight*4];
                bitmap.CopyPixels(pixels,bitmap.PixelWidth*4,0);
                var encoder=new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
                var path=Path.Combine(AppContext.BaseDirectory,edit?"source-window-edit.png":wrap?(indic?"source-window-wrap-indic.png":"source-window-wrap.png"):indic?"source-window-indic.png":text?"source-window-text.png":"source-window.png");
                using(var file=File.Create(path)) encoder.Save(file);
                if(edit) {
                    int dark=0;
                    var top=editor.TranslatePoint(new Point(5,5),panel);
                    for(int y=(int)top.Y;y<(int)top.Y+110;++y)
                        for(int x=(int)top.X;x<(int)top.X+165;++x) {
                            int i=(y*bitmap.PixelWidth+x)*4;
                            if(pixels[i]<80&&pixels[i+1]<80&&pixels[i+2]<80&&pixels[i+3]==255) ++dark;
                        }
                    if(dark<100) throw new Exception("TextBox glyph pixels did not render");
                    var first=editor.GetRectFromCharacterIndex(0);
                    var after=editor.GetRectFromCharacterIndex(2);
                    var offset=editor.TranslatePoint(new Point(0,0),panel);
                    int selected=0;
                    for(int y=(int)(offset.Y+first.Y)+1;y<(int)(offset.Y+first.Bottom)-1;++y)
                        for(int x=(int)(offset.X+first.X)+1;x<(int)(offset.X+after.X)-1;++x) {
                            int i=(y*bitmap.PixelWidth+x)*4;
                            if(pixels[i]>150&&pixels[i+1]>100&&pixels[i+2]+20<pixels[i]) ++selected;
                        }
                    Console.WriteLine($"selection pixels={selected}, first={first}, after={after}, start={editor.SelectionStart}, length={editor.SelectionLength}");
                    if(selected<20) throw new Exception("Indic selection highlight did not render at its caret bounds");
                } else if(!text) {
                    int blue=0;
                    for(int i=0;i<pixels.Length;i+=4) if(pixels[i]>240 && pixels[i+1]<15 && pixels[i+2]<15 && pixels[i+3]==255) blue++;
                    if(blue<100) throw new Exception("themed button content did not render");
                } else if(wrap) {
                    if(wrapped.ActualHeight<40) throw new Exception("wrapped TextBlock did not lay out multiple lines");
                    int bands=0,blankRows=3;
                    for(int y=bitmap.PixelHeight/2-55;y<bitmap.PixelHeight/2+55;++y) {
                        int dark=0;
                        for(int x=bitmap.PixelWidth/2-45;x<bitmap.PixelWidth/2+45;++x) {
                            int i=(y*bitmap.PixelWidth+x)*4;
                            if(pixels[i]<80 && pixels[i+1]<80 && pixels[i+2]<80 && pixels[i+3]==255) ++dark;
                        }
                        if(dark>=3) { if(blankRows>=3) ++bands; blankRows=0; }
                        else ++blankRows;
                    }
                    if(bands<2) throw new Exception("wrapped glyph pixels did not occupy multiple lines");
                    Console.WriteLine($"wrapped TextBlock: height={wrapped.ActualHeight:F3}, {bands} distinct glyph-pixel bands PASS");
                } else {
                    // Count dark glyph pixels in the central content area,
                    // excluding the control's border and background.
                    int dark=0;
                    for(int y=bitmap.PixelHeight/2-10;y<bitmap.PixelHeight/2+10;++y)
                        for(int x=bitmap.PixelWidth/2-60;x<bitmap.PixelWidth/2+60;++x) {
                            int i=(y*bitmap.PixelWidth+x)*4;
                            if(pixels[i]<80 && pixels[i+1]<80 && pixels[i+2]<80 && pixels[i+3]==255) ++dark;
                        }
                    if(dark<30) throw new Exception("themed button text glyph pixels did not render");
                }
                Console.WriteLine((edit?"visible source WPF TextBox, default theme, native input, undo and rendered Indic selection OK: ":"visible source WPF window, default Button template, routed click and rendered content OK: ")+path);
                window.Close();
            } catch(Exception failure) { Console.Error.WriteLine(failure); app.Shutdown(1); }
        };
        timer.Start();
        try { return app.Run(window); }
        catch(Exception failure) { Console.Error.WriteLine(failure); return 1; }
    }
}
