using System;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
class Check {
    [STAThread]
    static void Main() {
        var document=new FlowDocument(new Paragraph(new Run("Source FlowDocument paragraph layout."))){FontFamily=new FontFamily("Segoe UI"),FontSize=20,PagePadding=new Thickness(10)};
        DocumentPaginator paginator=((IDocumentPaginatorSource)document).DocumentPaginator;
        paginator.PageSize=new Size(400,240);
        using var page=paginator.GetPage(0);
        if(page==DocumentPage.Missing || page.Visual==null || page.Size.Width<=0)throw new Exception("FlowDocument did not create a real page");
        Console.WriteLine("original FlowDocument finite page/visual layout PASS");
    }
}
