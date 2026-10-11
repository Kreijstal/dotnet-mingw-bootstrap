using namespace System;
public ref class Errors abstract sealed {
public:
    static int Flow(int mode) {
        try {
            Console::WriteLine("try");
            if (mode == 1) throw gcnew InvalidOperationException();
            return 7;
        } catch (InvalidOperationException^ error) {
            Console::WriteLine("catch");
            return 9;
        } finally {
            Console::WriteLine("finally");
        }
    }
    static int Rethrow() {
        try {
            try { throw gcnew InvalidOperationException(); }
            catch (Exception^ error) { Console::WriteLine("rethrow"); throw; }
        } catch (...) { Console::WriteLine("all"); return 13; }
    }
    static void Loop() {
        for (int i = 0; i < 3; ++i) {
            try { if (i == 0) continue; if (i == 1) break; }
            finally { Console::WriteLine(i); }
        }
    }
    static void Fail() { throw gcnew InvalidOperationException(); }
    static int Bounds(array<int>^ values) {
        try { return values[5]; }
        catch (Exception^ error) { return 11; }
    }
    static int OnlyFinally() {
        try { return 21; }
        finally { Console::WriteLine("only finally"); }
    }
};
int main() {
    Console::WriteLine(Errors::Flow(0));
    Console::WriteLine(Errors::Flow(1));
    Console::WriteLine(Errors::Rethrow());
    Errors::Loop();
    Console::WriteLine(Errors::Bounds(gcnew array<int>(1)));
    Console::WriteLine(Errors::OnlyFinally());
    return 0;
}
