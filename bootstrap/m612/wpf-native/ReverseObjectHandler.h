// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
// Private protocol shared by this source Line Services producer and consumer.
// Original WPF only forwards the opaque objectInfo pointer; it never reads this
// record. This is not a reconstruction of Microsoft's unpublished handler ABI.
using WpfReverseTransition=int(WINAPI*)(void*,int,int,int,int*);
struct WpfReverseObjectHandler {
    uint32_t version=0,kind=0;
    void* context=nullptr;
    WpfReverseTransition transition=nullptr;
};
constexpr uint32_t WpfReverseObjectKind=0x52565331;
