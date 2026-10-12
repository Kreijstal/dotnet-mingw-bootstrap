// SPDX-License-Identifier: MIT
// Original node definition and range methods are extracted byte-for-byte.
#include <windows.h>
#include <dwrite.h>
#include <vcclr.h>
#define __deref_inout_ecount(x)
#define __inout_ecount(x)
using namespace System;
using namespace System::Collections::Generic;
namespace MS { namespace Internal { namespace Text { namespace TextInterface {
#include "RangeNodeOriginal.inc"
ref class TextItemizer {
    DWriteTextAnalysisNode<DWRITE_SCRIPT_ANALYSIS>* _pScriptAnalysisListHead;
    DWriteTextAnalysisNode<IDWriteNumberSubstitution*>* _pNumberSubstitutionListHead;
    List<bool>^ _isDigitList;
    List<array<UINT32>^>^ _isDigitListRanges;
public:
    TextItemizer(DWriteTextAnalysisNode<DWRITE_SCRIPT_ANALYSIS>*,DWriteTextAnalysisNode<IDWriteNumberSubstitution*>*);
    UINT32 GetNextSmallestPos(DWriteTextAnalysisNode<DWRITE_SCRIPT_ANALYSIS>**,UINT32&,
                             DWriteTextAnalysisNode<IDWriteNumberSubstitution*>**,UINT32&,UINT32&,UINT32&);
    void SetIsDigit(UINT32 textPosition,UINT32 textLength,bool isDigit);
};
#include "RangeMethodsOriginal.inc"
public ref class WpfRangeNodes abstract sealed {
public:
    static array<UInt32>^ Verify() {
        DWriteTextAnalysisNode<DWRITE_SCRIPT_ANALYSIS>* scripts=new DWriteTextAnalysisNode<DWRITE_SCRIPT_ANALYSIS>[2]();
        DWriteTextAnalysisNode<IDWriteNumberSubstitution*>* numbers=new DWriteTextAnalysisNode<IDWriteNumberSubstitution*>();
        try {
            scripts[0].Range[0]=0; scripts[0].Range[1]=4; scripts[0].Next=&scripts[1];
            scripts[1].Range[0]=4; scripts[1].Range[1]=10;
            numbers->Range[0]=0; numbers->Range[1]=10;
            TextItemizer^ itemizer=gcnew TextItemizer(scripts,numbers);
            itemizer->SetIsDigit(2,3,true); itemizer->SetIsDigit(8,1,false);
            DWriteTextAnalysisNode<DWRITE_SCRIPT_ANALYSIS>* scriptCurrent=scripts;
            DWriteTextAnalysisNode<IDWriteNumberSubstitution*>* numberCurrent=numbers;
            UINT32 scriptIndex=0,numberIndex=0,digitIndex=0,digitRangeIndex=0;
            array<UInt32>^ positions=gcnew array<UInt32>(11);
            for(int i=0;i<11;++i) {
                GC::Collect();
                positions[i]=itemizer->GetNextSmallestPos(&scriptCurrent,scriptIndex,&numberCurrent,numberIndex,digitIndex,digitRangeIndex);
            }
            if(scriptCurrent!=nullptr || numberCurrent!=nullptr || digitIndex!=2 || digitRangeIndex!=0)
                throw gcnew InvalidOperationException("original range node progression failed");
            TextItemizer^ empty=gcnew TextItemizer(nullptr,nullptr);
            scriptCurrent=nullptr; numberCurrent=nullptr;
            scriptIndex=0; numberIndex=0; digitIndex=0; digitRangeIndex=0;
            if(empty->GetNextSmallestPos(&scriptCurrent,scriptIndex,&numberCurrent,numberIndex,digitIndex,digitRangeIndex)!=UInt32::MaxValue)
                throw gcnew InvalidOperationException("original empty range sentinel failed");
            return positions;
        } finally { delete numbers; delete[] scripts; }
    }
};
}}}}
