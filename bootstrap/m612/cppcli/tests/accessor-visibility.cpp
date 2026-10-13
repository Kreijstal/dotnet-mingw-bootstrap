// SPDX-License-Identifier: MIT
public ref class AccessorBox {
    int _value;
public:
    AccessorBox():_value(7) {}
    property virtual int Value {
        public: int get() { return _value; }
        internal: void set(int value) { _value=value; }
    }
    int After() { return _value+1; }
    void Update(int value) { Value=value; }
    property int ProtectedRead {
        protected: int get() { return _value; }
        private: void set(int value) { _value=value; }
    }
    int PublicAfter() { return ProtectedRead; }
    property int External {
        public: int get();
        internal: void set(int value);
    }
    void UpdateExternal(int value) { External=value; }
};
int AccessorBox::External::get() { return _value; }
void AccessorBox::External::set(int value) { _value=value; }
