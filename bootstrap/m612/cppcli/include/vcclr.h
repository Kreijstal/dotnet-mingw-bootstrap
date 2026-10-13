// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Intrinsic declarations for cli-clang. These have CLR byref/pinned storage,
// never a native C++ object layout or a native implementation.
#ifndef CPPCLI_VCCLR_H
#define CPPCLI_VCCLR_H
namespace System { class String; }
namespace cli {
template<class T> class __attribute__((annotate("cli.interior"))) interior_ptr {
public:
    interior_ptr(T*);
    template<class U> explicit interior_ptr(U*);
    template<class U> explicit interior_ptr(interior_ptr<U>);
    interior_ptr& operator+=(long long);
    interior_ptr& operator-=(long long);
    interior_ptr& operator++();
    interior_ptr operator++(int);
    interior_ptr& operator--();
    interior_ptr operator--(int);
    __attribute__((annotate("cli.interior.address"))) T* operator->() const;
    T& operator*() const;
    T& operator[](long long) const;
};
template<class T> class __attribute__((annotate("cli.pin"))) pin_ptr {
public:
    pin_ptr();
    pin_ptr(T*);
    pin_ptr(interior_ptr<T>);
    pin_ptr(decltype(nullptr));
    pin_ptr& operator+=(long long);
    pin_ptr& operator-=(long long);
    pin_ptr& operator++();
    pin_ptr operator++(int);
    pin_ptr& operator--();
    pin_ptr operator--(int);
    operator T*() const;
    // Explicit pointer reinterpretation is a CLI intrinsic. Keeping it
    // explicit prevents unrelated pointer types from matching native overloads.
    template<class U> explicit operator U*() const;
    __attribute__((annotate("cli.interior.address"))) T* operator->() const;
    T& operator*() const;
    T& operator[](long long) const;
};
}
using cli::pin_ptr;
using cli::interior_ptr;
__attribute__((annotate("cli.string.chars")))
cli::interior_ptr<const wchar_t> PtrToStringChars(System::String^);
#endif
