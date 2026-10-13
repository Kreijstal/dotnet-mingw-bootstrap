// SPDX-License-Identifier: MIT
// Unicode properties for the source-port of WPF's managed Classification API.
// This does not pretend to provide the unavailable legacy MIL table contract.
#include <windows.h>
#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <stdexcept>
#include <vector>
#include "ClassificationEnums.h"

#pragma pack(push,1)
struct CharacterAttribute {
    uint8_t script,item;
    uint16_t flags;
    uint8_t breaking,bidi;
    int16_t line_break;
};
#pragma pack(pop)
static_assert(sizeof(CharacterAttribute)==8,"WPF CharacterAttribute ABI");

struct ClassificationTableView {
    int16_t*** classes;
    CharacterAttribute* attributes;
    int count;
};

static uint8_t script_id(UScriptCode script) {
    switch(script) {
#define SCRIPT(icu,wpf) case USCRIPT_##icu: return WPF_ScriptID_##wpf
        SCRIPT(ARABIC,Arabic); SCRIPT(ARMENIAN,Armenian); SCRIPT(BENGALI,Bengali);
        SCRIPT(BOPOMOFO,Bopomofo); SCRIPT(BRAILLE,Braille); SCRIPT(BUGINESE,Buginese);
        SCRIPT(BUHID,Buhid); SCRIPT(CANADIAN_ABORIGINAL,CanadianSyllabics);
        SCRIPT(CHEROKEE,Cherokee); SCRIPT(HAN,CJKIdeographic); SCRIPT(COPTIC,Coptic);
        SCRIPT(CYPRIOT,CypriotSyllabary); SCRIPT(CYRILLIC,Cyrillic); SCRIPT(DESERET,Deseret);
        SCRIPT(DEVANAGARI,Devanagari); SCRIPT(ETHIOPIC,Ethiopic); SCRIPT(GEORGIAN,Georgian);
        SCRIPT(GLAGOLITIC,Glagolitic); SCRIPT(GOTHIC,Gothic); SCRIPT(GREEK,Greek);
        SCRIPT(GUJARATI,Gujarati); SCRIPT(GURMUKHI,Gurmukhi); SCRIPT(HANGUL,Hangul);
        SCRIPT(HANUNOO,Hanunoo); SCRIPT(HEBREW,Hebrew); SCRIPT(KANNADA,Kannada);
        SCRIPT(HIRAGANA,Kana); SCRIPT(KATAKANA,Kana); SCRIPT(KATAKANA_OR_HIRAGANA,Kana);
        SCRIPT(KHAROSHTHI,Kharoshthi); SCRIPT(KHMER,Khmer); SCRIPT(LAO,Lao);
        SCRIPT(LATIN,Latin); SCRIPT(LIMBU,Limbu); SCRIPT(LINEAR_B,LinearB);
        SCRIPT(MALAYALAM,Malayalam); SCRIPT(MONGOLIAN,Mongolian); SCRIPT(MYANMAR,Myanmar);
        SCRIPT(NEW_TAI_LUE,NewTaiLue); SCRIPT(OGHAM,Ogham); SCRIPT(OLD_ITALIC,OldItalic);
        SCRIPT(OLD_PERSIAN,OldPersianCuneiform); SCRIPT(ORIYA,Oriya); SCRIPT(OSMANYA,Osmanya);
        SCRIPT(RUNIC,Runic); SCRIPT(SHAVIAN,Shavian); SCRIPT(SINHALA,Sinhala);
        SCRIPT(SYLOTI_NAGRI,SylotiNagri); SCRIPT(SYRIAC,Syriac); SCRIPT(TAGALOG,Tagalog);
        SCRIPT(TAGBANWA,Tagbanwa); SCRIPT(TAI_LE,TaiLe); SCRIPT(TAMIL,Tamil);
        SCRIPT(TELUGU,Telugu); SCRIPT(THAANA,Thaana); SCRIPT(THAI,Thai);
        SCRIPT(TIBETAN,Tibetan); SCRIPT(TIFINAGH,Tifinagh); SCRIPT(UGARITIC,UgariticCuneiform);
        SCRIPT(YI,Yi);
#undef SCRIPT
        default: return WPF_ScriptID_Default;
    }
}

