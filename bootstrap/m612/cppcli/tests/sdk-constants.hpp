// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#pragma once
#pragma clang system_header
constexpr unsigned long long SdkWideConstant(unsigned long long value) { return value | 0x8000000000000000ULL; }
constexpr int SdkIntegerConstant(int value) { return value + 7; }
