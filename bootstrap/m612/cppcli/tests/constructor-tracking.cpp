// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public value struct CtorData { int Value; };
public ref class CtorPayload { public: int Value; CtorPayload(int value):Value(value) {} };
public ref class TrackingCtorBase {
public:
    int Snapshot;
    TrackingCtorBase(int% value):Snapshot(value) { value+=3; GC::Collect(); }
};
public ref class TrackingCtorDerived : TrackingCtorBase {
public:
    int AfterBase;
    CtorPayload^ Retained;
    TrackingCtorDerived(int% value,CtorData% data,Object^% output,CtorPayload% payload)
        :TrackingCtorBase(value),AfterBase(value) {
        value+=4; data.Value+=5; payload.Value+=6;
        output="constructor"; GC::Collect();
    }
};
public value struct TrackingCtorValue {
    int Snapshot;
    TrackingCtorValue(int% value):Snapshot(value) { value+=9; GC::Collect(); }
};
public ref class ThrowingTrackingCtor {
public:
    ThrowingTrackingCtor(int% value) { value+=2; throw gcnew ArgumentException("constructor"); }
};
public ref class ConstructorTracking abstract sealed {
public:
    static int Indices;
    static int Index() { ++Indices; GC::Collect(); return 1; }
    static int Array(array<int>^ values) {
        TrackingCtorBase^ result=gcnew TrackingCtorBase(values[Index()]);
        return result->Snapshot;
    }
    static TrackingCtorDerived^ Create(int% value,CtorData% data,Object^% output,CtorPayload^ payload) {
        return gcnew TrackingCtorDerived(value,data,output,*payload);
    }
    static TrackingCtorValue Value(int% value) { return TrackingCtorValue(value); }
};
