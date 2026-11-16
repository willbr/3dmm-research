/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

/***************************************************************************
    Author: ShonK
    Project: Kauai
    Copyright (c) Microsoft Corporation

    String manipulation.
    WARNING: Must be in a fixed (pre-loaded) seg on Mac.

***************************************************************************/
#include <cstring>
#include "util.h"
#include "platform_text.h"
#include <cstdarg>
ASSERTNAME

#include "mm/text.h"
#include "mm/string_wire.h"
#include "mm/string_value.h"
#include "mm/string_format.h"
#include "mm/string_ops.h"

static void StringCopyCheck(void *, const void *source, void *destination, size_t bytes)
{
    AssertPvCb(const_cast<void *>(source), bytes);
    AssertPvCb(destination, bytes);
    Assert(static_cast<const byte *>(source) + bytes <= destination ||
           static_cast<byte *>(destination) + bytes <= source, "blocks overlap");
}
static MMStringValue StringValueView(achar *buffer)
{
    static_assert(sizeof(achar) == 1 || sizeof(achar) == 2, "unsupported string unit");
    MMStringValue value = {buffer, sizeof(achar), nullptr, StringCopyCheck};
    return value;
}

static void StringRangeCheck(void *, const void *buffer, size_t bytes)
{
    AssertPvCb(const_cast<void *>(buffer), bytes);
}
static void StringCountedCheck(void *, const void *buffer)
{
    AssertSt(static_cast<achar *>(const_cast<void *>(buffer)));
}
static void StringUpper(void *, void *buffer, size_t count)
{
#ifdef UNICODE
    KauaiText::UpperUtf16(static_cast<wchar *>(buffer), count);
#else
    KauaiText::UpperBytes(static_cast<char *>(buffer), count);
#endif
}
static int StringCompare(void *, const void *a, size_t na, const void *b, size_t nb, int ignore)
{
#ifdef UNICODE
    return KauaiText::CompareUtf16(static_cast<const wchar *>(a), na, static_cast<const wchar *>(b), nb, ignore != 0);
#else
    return KauaiText::CompareBytes(static_cast<const char *>(a), na, static_cast<const char *>(b), nb, ignore != 0);
#endif
}
static MMStringOps StringOperations()
{
    static_assert(fcmpEq == MM_STRING_EQUAL && fcmpLt == MM_STRING_LESS && fcmpGt == MM_STRING_GREATER,
                  "comparison flags must retain their legacy values");
    static_assert(fchWhiteOverhang == 1 && fchBreak == 2 && fchMayBreak == 4 && fchIgnore == 8 && fchControl == 16 && fchTab == 32,
                  "character flags must retain their legacy values");
    MMStringOps operations = {sizeof(achar), achar(-1) < 0, nullptr, StringRangeCheck, StringCopyCheck,
        StringCountedCheck, StringUpper, StringCompare};
    return operations;
}

const achar vrgchHex[] = PszLit("0123456789ABCDEF");

/***************************************************************************
    Constructor for a string based on another string.
***************************************************************************/
String::String(String &stnSrc)
{
    AssertPo(&stnSrc, 0);
    const auto value = StringValueView(_rgch);
    mm_string_value_copy(&value, stnSrc._rgch);
    AssertThis(0);
}

/***************************************************************************
    Constructor for a string based on an sz.
***************************************************************************/
String::String(PZString pszSrc)
{
    LegacyLong cch = LwBound(CchSz(pszSrc), 0, kcchMaxStn + 1);
    AssertIn(cch, 0, kcchMaxStn + 1);
    const auto value = StringValueView(_rgch);
    mm_string_value_set(&value, pszSrc, cch);
    AssertThis(0);
}

/***************************************************************************
    Assignment of one string to another.
***************************************************************************/
String &String::operator=(String &stnSrc)
{
    AssertThis(0);
    AssertPo(&stnSrc, 0);
    const auto value = StringValueView(_rgch);
    mm_string_value_copy(&value, stnSrc._rgch);
    AssertThis(0);
    return *this;
}

