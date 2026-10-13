// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
const double PixelEpsilon=0.015625/100;
const double NegativeZero=-0.0;
const double Tiny=4.9406564584124654e-324;
const unsigned long long Wide=0xffffffffffffffffULL;
const unsigned short Small=0xffff;
const bool Flag=true;
const int Signed=-7;
const float Single=0.125f;
public ref class GlobalScalars abstract sealed {
public:
    static double Threshold() { return PixelEpsilon; }
    static double Zero() { return NegativeZero; }
    static double Subnormal() { return Tiny; }
    static unsigned long long Unsigned() { return Wide; }
    static int Narrow() { return Small; }
    static bool Boolean() { return Flag; }
    static int Negative() { return Signed; }
    static float Float() { return Single; }
    static bool Close(double difference) { return difference<PixelEpsilon; }
};
