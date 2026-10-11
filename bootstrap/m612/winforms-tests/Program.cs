using System;
using System.Drawing;
using System.Windows.Forms;

static class Program
{
    [STAThread]
    static void Main()
    {
        Application.SetHighDpiMode(HighDpiMode.SystemAware);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        using var image = new Bitmap(8, 8);
        using (var graphics = Graphics.FromImage(image)) graphics.Clear(Color.CornflowerBlue);
        if (image.GetPixel(3, 3).ToArgb() != Color.CornflowerBlue.ToArgb()) throw new Exception("GDI+ pixels");
        using var form = new Form { Text = "Source-built WinForms", Size = new Size(400, 240) };
        using var button = new Button { Text = "Source-built button", Dock = DockStyle.Top };
        using var grid = new DataGridView { Dock = DockStyle.Fill };
        grid.Columns.Add("message", "Message");
        grid.Rows.Add("Hello from source-built WinForms");
        form.Controls.Add(grid);
        form.Controls.Add(button);
        int clicks = 0;
        button.Click += (_, _) => ++clicks;
        using var preview = new PrintPreviewDialog(); // exercises its embedded resx/icons
        using var navigator = new BindingNavigator(true);
        using var timer = new System.Windows.Forms.Timer { Interval = 250 };
        bool shown = false;
        form.Shown += (_, _) => {
            shown = true;
            button.PerformClick();
            if (!form.IsHandleCreated || !button.IsHandleCreated || grid.Rows.Count != 2 || clicks != 1 || preview.Text.Length == 0 || navigator.Items.Count == 0)
                throw new Exception("WinForms controls/resources");
            timer.Start();
        };
        timer.Tick += (_, _) => { timer.Stop(); form.Close(); };
        Application.Run(form);
        if (!shown || clicks != 1) throw new Exception("message loop");
        Console.WriteLine("WinForms window, controls, resources, events and GDI+ OK");
    }
}
