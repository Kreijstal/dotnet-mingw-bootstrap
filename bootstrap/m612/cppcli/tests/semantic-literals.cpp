using namespace System;
extern "C" int NativeLength(const char* value);
extern "C" int NativeWideLength(const wchar_t* value);
extern "C" void NativeRetain(const char* value);
extern "C" int NativeRetainedLength();
extern "C" int NativeByte(const char* value,int index);
extern "C" int NativeWideUnit(const wchar_t* value,int index);
extern "C" int NativeWideAlignment(const wchar_t* value);
public ref class StringParent {
public:
    int Pick(String^ value) { return value->Length; }
    int Pick(bool value) { return value ? -1 : -2; }
    String^ GetString(String^ value, Globalization::CultureInfo^ culture) { return value; }
};
public ref class StringChild : StringParent {};
public ref class LiteralCalls abstract sealed {
public:
    static int Receivers;
    static int FinallyCount;
    static StringChild^ Receiver(StringChild^ source) { ++Receivers; return source; }
    static int Inherited(StringChild^ source) { return Receiver(source)->Pick(("R" "AW")); }
    static int Unicode(StringChild^ source) { return source->Pick((L"東京" L" 😀")); }
    static String^ ResourceShape(StringChild^ source) { return source->GetString("IndexOutOfRangeException.InvalidSize", Globalization::CultureInfo::InvariantCulture); }
    static bool Imported(String^ source) { return source->Contains("needle"); }
    static int Native() { return NativeLength("R" "AW") + NativeWideLength(L"東京"); }
    static bool NativeBytes() {
        return NativeByte("\xFF",0)==255 && NativeByte(u8"é",0)==0xC3 && NativeByte(u8"é",1)==0xA9 &&
            NativeWideLength(L"A\0B")==1 && NativeWideUnit(L"A\0B",2)=='B' && NativeWideAlignment(L"東京")==0;
    }
    static void Retain() { NativeRetain("saved"); }
    static int Retained() { return NativeRetainedLength(); }
    static bool ObjectArguments() {
        Collections::Hashtable^ values=gcnew Collections::Hashtable();
        values->Add(("key"),(L"value"));
        return values->ContainsKey("key") && values->Count==1;
    }
    static bool LegacyFinally(bool fail) {
        try { if(fail) throw gcnew Exception(); return true; }
        catch(Exception^ error) { throw error; }
        __finally { ++FinallyCount; }
    }
};
