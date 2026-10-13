// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#using "NestedContract.dll"
#using "NestedPeer.dll"
using namespace System;
public ref class NestedBridge abstract sealed {
public:
    static NestedContract::Outer::Flags Echo(NestedContract::Outer::Flags value) {
        return NestedPeer::Api::Echo(NestedContract::Outer::Echo(value));
    }
    static NestedContract::Outer::Flags Default() { return NestedContract::Outer::Default; }
    static int Facility(int value) { return static_cast<int>(NestedContract::Outer::Facility(value)); }
    static String^ Node(String^ text) {
        NestedContract::Outer::Node^ node=gcnew NestedContract::Outer::Node(text);
        node->Code=NestedContract::Outer::Flags::High;
        return NestedPeer::Api::Node(node)->Name;
    }
    static int Point(int value) {
        NestedContract::Outer::Point point;
        point.X=value;
        return NestedPeer::Api::Point(point).X;
    }
    static String^ Detail(String^ text) {
        NestedContract::Outer::Node::Detail^ value=gcnew NestedContract::Outer::Node::Detail(text);
        return value->Name;
    }
    static String^ LiteralNode() {
        NestedContract::Outer::Node^ node=gcnew NestedContract::Outer::Node(("R" "AW"));
        return node->Name;
    }
    static NestedContract::Outer::Flags^ Box(NestedContract::Outer::Flags value) {
        NestedContract::Outer::Flags^ box=value; return box;
    }
};
