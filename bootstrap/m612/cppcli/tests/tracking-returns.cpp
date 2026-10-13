// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public value struct ReturnData { int Counter; String^ Text; };
public ref class ReturnOwner {
public:
    int Number;
    ReturnData Data;
    static int Shared;
    int Calls;
    int FinallyCalls;
    int% Counter() { ++Calls; return Number; }
    ReturnData% Payload() { ++Calls; return Data; }
    String^% Text();
    int% Nested() { return Data.Counter; }
    int% Protected() { try { return Number; } finally { ++FinallyCalls; GC::Collect(); } }
    static int% Static() { return Shared; }
};
String^% ReturnOwner::Text() { return Data.Text; }
public ref class TrackingReturns abstract sealed {
public:
    static int% Fresh() { ReturnOwner^ owner=gcnew ReturnOwner(); owner->Number=31; return owner->Number; }
    static int% Array(array<int>^ values,int index) { return values[index]; }
    static ReturnData% Values(array<ReturnData>^ values,int index) { return values[index]; }
    static int NestedRead(ReturnOwner^ owner) { return owner->Payload().Counter; }
    static void NestedWrite(ReturnOwner^ owner,int value) { owner->Payload().Counter=value; }
    static int Read(ReturnOwner^ owner) { return owner->Counter(); }
    static int Alias(ReturnOwner^ owner) { int% alias=owner->Counter(); GC::Collect(); alias+=7; return alias; }
    static void Payload(ReturnOwner^ owner) {
        ReturnData% alias=owner->Payload(); GC::Collect(); alias.Counter+=3; alias.Text="retained";
    }
};
