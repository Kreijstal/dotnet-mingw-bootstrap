// SPDX-License-Identifier: MIT
using namespace System;
public ref class InitializerNode {
public:
    int Value;
    InitializerNode(int value):Value(value) {}
};
public ref class InitializerArrays abstract sealed {
    static int _sequence;
    static int Next(int value) { _sequence=_sequence*10+value/11; return value; }
    static array<int>^ _numbers={Next(11),Next(22),Next(33)};
    static array<String^>^ _words={"R" "AW",L"東京" L" 😀"};
    static array<Type^>^ _types={Int32::typeid,InitializerNode::typeid};
    static array<InitializerNode^>^ _nodes={gcnew InitializerNode(7),nullptr,gcnew InitializerNode(9)};
    static array<int>^ _empty={};
    static array<unsigned char>^ _converted={static_cast<unsigned char>(300)};
public:
    static int Sequence() { return _sequence; }
    static array<int>^ Numbers() { return _numbers; }
    static array<String^>^ Words() { return _words; }
    static array<Type^>^ Types() { return _types; }
    static array<InitializerNode^>^ Nodes() { return _nodes; }
    static array<int>^ Empty() { return _empty; }
    static array<unsigned char>^ Converted() { return _converted; }
};
