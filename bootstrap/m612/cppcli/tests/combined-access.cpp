public ref class CombinedAccess {
public:
    CombinedAccess() { OrField=20; AndField=22; }
public protected:
    int OrField;
    int OrMethod() { return OrField; }
    property int OrAutomatic;
protected private:
    int AndField;
    int AndMethod() { return AndField; }
private protected:
    static int AndStatic;
protected public:
    static int OrStatic;
public:
    property int Mixed {
        public protected: int get() { return OrField; }
        private protected: void set(int value) { AndField=value; }
    }
public private:
    int AssemblyOnly() { return 42; }
public:
    static int SameAssembly(CombinedAccess^ source) { return source->OrMethod()+source->AssemblyOnly(); }
};
public ref class LocalAccessDerived : CombinedAccess {
public:
    int ReadBoth() { OrAutomatic=42; Mixed=23; return OrMethod()+AndMethod(); }
};
