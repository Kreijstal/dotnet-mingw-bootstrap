// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public ref class TemporaryProxy {
public:
    static int Constructions;
    int Value;
    TemporaryProxy^ Self;
    TemporaryProxy(int value) { ++Constructions; Value=value; Self=this; if(value<0) throw gcnew ArgumentException("constructor"); }
};
public delegate int ProxyCallback(TemporaryProxy% proxy);
public ref class TrackingTemporaries abstract sealed {
public:
    static int Operands;
    static int Callbacks;
    static TemporaryProxy^ Saved;
    static int Operand(int value) { ++Operands; GC::Collect(); return value; }
    static int Receive(TemporaryProxy% proxy) {
        ++Callbacks; GC::Collect(); Saved=proxy.Self;
        proxy.Value+=7; GC::Collect(); return proxy.Value;
    }
    static int Pair(TemporaryProxy% left,TemporaryProxy% right) {
        GC::Collect(); left.Value+=right.Value; return left.Value;
    }
    static int Direct(int value) { return Receive(TemporaryProxy(Operand(value))); }
    static int Delegate(int value) {
        ProxyCallback^ callback=gcnew ProxyCallback(&Receive);
        return callback(TemporaryProxy(Operand(value)));
    }
    static int Two(int left,int right) { return Pair(TemporaryProxy(Operand(left)),TemporaryProxy(Operand(right))); }
};