/***************************************************************************
    Set the string to the given array of characters.
***************************************************************************/
void String::SetRgch(achar *prgchSrc, LegacyLong cch)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgchSrc, cch * size(achar));
    const auto value = StringValueView(_rgch);
    mm_string_value_set(&value, prgchSrc, cch);
    AssertThis(0);
}

/***************************************************************************
    Put the zero terminated short character string into the String.
***************************************************************************/
void String::SetSzs(PSZS pszsSrc)
{
    AssertThis(0);
    AssertVarMem(pszsSrc);
#ifdef UNICODE
    const auto count = mm_string_zero_length(pszsSrc, 1);
#else
    const auto count = CchSz(pszsSrc);
    AssertIn(count, 0, kcbMax);
    AssertPvCb(pszsSrc, count * size(achar));
#endif
    const auto value = StringValueView(_rgch);
    mm_string_from_ansi(&value, KauaiText::Services(), pszsSrc, count);
#ifndef UNICODE
    AssertThis(0);
#endif
}

/***************************************************************************
    Delete (at most) cch characters starting at position ich.
***************************************************************************/
void String::Delete(LegacyLong ich, LegacyLong cch)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    if (ich < 0 || ich > Cch()) {
        Assert(false, "Bad character position to delete from");
        return;
    }
    const auto value = StringValueView(_rgch);
    mm_string_value_delete(&value, ich, cch);
    AssertThis(0);
}

/***************************************************************************
    Append some characters to the end of the string.
***************************************************************************/
bool String::FAppendRgch(achar *prgchSrc, LegacyLong cch)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgchSrc, cch * size(achar));
    const auto value = StringValueView(_rgch);
    const bool result = mm_string_value_append(&value, prgchSrc, cch) != 0;
    AssertThis(0);
    return result;
}

/***************************************************************************
    Insert some characters into the middle of a string.
***************************************************************************/
bool String::FInsertRgch(LegacyLong ich, achar *prgchSrc, LegacyLong cch)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgchSrc, cch * size(achar));
    const auto value = StringValueView(_rgch);
    const bool result = mm_string_value_insert(&value, ich, prgchSrc, cch) != 0;
    AssertThis(0);
    return result;
}

/***************************************************************************
    Test whether the given rgch is equal to this string.  This does bytewise
    compare - not user level comparison.
***************************************************************************/
bool String::FEqualRgch(achar *prgch, LegacyLong cch)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgch, cch * size(achar));
    const auto value = StringValueView(_rgch);
    return mm_string_value_equal(&value, prgch, cch) != 0;
}

/***************************************************************************
    Do user level string equality testing with the options given in grfstn.
***************************************************************************/
bool String::FEqualUserRgch(achar *prgch, LegacyLong cch, LegacyUlong grfstn)
{
    AssertThis(0);
    AssertIn(cch, 0, kcbMax);
    AssertPvCb(prgch, cch * size(achar));

    return ::FEqualUserRgch(Prgch(), Cch(), prgch, cch, grfstn);
}

