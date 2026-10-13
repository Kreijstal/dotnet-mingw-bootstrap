using namespace System;
public delegate void Changed(int value);
public delegate void Adjust(int% value);
public delegate int Compute(int value);
public ref class EventSource {
    Changed^ _custom;
public:
    int Adds;
    int Removes;
    int property;
    int event;
    int Context(int property, int event) { return property+event; }
    event Changed^ Automatic;
    event Adjust^ ByReference;
    event Action^ Imported;
internal:
    event Changed^ InternalEvent;
public:
    void AddInternal(Changed^ value) { InternalEvent+=value; }
    void FireInternal(int value) { InternalEvent(value); }
    event Changed^ Custom {
        public:
        virtual void add(Changed^ value) { ++Adds; _custom+=value; }
        virtual void remove(Changed^ value) { ++Removes; _custom-=value; }
        protected:
        virtual void raise(int value) { Changed^ saved=_custom; if(saved!=nullptr) saved(value); }
    }
    void Fire(int value) { Automatic(value); Custom(value); }
    void FireByRef(int% value) { ByReference(value); }
    void FireImported() { Imported(); }
    void Subscribe(Changed^ value) { Custom+=value; }
    void Unsubscribe(Changed^ value) { Custom-=value; }
    int PublicAfter() { return 42; }
};
public ref class DerivedEvents : EventSource {
public:
    virtual event Changed^ Custom {
        void add(Changed^ value) override { EventSource::Custom::add(value); }
        void remove(Changed^ value) override { EventSource::Custom::remove(value); }
        protected:
        void raise(int value) override { EventSource::Custom::raise(value+1); }
    }
    void FireDerived(int value) { Custom(value); }
};
public ref class EventClient abstract sealed {
public:
    static int Receivers;
    static EventSource^ Receiver(EventSource^ source) { ++Receivers; return source; }
    static void Add(EventSource^ source, Changed^ value) { Receiver(source)->Automatic+=value; }
    static void Remove(EventSource^ source, Changed^ value) { Receiver(source)->Automatic-=value; }
};
public ref class ResultEvent {
    Compute^ _handler;
public:
    event Compute^ Evaluate {
        void add(Compute^ value) { _handler+=value; }
        void remove(Compute^ value) { _handler-=value; }
        int raise(int value) { return _handler!=nullptr ? _handler(value) : -1; }
    }
    int Call(int value) { return Evaluate(value); }
};
public ref class ExternalEvent {
    Changed^ _handler;
public:
    event Changed^ ChangedEvent {
        void add(Changed^ value);
        void remove(Changed^ value);
        void raise(int value);
    }
};
void ExternalEvent::ChangedEvent::add(Changed^ value) { _handler+=value; }
void ExternalEvent::ChangedEvent::remove(Changed^ value) { _handler-=value; }
void ExternalEvent::ChangedEvent::raise(int value) { if(_handler!=nullptr) _handler(value); }