static uint8_t direction(UCharDirection bidi) {
    switch(bidi) {
#define BIDI(icu,wpf) case U_##icu: return WPF_DirectionClass_##wpf
        BIDI(LEFT_TO_RIGHT,Left); BIDI(RIGHT_TO_LEFT,Right);
        BIDI(EUROPEAN_NUMBER,EuropeanNumber); BIDI(EUROPEAN_NUMBER_SEPARATOR,EuropeanSeparator);
        BIDI(EUROPEAN_NUMBER_TERMINATOR,EuropeanTerminator); BIDI(ARABIC_NUMBER,ArabicNumber);
        BIDI(COMMON_NUMBER_SEPARATOR,CommonSeparator); BIDI(BLOCK_SEPARATOR,ParagraphSeparator);
        BIDI(SEGMENT_SEPARATOR,SegmentSeparator); BIDI(WHITE_SPACE_NEUTRAL,WhiteSpace);
        BIDI(OTHER_NEUTRAL,OtherNeutral); BIDI(LEFT_TO_RIGHT_EMBEDDING,LeftToRightEmbedding);
        BIDI(LEFT_TO_RIGHT_OVERRIDE,LeftToRightOverride); BIDI(RIGHT_TO_LEFT_ARABIC,ArabicLetter);
        BIDI(RIGHT_TO_LEFT_EMBEDDING,RightToLeftEmbedding); BIDI(RIGHT_TO_LEFT_OVERRIDE,RightToLeftOverride);
        BIDI(POP_DIRECTIONAL_FORMAT,PopDirectionalFormat); BIDI(DIR_NON_SPACING_MARK,NonSpacingMark);
        BIDI(BOUNDARY_NEUTRAL,BoundaryNeutral);
#undef BIDI
        // WPF's original managed bidi engine predates isolate classes. Keep
        // their formatting controls out of the fast-text route; full isolate
        // processing requires a separate bidi-engine port.
        default: return WPF_DirectionClass_BoundaryNeutral;
    }
}

static bool simple_script(UScriptCode script) {
    switch(script) {
        case USCRIPT_COMMON: case USCRIPT_LATIN: case USCRIPT_GREEK:
        case USCRIPT_CYRILLIC: case USCRIPT_ARMENIAN: case USCRIPT_GEORGIAN:
        case USCRIPT_COPTIC: case USCRIPT_CHEROKEE: case USCRIPT_DESERET:
        case USCRIPT_CANADIAN_ABORIGINAL: case USCRIPT_ETHIOPIC:
        case USCRIPT_BRAILLE: case USCRIPT_OGHAM: case USCRIPT_RUNIC:
        case USCRIPT_SHAVIAN: case USCRIPT_OLD_ITALIC: case USCRIPT_OLD_PERSIAN:
        case USCRIPT_UGARITIC: return true;
        default: return false;
    }
}

