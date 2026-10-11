#pragma once
#ifdef CPPCLI_NATIVE_BUILD
#define CPPCLI_EXPORT __declspec(dllexport)
#else
#define CPPCLI_EXPORT __declspec(dllimport)
#endif
extern "C" {
CPPCLI_EXPORT int NativeSum(int, int);
CPPCLI_EXPORT long long NativeScale(long long);
CPPCLI_EXPORT double NativeHalf(double);
}
