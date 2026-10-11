#include "native.hpp"
template<class T> struct Calculator {
    static T Sum(T left, T right) { return left + right; }
};
int NativeSum(int left, int right) { return Calculator<int>::Sum(left, right); }
long long NativeScale(long long value) { return Calculator<long long>::Sum(value, value); }
double NativeHalf(double value) { return value / 2.0; }