static CharacterAttribute classify(UChar32 codepoint) {
    UErrorCode error=U_ZERO_ERROR;
    auto script=uscript_getScript(codepoint,&error);
    if(U_FAILURE(error)) throw std::runtime_error("ICU script lookup failed");
    auto category=u_charType(codepoint);
    auto bidi=u_charDirection(codepoint);
    auto line=u_getIntPropertyValue(codepoint,UCHAR_LINE_BREAK);
    bool mark=category==U_NON_SPACING_MARK || category==U_COMBINING_SPACING_MARK || category==U_ENCLOSING_MARK;
    bool control=category==U_CONTROL_CHAR || category==U_FORMAT_CHAR || category==U_SURROGATE;
    bool rtl=uscript_isRightToLeft(script) || bidi==U_RIGHT_TO_LEFT || bidi==U_RIGHT_TO_LEFT_ARABIC || bidi==U_ARABIC_NUMBER;
    bool ideographic=u_hasBinaryProperty(codepoint,UCHAR_IDEOGRAPHIC);
    bool nominal_script=simple_script(script) || ideographic || script==USCRIPT_HIRAGANA ||
        script==USCRIPT_KATAKANA || script==USCRIPT_BOPOMOFO ||
        (script==USCRIPT_HANGUL && codepoint>=0xAC00 && codepoint<=0xD7A3);
    bool complex=!nominal_script || mark || control || rtl;
    bool digit=category==U_DECIMAL_DIGIT_NUMBER;
    CharacterAttribute result{};
    result.script=script_id(script);
    if(codepoint>=0x1D400 && codepoint<=0x1D7FF) result.script=WPF_ScriptID_MathematicalAlphanumericSymbols;
    if(codepoint>=0x1D100 && codepoint<=0x1D1FF) result.script=WPF_ScriptID_MusicalSymbols;
    if(digit) result.script=WPF_ScriptID_Digit;
    if(control) result.script=WPF_ScriptID_Control;
    result.item=WPF_ItemClass_WeakClass;
    if(u_isalpha(codepoint)) result.item=WPF_ItemClass_StrongClass;
    if(digit) result.item=bidi==U_ARABIC_NUMBER ? WPF_ItemClass_ANClass : WPF_ItemClass_DigitClass;
    if(bidi==U_COMMON_NUMBER_SEPARATOR) result.item=WPF_ItemClass_CSClass;
    if(bidi==U_EUROPEAN_NUMBER_SEPARATOR) result.item=WPF_ItemClass_ESClass;
    if(bidi==U_EUROPEAN_NUMBER_TERMINATOR) result.item=WPF_ItemClass_ETClass;
    if(mark) result.item=simple_script(script) || script==USCRIPT_INHERITED ? WPF_ItemClass_SimpleMarkClass : WPF_ItemClass_ComplexMarkClass;
    if(control) result.item=WPF_ItemClass_ControlClass;
    if(codepoint==0x200C || codepoint==0x200D) result.item=WPF_ItemClass_JoinerClass;
    if(complex) result.flags|=WPF_CharacterAttributeFlags_CharacterComplex;
    if(rtl) result.flags|=WPF_CharacterAttributeFlags_CharacterRTL;
    if(!complex && !ideographic && category!=U_UNASSIGNED && category!=U_PRIVATE_USE_CHAR) result.flags|=WPF_CharacterAttributeFlags_CharacterFastText;
    if(ideographic) result.flags|=WPF_CharacterAttributeFlags_CharacterIdeo;
    if(codepoint>0xFFFF) result.flags|=WPF_CharacterAttributeFlags_CharacterExtended;
    if(u_isUWhiteSpace(codepoint)) result.flags|=WPF_CharacterAttributeFlags_CharacterSpace;
    if(digit) result.flags|=WPF_CharacterAttributeFlags_CharacterDigit;
    if(u_isalpha(codepoint)) result.flags|=WPF_CharacterAttributeFlags_CharacterLetter;
    if(category==U_FORMAT_CHAR && codepoint!=0x200C && codepoint!=0x200D)
        result.flags|=WPF_CharacterAttributeFlags_CharacterFormatAnchor;
    if(line==U_LB_MANDATORY_BREAK || line==U_LB_CARRIAGE_RETURN || line==U_LB_LINE_FEED || line==U_LB_NEXT_LINE)
        result.flags|=WPF_CharacterAttributeFlags_CharacterLineBreak;
    if(codepoint==0x2029) result.flags|=WPF_CharacterAttributeFlags_CharacterParaBreak;
    if(codepoint==0x0D || codepoint==0x0A) result.flags|=WPF_CharacterAttributeFlags_CharacterCRLF;
    result.breaking=control ? WPF_CharBreakingType_ControlBreak : digit ? WPF_CharBreakingType_DigitBreak : WPF_CharBreakingType_NoBreak;
    if(u_isMirrored(codepoint)) result.breaking=u_getBidiPairedBracket(codepoint)!=codepoint ? WPF_CharBreakingType_PairMirrorBreak : WPF_CharBreakingType_SingleMirrorBreak;
    result.bidi=direction(bidi);
    result.line_break=static_cast<int16_t>(line);
    return result;
}

struct Tables {
    std::vector<int16_t> scalar_classes;
    std::vector<CharacterAttribute> attributes;
    std::array<std::array<int16_t*,256>,17> blocks{};
    std::array<int16_t**,17> planes{};
    Tables():scalar_classes(0x110000) {
        std::map<std::array<uint8_t,8>,int16_t> ids;
        for(int codepoint=0;codepoint<0x110000;++codepoint) {
            auto value=classify(codepoint);
            std::array<uint8_t,8> key{}; std::memcpy(key.data(),&value,8);
            auto found=ids.find(key);
            if(found==ids.end()) {
                if(attributes.size()>=32767) throw std::runtime_error("too many WPF character classes");
                auto id=static_cast<int16_t>(attributes.size());
                attributes.push_back(value); found=ids.emplace(key,id).first;
            }
            scalar_classes[codepoint]=found->second;
        }
        for(int plane=0;plane<17;++plane) {
            for(int block=0;block<256;++block) blocks[plane][block]=scalar_classes.data()+plane*65536+block*256;
            planes[plane]=blocks[plane].data();
        }
    }
};

extern "C" __declspec(dllexport) HRESULT WINAPI WpfGetClassificationTables(ClassificationTableView* output) {
    if(!output) return E_POINTER;
    *output={};
    try {
        static Tables tables;
        *output={tables.planes.data(),tables.attributes.data(),static_cast<int>(tables.attributes.size())};
        return S_OK;
    } catch(const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch(...) { return E_FAIL; }
}
