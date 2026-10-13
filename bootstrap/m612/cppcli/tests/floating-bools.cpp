// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
public ref class FloatingBools abstract sealed {
public:
    static int Calls;
    static double Value(double value) { ++Calls; return value; }
    static bool Double(double value) { return value; }
    static bool Float(float value) { return value; }
    static bool Not(double value) { return !value; }
    static int Condition(double value) { if(Value(value)) return 1; return 0; }
};
