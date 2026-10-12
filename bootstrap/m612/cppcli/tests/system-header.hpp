// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#pragma clang system_header
template<class T> struct [[clang::no_specializations]] SystemTrait {};
template<> struct SystemTrait<int> {};
