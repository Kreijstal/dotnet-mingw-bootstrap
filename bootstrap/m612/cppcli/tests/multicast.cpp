using namespace System;
public delegate void Listener(int value);
public delegate void Change(int% value);
public ref class MulticastBridge {
public:
    Listener^ Handlers;
    static Listener^ Shared;
    static int ReceiverCount;
    static int HandlerCount;
    static MulticastBridge^ Receiver(MulticastBridge^ source) { ++ReceiverCount; return source; }
    static Listener^ Handler(Listener^ handler) { ++HandlerCount; return handler; }
    static Listener^ Append(Listener^ left, Listener^ right) { left += right; return left; }
    static Listener^ Remove(Listener^ left, Listener^ right) { left -= right; return left; }
    static void Add(MulticastBridge^ source, Listener^ value) { Receiver(source)->Handlers += Handler(value); }
    static void Subtract(MulticastBridge^ source, Listener^ value) { Receiver(source)->Handlers -= Handler(value); }
    static void AddShared(Listener^ value) { Shared += value; }
    static void RemoveShared(Listener^ value) { Shared -= value; }
    static Listener^ Expression(Listener^ left, Listener^ right) { return left += right; }
    static Listener^ ArrayAdd(array<Listener^>^ values, int index, Listener^ right) { return values[index] += right; }
    static Listener^ ArrayRemove(array<Listener^>^ values, int index, Listener^ right) { return values[index] -= right; }
    static void Invoke(Listener^ value, int argument) { value(argument); }
    static Change^ AppendChange(Change^ left, Change^ right) { left += right; return left; }
    static Action^ AppendAction(Action^ left, Action^ right) { left += right; return left; }
};
