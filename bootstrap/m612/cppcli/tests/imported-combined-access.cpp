#using "combined-access.dll"
public ref class ForeignAccessDerived : CombinedAccess {
public:
    int Read() { return OrMethod(); }
};