/***************************************************************************
    Return the buffer size needed by GetData, or the block size needed
    by String::FWrite.
***************************************************************************/
namespace {
MMStringWire StringWireView(void *buffer)
{
    const short one = 1;
    return {buffer, sizeof(achar), koskCur, MacWin(koskUniWin, koskUniMac), reinterpret_cast<const byte *>(&one)[0] == 0, nullptr,
        [](void *, const void *input, MMU32 bytes, int encoding, void *output, MMU32 capacity) -> MMI32 {
            return CchTranslateRgb(const_cast<void *>(input), bytes, static_cast<short>(encoding), static_cast<achar *>(output), capacity);
        }};
}
int ReadStringBlock(void *context, MMU32 offset, void *bytes, size_t count)
{ return static_cast<PDataBlock>(context)->FReadRgb(bytes, count, offset); }
int WriteStringBlock(void *context, MMU32 offset, const void *bytes, size_t count)
{ return static_cast<PDataBlock>(context)->FWriteRgb(const_cast<void *>(bytes), count, offset); }
}
LegacyLong String::CbData(void)
{
    AssertThis(0); const auto text = StringWireView(_rgch);
    return mm_string_wire_size(&text);
}
void String::GetData(void *pv)
{
    AssertThis(0); AssertPvCb(pv, CbData()); const auto text = StringWireView(_rgch);
    AssertDo(mm_string_wire_get(&text, pv, CbData()), "invalid string serialization");
}
bool String::FWrite(PDataBlock pblck, LegacyLong ib)
{
    AssertThis(0); AssertPo(pblck, 0);
    const auto text = StringWireView(_rgch);
    const MMStringWriter writer = {pblck, static_cast<MMU32>(pblck->Cb()), WriteStringBlock};
    return mm_string_wire_write(&text, &writer, ib) != 0;
}
bool String::FSetData(void *pv, LegacyLong cbMax, LegacyLong *pcbRead)
{
    AssertThis(0); AssertIn(cbMax, 0, kcbMax); AssertPvCb(pv, cbMax); AssertNilOrVarMem(pcbRead);
    const auto text = StringWireView(_rgch); MMMemoryStream memory; MMU32 end;
    const bool ok = mm_memory_stream_init(&memory, pv, cbMax) &&
        mm_string_wire_read(&text, &memory.stream, 0, pcbRead ? &end : nullptr);
    if (ok) { if (pcbRead) *pcbRead = end; AssertThis(0); return fTrue; }
    PushErc(ercStnRead); Warn("bad String data"); _rgch[0] = _rgch[1] = 0; TrashVar(pcbRead); return fFalse;
}
bool String::FRead(PDataBlock pblck, LegacyLong ib, LegacyLong *pcbRead)
{
    AssertThis(0); AssertPo(pblck, 0); AssertNilOrVarMem(pcbRead);
    if (!pblck->FUnpackData()) return fFalse;
    const auto text = StringWireView(_rgch); MMU32 end;
    const MMReadStream reader = {pblck, static_cast<MMU32>(pblck->Cb()), ReadStringBlock};
    const bool ok = mm_string_wire_read(&text, &reader, ib, pcbRead ? &end : nullptr) != 0;
    if (ok) { if (pcbRead) *pcbRead = end; AssertThis(0); return fTrue; }
    PushErc(ercStnRead); Warn("bad String data or read failure"); TrashVar(pcbRead); return fFalse;
}

/***************************************************************************
    Get a zero terminated short string from this string.
***************************************************************************/
void String::GetSzs(PSZS pszs)
{
    AssertThis(0);
    AssertPvCb(pszs, kcchTotSz);
    const auto value = StringValueView(_rgch);
    mm_string_to_ansi(&value, KauaiText::Services(), pszs);
}

/***************************************************************************
    Format this string using the given template string and any additional
    parameters (ala sprintf), using typed native varargs extraction.

    Returns false if the string ended up being too long to fit in an stn.
    The following controls are supported:

    %c (long)achar
    %s pstn
    %z psz
    %d signed decimal (long)
    %u unsigned decimal (long)
    %x hex
    %f long as a 4 character value: 'xxxx' (ala FileType and ChunkTagOrType values)
    %% a percent sign

    Supports the following options, in this order:

        Left justify ('-')
        Explicit plus sign ('+')
        Zero padding instead of space padding ('0')
        Minimum field width (decimal number)

    These all go between the '%' and the control letter. The legacy positional
    notation ('<', argument index, '>') is currently rejected before fetching
    an argument, as in the original implementation being converted.

    Numeric arguments use the legacy 32-bit types; pointer arguments retain
    their native width. Positional %<N> syntax is currently rejected.
***************************************************************************/
bool String::FFormat(PString pstnFormat, ...)
{
    AssertThis(0);
    AssertPo(pstnFormat, 0);

    va_list va;
    va_start(va, pstnFormat);
    bool fRet = FFormatRgch(pstnFormat->Prgch(), pstnFormat->Cch(), va);
    va_end(va);
    return fRet;
}

/***************************************************************************
    See comments for String::FFormat
***************************************************************************/
bool String::FFormatSz(const achar *pszFormat, ...)
{
    auto *format = const_cast<achar *>(pszFormat); // formatting never mutates it
    AssertThis(0);
    AssertSz(format);
    va_list va;
    va_start(va, pszFormat);
    bool result = FFormatRgch(format, CchSz(format), va);
    va_end(va);
    return result;
}

