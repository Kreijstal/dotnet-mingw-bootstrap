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
    T& operator*() const;
};
template<class T> class __attribute__((annotate("cli.pin"))) pin_ptr {
public:
    pin_ptr();
    pin_ptr(T*);
    pin_ptr(interior_ptr<T>);
    pin_ptr(decltype(nullptr));
    operator T*() const;
    // Explicit pointer reinterpretation is a CLI intrinsic. Keeping it
    // explicit prevents unrelated pointer types from matching native overloads.
    template<class U> explicit operator U*() const;
    T& operator*() const;
};
}
using cli::pin_ptr;
using cli::interior_ptr;
__attribute__((annotate("cli.string.chars")))
cli::interior_ptr<const wchar_t> PtrToStringChars(System::String^);
#endif
