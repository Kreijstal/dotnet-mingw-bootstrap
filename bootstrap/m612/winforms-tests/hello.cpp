using namespace System;
using namespace System::Windows::Forms;
int main() {
    Application::EnableVisualStyles();
    Form^ form = gcnew Form();
    form->Text = gcnew String("C++/CLI with source-built WinForms");
    form->Show();
    Application::DoEvents();
    Console::WriteLine(form->IsHandleCreated);
    form->Close();
    return 0;
}