bool String::FFormatSz(PZString pszFormat, ...)
{
    AssertThis(0);
    AssertSz(pszFormat);

    va_list va;
    va_start(va, pszFormat);
    bool fRet = FFormatRgch(pszFormat, CchSz(pszFormat), va);
    va_end(va);
    return fRet;
}

/***************************************************************************
    Core routine for sprintf functionality.
***************************************************************************/
struct NativeStringFormatArguments
{
    va_list values;
};
static int StringFormatNext(void *context, unsigned control, MMStringFormatArgument *argument)
{
    auto *source = static_cast<NativeStringFormatArguments *>(context);
    switch (control) {
    case 'c': argument->number = static_cast<MMU32>(va_arg(source->values, int)); break;
    case 'd': argument->number = static_cast<MMU32>(va_arg(source->values, LegacyLong)); break;
    case 'u': case 'x': case 'f': argument->number = va_arg(source->values, LegacyUlong); break;
    case 's': {
        auto *text = static_cast<PString>(va_arg(source->values, void *));
        AssertPo(text, 0);
        argument->text = text->Prgch();
        argument->count = text->Cch();
        break;
    }
    case 'z': {
        auto *text = static_cast<achar *>(va_arg(source->values, void *));
        AssertSz(text);
        argument->text = text;
        argument->count = CchSz(text);
        break;
    }
    }
    return 1;
}
static void StringFormatBug(void *)
{
    Bug("bad format string");
}
bool String::FFormatRgch(achar *prgchFormat, LegacyLong cchFormat, va_list va)
{
    AssertThis(0);
    AssertIn(cchFormat, 0, kcchMaxStn + 1);
    AssertPvCb(prgchFormat, cchFormat * size(achar));
    NativeStringFormatArguments native;
    va_copy(native.values, va);
    const MMStringFormatArguments arguments = {&native, StringFormatNext, StringFormatBug};
    const auto value = StringValueView(_rgch);
    const bool result = mm_string_format(&value, prgchFormat, cchFormat, &arguments) != 0;
    va_end(native.values);
    return result;
}

/***************************************************************************
    Parses the String as a number.  If lwBase is 0, automatically determines
    the base as one of 10, 8 or 16 (as in standard C) and allows leading
    spaces, '+' and '-' signs, and trailing spaces.  Doesn't deal with
    overflow.
***************************************************************************/
bool String::FGetLw(LegacyLong *plw, LegacyLong lwBase)
{
    AssertThis(0);
    AssertVarMem(plw);
    AssertIn(lwBase, 0, 36);
    Assert(lwBase != 1, "base can't be 1");
    const auto value = StringValueView(_rgch);
    MMI32 result;
    if (!mm_string_value_number(&value, lwBase, &result)) {
        *plw = 0;
        TrashVar(plw);
        return false;
    }
    *plw = result;
    return true;
}

/***************************************************************************
    Doubles any backslash characters in the string and replaces " literals
    with \".
***************************************************************************/
bool String::FExpandControls(void)
{
    AssertThis(0);
    const auto value = StringValueView(_rgch);
    const bool result = mm_string_value_expand(&value) != 0;
    AssertThis(0);
    return result;
}

#ifdef DEBUG
/***************************************************************************
    Assert the validity of a String.
***************************************************************************/
void String::AssertValid(LegacyUlong grf)
{
    AssertThisMem();

    LegacyLong cch = (LegacyLong)(uchar)_rgch[0];

    AssertIn(cch, 0, kcchMaxStn + 1);
    Assert(_rgch[cch + 1] == 0, "missing termination byte");

    Assert(mm_string_zero_length(_rgch + 1, sizeof(achar)) == static_cast<size_t>(cch), "internal null characters");
}
#endif // DEBUG

/***************************************************************************
    Check the validity of an st (make sure no zeros are in it).
***************************************************************************/
bool FValidSt(PPascalString pst)
{
    AssertVarMem(pst);
    const auto operations = StringOperations();
    return mm_string_counted_valid(&operations, pst, 0) != 0;
}

