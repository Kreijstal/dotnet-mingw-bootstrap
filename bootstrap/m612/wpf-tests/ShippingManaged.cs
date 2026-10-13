using System;
using System.Collections;
using System.Collections.Generic;
using System.Data;
using System.Dynamic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Resources;
using System.Runtime.CompilerServices;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Controls.Ribbon;
using System.Windows.Data;
using System.Windows.Forms.Integration;
using System.Windows.Media;
using System.Windows.Threading;
using System.Xml;
class Check {
    static readonly string[] Names={"UIAutomationClient","UIAutomationClientSideProviders","PresentationFramework-SystemCore",
        "PresentationFramework-SystemData","PresentationFramework-SystemDrawing","PresentationFramework-SystemXml",
        "PresentationFramework-SystemXmlLinq","System.Windows.Presentation","System.Windows.Controls.Ribbon","WindowsFormsIntegration"};
    [STAThread]
    static void Main() {
        int totalTypes=0,prepared=0;
        foreach(var name in Names) {
            var assembly=Assembly.Load(name);
            string token=Convert.ToHexString(assembly.GetName().GetPublicKeyToken()).ToLowerInvariant();
            string expected=name.StartsWith("PresentationFramework-System",StringComparison.Ordinal) || name=="System.Windows.Presentation" || name=="System.Windows.Controls.Ribbon" ? "b77a5c561934e089" : "31bf3856ad364e35";
            if(token!=expected || assembly.GetName().Version!=new Version(8,0,0,0)) throw new Exception("shipping identity: "+name);
            var types=assembly.GetTypes(); totalTypes+=types.Length;
            const BindingFlags All=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static|BindingFlags.DeclaredOnly;
            foreach(var type in types)
                foreach(var method in type.GetMethods(All).Cast<MethodBase>().Concat(type.GetConstructors(All))) {
                    if(method.IsAbstract || method.ContainsGenericParameters || method.GetMethodBody()==null) continue;
                    RuntimeHelpers.PrepareMethod(method.MethodHandle); ++prepared;
                }
            Console.WriteLine($"shipping assembly {name}: {types.Length} CLR types");
        }
        var desktop=AutomationElement.RootElement;
        if(desktop==null || desktop.Current.ControlType==null || desktop.GetRuntimeId().Length==0)
            throw new Exception("native automation desktop root");
        Console.WriteLine("original UIAutomationClient native desktop root PASS");
        var ribbon=new Ribbon();
        ribbon.Items.Add(new RibbonTab{Header="Source"});
        ribbon.Measure(new Size(400,160)); ribbon.Arrange(new Rect(0,0,400,160));
        ribbon.ApplyTemplate();
        if(ribbon.Template==null || VisualTreeHelper.GetChildrenCount(ribbon)==0) throw new Exception("source Ribbon BAML template");
        using(var stream=typeof(Ribbon).Assembly.GetManifestResourceStream("System.Windows.Controls.Ribbon.g.resources"))
        using(var resources=new ResourceReader(stream)) {
            if(!resources.Cast<DictionaryEntry>().Any(e=>((string)e.Key).EndsWith("generic.baml",StringComparison.OrdinalIgnoreCase)))
                throw new Exception("source Ribbon generic BAML");
        }
        Console.WriteLine("original Ribbon control, compiled BAML and default template PASS");
        dynamic model=new ExpandoObject(); model.Value="dynamic λ";
        var target=new TextBlock();
        BindingOperations.SetBinding(target,TextBlock.TextProperty,new Binding("Value"){Source=model});
        if(target.Text!="dynamic λ") throw new Exception("SystemCore dynamic binding");
        model.Value="changed";
        Dispatcher.CurrentDispatcher.Invoke(()=>{},DispatcherPriority.DataBind);
        if(target.Text!="changed") throw new Exception("SystemCore dynamic binding update");
        var table=new DataTable(); table.Columns.Add("Value",typeof(string)); table.Rows.Add("data λ");
        BindingOperations.SetBinding(target,TextBlock.TextProperty,new Binding("Value"){Source=table.DefaultView[0]});
        if(target.Text!="data λ") throw new Exception("SystemData row-view binding");
        var document=new XmlDocument(); document.LoadXml("<root><value>xml λ</value></root>");
        BindingOperations.SetBinding(target,TextBlock.TextProperty,new Binding{Source=document.DocumentElement,XPath="value"});
        if(target.Text!="xml λ") throw new Exception("SystemXml XPath binding");
        Console.WriteLine("original dynamic/data/XML extension bindings PASS");
        using(var formsHost=new WindowsFormsHost()) {
            formsHost.Child=new System.Windows.Forms.Panel();
            if(formsHost.Child==null || formsHost.PropertyMap.Keys.Count==0) throw new Exception("WindowsFormsHost");
        }
        using(var elementHost=new ElementHost()) {
            var child=new Button{Content="source interop"}; elementHost.Child=child;
            if(!ReferenceEquals(elementHost.Child,child) || elementHost.PropertyMap.Keys.Count==0) throw new Exception("ElementHost");
        }
        GC.Collect(); GC.WaitForPendingFinalizers();
        Console.WriteLine("original WPF/WinForms host construction, child/property maps and disposal PASS");
        Console.WriteLine($"shipping managed assembly checks PASS: {Names.Length} assemblies, {totalTypes} types, {prepared} prepared bodies");
    }
}
