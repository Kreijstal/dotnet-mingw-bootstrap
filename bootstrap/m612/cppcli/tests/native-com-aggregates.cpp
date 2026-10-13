// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#include "native-com-aggregates.hpp"
#include <objidl.h>
using namespace System;
extern "C" void* create_aggregates();
extern "C" long long native_stream_seek();
public ref class NativeComAggregates abstract sealed {
public:
    static int Calls;
    static PairArg Pair() { ++Calls; PairArg value={0x89abcdef,0xfedcba98}; return value; }
    static int Verify() {
        IAggregates* probe=(IAggregates*)create_aggregates();
        ByteArg a={0xa5}; WordArg b={0xf123}; FloatArg c={1.25f};
        if(probe->Mix(a,b,c,Pair(),-2.5,42)!=73 || Calls!=1 || a.Value!=0xa5) return 1;
        LARGE_INTEGER position; position.QuadPart=-5000000002LL;
        ULARGE_INTEGER result;
        GC::Collect();
        if(probe->SeekValue(position,&result)!=S_FALSE || result.QuadPart!=(unsigned long long)-5000000002LL || position.QuadPart!=-5000000002LL) return 2;
        if(probe->Release()!=0) return 3;
        IStream* stream=nullptr;
        if(CreateStreamOnHGlobal(nullptr,TRUE,&stream)!=S_OK) return 4;
        position.QuadPart=1234567LL;
        if(stream->Seek(position,STREAM_SEEK_SET,&result)!=S_OK || result.QuadPart!=1234567ULL || (long long)result.QuadPart!=native_stream_seek()) return 5;
        if(stream->Release()!=0) return 6;
        return 0;
    }
};
int main() {
    int result=NativeComAggregates::Verify();
    if(result!=0) { Console::WriteLine(result); return 1; }
    Console::WriteLine("native COM small aggregates: integer/float bit ABI, register/stack args, union signed bits, single evaluation, copy isolation, GC and original IStream Seek PASS");
    return 0;
}