/***************************************************************************
    Check the validity of an stz
***************************************************************************/
bool FValidStz(PSTZ pstz)
{
    AssertVarMem(pstz);
    const auto operations = StringOperations();
    return mm_string_counted_valid(&operations, pstz, 1) != 0;
}

/***************************************************************************
    Find the length of a zero terminated string.
***************************************************************************/
LegacyLong CchSz(PZString psz)
{
    // Do not call AssertSz: it delegates here.
    AssertVarMem(psz);
    const auto count = mm_string_zero_length(psz, sizeof(achar));
    Assert(count <= kcchMaxSz, "sz too long");
    AssertPvCb(psz, (count + 1) * size(achar));
    return static_cast<LegacyLong>(count);
}

/***************************************************************************
    Do string equality testing.  This does byte-wise comparison for
    internal (non-user) use only!
***************************************************************************/
bool FEqualRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2)
{
    AssertIn(cch1, 0, kcbMax);
    AssertPvCb(prgch1, cch1 * size(achar));
    AssertIn(cch2, 0, kcbMax);
    AssertPvCb(prgch2, cch2 * size(achar));
    const auto operations = StringOperations();
    return mm_string_raw_equal(&operations, prgch1, cch1, prgch2, cch2) != 0;
}

/***************************************************************************
    Do string comparison for sorting.  This is byte-wise for internal
    (non-user) sorting only!  The sorting is byte-order independent.
    fcmpLt means that string 1 is less than string 2.
***************************************************************************/
LegacyUlong FcmpCompareRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2)
{
    AssertIn(cch1, 0, kcbMax);
    AssertPvCb(prgch1, cch1 * size(achar));
    AssertIn(cch2, 0, kcbMax);
    AssertPvCb(prgch2, cch2 * size(achar));
    const auto operations = StringOperations();
    return mm_string_internal_compare(&operations, prgch1, cch1, prgch2, cch2);
}

/***************************************************************************
    User level equality testing of strings.
***************************************************************************/
bool FEqualUserRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2, LegacyUlong grfstn)
{
    AssertIn(cch1, 0, kcbMax);
    AssertPvCb(prgch1, cch1 * size(achar));
    AssertIn(cch2, 0, kcbMax);
    AssertPvCb(prgch2, cch2 * size(achar));
    const auto operations = StringOperations();
    return mm_string_user_equal(&operations, prgch1, cch1, prgch2, cch2, (grfstn & fstnIgnoreCase) != 0) != 0;
}

/***************************************************************************
    Do user level string comparison with the given options.
***************************************************************************/
LegacyUlong FcmpCompareUserRgch(achar *prgch1, LegacyLong cch1, achar *prgch2, LegacyLong cch2, LegacyUlong grfstn)
{
    AssertIn(cch1, 0, kcbMax);
    AssertPvCb(prgch1, cch1 * size(achar));
    AssertIn(cch2, 0, kcbMax);
    AssertPvCb(prgch2, cch2 * size(achar));
    const auto operations = StringOperations();
    return mm_string_user_compare(&operations, prgch1, cch1, prgch2, cch2, (grfstn & fstnIgnoreCase) != 0);
}

/***************************************************************************
    Map an array of short characters to upper case equivalents.
***************************************************************************/
void UpperRgchs(schar *prgchs, LegacyLong cchs)
{
    AssertIn(cchs, 0, kcbMax);
    AssertPvCb(prgchs, cchs * size(schar));
    KauaiText::UpperMappedBytes(prgchs, cchs);
}

/***************************************************************************
    Map an array of characters to lower case equivalents.
***************************************************************************/
void LowerRgchs(schar *prgchs, LegacyLong cchs)
{
    AssertIn(cchs, 0, kcbMax);
    AssertPvCb(prgchs, cchs * size(schar));
    KauaiText::LowerMappedBytes(prgchs, cchs);
}

