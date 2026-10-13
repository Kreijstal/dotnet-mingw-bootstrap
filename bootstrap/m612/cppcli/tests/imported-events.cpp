#using "events.dll"
public ref class ImportedEventClient abstract sealed {
public:
    static void Add(EventSource^ source, Changed^ handler) { source->Automatic+=handler; source->Custom+=handler; }
    static void Remove(EventSource^ source, Changed^ handler) { source->Automatic-=handler; source->Custom-=handler; }
};
