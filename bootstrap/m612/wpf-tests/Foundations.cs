using System;
using System.ComponentModel;
using System.Windows;
using System.Windows.Input.Manipulations;
using System.Xaml;
using System.Threading.Tasks;
using System.Windows.Threading;
using System.Runtime.InteropServices;
using System.Text;

static class Program
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateWindowExW(int extendedStyle, string className, string name, int style,
        int x, int y, int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr parameter);
    [DllImport("user32.dll")]
    static extern bool DestroyWindow(IntPtr window);
    [DllImport("kernel32.dll")]
    static extern void SetLastError(uint error);
    [DllImport("PresentationNative_cor3.dll", EntryPoint = "EnableWindowWrapper", SetLastError = true)]
    static extern bool EnableWindow(IntPtr window, bool enabled);
    [DllImport("PresentationNative_cor3.dll", EntryPoint = "GetWindowLongWrapper", SetLastError = true)]
    static extern int GetWindowLong(IntPtr window, int index);
    [DllImport("PresentationNative_cor3.dll", EntryPoint = "GetWindowTextWrapper", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern int GetWindowText(IntPtr window, StringBuilder text, int capacity);
    [DllImport("PresentationNative_cor3.dll", CallingConvention = CallingConvention.Cdecl)]
    [return: MarshalAs(UnmanagedType.I1)]
    static extern bool IsWindows10OrGreater();

    static void Require(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    [STAThread]
    static void Main()
    {
        Require(typeof(Point).Assembly.GetName().Version.Major == 8, "source-built WindowsBase identity");
        Point point = (Point)TypeDescriptor.GetConverter(typeof(Point)).ConvertFromInvariantString("3,4");
        Require(point == new Point(3, 4), "Point converter");
        Rect rect = Rect.Parse("1,2,3,4");
        Require(rect.Contains(new Point(2, 3)) && !rect.Contains(new Point(10, 10)), "Rect geometry");
        try { Point.Parse("invalid"); throw new Exception("Point parse accepted invalid input"); }
        catch (FormatException ex) { Require(ex.Message.Length > 0, "WindowsBase embedded strings"); }

        object value = XamlServices.Parse("<x:String xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\">source bootstrap</x:String>");
        Require((string)value == "source bootstrap", "XAML object reader");
        Require((string)XamlServices.Parse(XamlServices.Save(value)) == (string)value, "XAML round trip");
        Require(typeof(System.Windows.Automation.Provider.IInvokeProvider).Assembly.GetName().Version.Major == 8,
                "source-built UIAutomationProvider identity");

        var dependency = new DependencyObject();
        var property = DependencyProperty.RegisterAttached("BootstrapValue", typeof(int), typeof(Program), new PropertyMetadata(2));
        Require((int)dependency.GetValue(property) == 2, "dependency property default");
        dependency.SetValue(property, 9);
        Require((int)dependency.GetValue(property) == 9, "dependency property value");
        Require(!Task.Run(() => dependency.CheckAccess()).GetAwaiter().GetResult(), "dispatcher thread affinity");
        bool dispatched = false;
        var frame = new DispatcherFrame();
        Dispatcher.CurrentDispatcher.BeginInvoke(new Action(() => { dispatched = true; frame.Continue = false; }));
        Dispatcher.PushFrame(frame);
        Require(dispatched, "dispatcher message loop");
        const string title = "WPF native café 東京";
        IntPtr window = CreateWindowExW(0, "STATIC", title, 0, 0, 0, 50, 50,
            new IntPtr(-3), IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
        Require(window != IntPtr.Zero, "native message-only window");
        try
        {
            SetLastError(87);
            Require(!EnableWindow(window, false) && Marshal.GetLastWin32Error() == 0,
                    "EnableWindow zero-success last-error semantics");
            SetLastError(87);
            Require(GetWindowLong(window, -21) == 0 && Marshal.GetLastWin32Error() == 0,
                    "GetWindowLong zero-success last-error semantics");
            var text = new StringBuilder(64);
            Require(GetWindowText(window, text, text.Capacity) == title.Length && text.ToString() == title,
                    "native Unicode window text");
            Require(IsWindows10OrGreater(), "native OS version bool ABI");
        }
        finally { Require(DestroyWindow(window), "native window cleanup"); }

        var processor = new ManipulationProcessor2D(Manipulations2D.TranslateX | Manipulations2D.TranslateY);
        bool started = false, completed = false;
        float x = 0, y = 0;
        processor.Started += (_, _) => started = true;
        processor.Delta += (_, args) => { x += args.Delta.TranslationX; y += args.Delta.TranslationY; };
        processor.Completed += (_, _) => completed = true;
        processor.ProcessManipulators(0, new[] { new Manipulator2D(1, 0, 0) });
        processor.ProcessManipulators(10000, new[] { new Manipulator2D(1, 5, 7) });
        processor.CompleteManipulation(20000);
        Require(started && completed && Math.Abs(x - 5) < 0.001 && Math.Abs(y - 7) < 0.001,
                "manipulation events and translation");
        Console.WriteLine("WPF foundations: geometry, embedded strings, XAML round trip, automation identity, dependency properties, dispatcher, native Win32/Unicode ABI and manipulation events OK");
    }
}