/***************************************************************************
    Map an array of unicode characters to upper case equivalents.
***************************************************************************/
void UpperRgchw(wchar *prgchw, LegacyLong cchw)
{
    AssertIn(cchw, 0, kcbMax);
    AssertPvCb(prgchw, cchw * size(wchar));
    KauaiText::UpperUtf16(prgchw, cchw);
}

/***************************************************************************
    Map an array of unicode characters to lower case equivalents.
***************************************************************************/
void LowerRgchw(wchar *prgchw, LegacyLong cchw)
{
    AssertIn(cchw, 0, kcbMax);
    AssertPvCb(prgchw, cchw * size(wchar));
    KauaiText::LowerUtf16(prgchw, cchw);
}

/***************************************************************************
    Translate text from the indicated source character set to koskCur.
***************************************************************************/
LegacyLong CchTranslateRgb(void *pvSrc, LegacyLong cbSrc, short oskSrc, achar *prgchDst, LegacyLong cchMaxDst)
{
    AssertPvCb(pvSrc, cbSrc);
    AssertOsk(oskSrc);
    AssertPvCb(prgchDst, cchMaxDst * size(achar));
    if (cbSrc < 0 || cchMaxDst < 0)
        return 0;
    auto encoding = static_cast<KauaiText::Encoding>(oskSrc);
#ifdef UNICODE
    return static_cast<LegacyLong>(KauaiText::ToUtf16(pvSrc, cbSrc, encoding, cchMaxDst ? prgchDst : pvNil, cchMaxDst));
#else
    return static_cast<LegacyLong>(KauaiText::ToAnsi(pvSrc, cbSrc, encoding, cchMaxDst ? prgchDst : pvNil, cchMaxDst));
#endif
}

/***************************************************************************
    Translates a string between the current platform and another.  If fToCur
    is true, the translation is from osk to koskCur, otherwise from koskCur
    to osk.
***************************************************************************/
void TranslateRgch(achar *prgch, LegacyLong cch, short osk, bool fToCur)
{
    AssertPvCb(prgch, cch);
    AssertOsk(osk);
    Assert(osk == koskMac || osk == koskWin, "TranslateRgch can't handle this osk");
    mm_string_translate(prgch, cch, sizeof(achar), osk, koskCur, fToCur);
}

/***************************************************************************
    Return the type of the character.
    This must follow these implications:
        fchBreak -> fchMayBreak
        fchBreak -> fchControl
        fchIgnore -> fchControl

    REVIEW shonk: make GrfchFromCh handle all unicode characters.
***************************************************************************/
LegacyUlong GrfchFromCh(achar ch)
{
    return mm_string_character_flags(static_cast<uchar>(ch));
}

#ifdef DEBUG
/***************************************************************************
    Validate an osk.
***************************************************************************/
void AssertOsk(short osk)
{
    switch (osk)
    {
    case koskSbMac:
    case koskSbWin:
    case koskUniMac:
    case koskUniWin:
        break;
    default:
        BugVar("bad osk", &osk);
    }
}

/***************************************************************************
    Check the validity of an st (make sure no zeros are in it).
***************************************************************************/
void AssertSt(PPascalString pst)
{
    Assert(FValidSt(pst), "bad st");
}

/***************************************************************************
    Check the validity of an stz.
***************************************************************************/
void AssertStz(PSTZ pstz)
{
    Assert(FValidStz(pstz), "bad stz");
}

/***************************************************************************
    Make sure the sz isn't too long.
***************************************************************************/
void AssertSz(PZString psz)
{
    // CchSz does all the asserting we need
    LegacyLong cch = CchSz(psz);
}

/***************************************************************************
    Check the validity of an st, nil is allowed.
***************************************************************************/
void AssertNilOrSt(PPascalString pst)
{
    if (pst != pvNil)
        AssertSt(pst);
}

/***************************************************************************
    Check the validity of an stz, nil is allowed.
***************************************************************************/
void AssertNilOrStz(PSTZ pstz)
{
    if (pstz != pvNil)
        AssertStz(pstz);
}

/***************************************************************************
    Check the validity of an sz, nil is allowed.
***************************************************************************/
void AssertNilOrSz(PZString psz)
{
    if (psz != pvNil)
        AssertSz(psz);
}
#endif // DEBUG
