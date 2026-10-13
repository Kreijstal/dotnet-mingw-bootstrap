// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
using namespace System;
public enum class RenderBits : unsigned int { None=0,Italic=1,Bold=2,High=0x80000000 };
public ref class ComparisonSource {
public:
    static int Gets;
    String^ Text;
    RenderBits Bits;
    property String^ Name { String^ get() { ++Gets; GC::Collect(); return Text; } }
    property RenderBits Simulations { RenderBits get() { ++Gets; GC::Collect(); return Bits; } }
};
public ref class StringEnumProperties abstract sealed {
public:
    static bool Equal(String^ left,String^ right) { return left==right; }
    static bool NotEqual(String^ left,String^ right) { return left!=right; }
    static bool Literal(String^ value) { return value==".ttf" && ".ttf"==value && value!=".otf"; }
    static bool Unicode(String^ value) { return value==L"東京 😀" && L"東京 😀"==value; }
    static bool Property(ComparisonSource^ value) { return value->Name==".ttf"; }
    static RenderBits Flags(ComparisonSource^ value) { return (value->Simulations & RenderBits::High) | RenderBits::Italic; }
    static bool Native(const char* left,const char* right) { return left==right; }
};
