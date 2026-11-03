/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error String data readers must compile without Windows SDK headers
#endif
ASSERTNAME
void WarnProc(schar *file, LegacyLong line, schar *message)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message);
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}
static void Check(bool value)
{
    if (!value)
        std::exit(1);
}
int main()
{
    const byte windows[] = {3, 3, 4, 'C', 'a', 'f', 0xe9, 0};
    const byte mac[] = {2, 2, 4, 'C', 'a', 'f', 0x8e, 0};
    const byte bigEndian[] = {4, 4, 0, 4, 0, 'T', 0, 'e', 0, 's', 0, 't', 0, 0};
    String value;
    byte stored[512];
    Check(value.FSetData(const_cast<byte *>(windows), sizeof(windows)));
    Check(value.CbData() == sizeof(windows));
    value.GetData(stored);
    Check(!std::memcmp(stored, windows, sizeof(windows)));
    Check(value.FSetData(const_cast<byte *>(mac), sizeof(mac)));
    value.GetData(stored);
    Check(!std::memcmp(stored, windows, sizeof(windows)));
    Check(value.FSetData(const_cast<byte *>(bigEndian), sizeof(bigEndian)) && value.FEqualSz(PszLit("Test")));
    Check(bigEndian[4] == 0 && bigEndian[5] == 'T');
    byte invalid[] = {3, 3, 4, 'C', 'a', 'f', 0xe9, 1};
    Check(!value.FSetData(invalid, sizeof(invalid)) && value.Cch() == 0);
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    Check(file != pvNil);
    ChunkNumber number;
    Check(file->FAddPv(invalid, sizeof(invalid), 'STR ', &number));
    DataBlock block;
    Check(file->FFind('STR ', number, &block));
    Check(!value.FRead(&block, 0, pvNil) && value.Cch() == 0);
    ReleasePpo(&file);
    wchar wide[] = {'a', 0xe9};
    UpperRgchw(wide, 2);
    Check(wide[0] == 'A' && wide[1] == 0xc9);
    LowerRgchw(wide, 2);
    Check(wide[0] == 'a' && wide[1] == 0xe9);
    schar bytes[128];
    for (unsigned i = 0; i < 128; ++i)
        bytes[i] = static_cast<schar>(i + 128);
    TranslateRgch(bytes, 128, koskSbMac, false);
    uint32_t hash = 2166136261u;
    for (auto ch : bytes)
        hash = (hash ^ static_cast<byte>(ch)) * 16777619u;
    Check(hash == 0x3e631f88u);
    // Pin the existing 32-bit parsing behavior, including wraparound and the
    // historical acceptance of a digit equal to an explicit radix.
    struct NumberCase {const char *text; LegacyLong radix, expected;};
    const NumberCase numbers[] = {
        {"-2147483648",0,(-2147483647L-1)}, {"2147483648",0,(-2147483647L-1)},
        {"4294967295",0,-1}, {"-4294967296",0,0}, {"8589934593",0,1},
        {"-2147483649",0,2147483647}, {"0xffffffff",0,-1},
        {"-0x80000000",0,(-2147483647L-1)}, {"--123",0,123}, {"19",9,18}
    };
    for(const auto &test:numbers) {
        value.SetSz(const_cast<char*>(test.text));
        LegacyLong number;
        Check(value.FGetLw(&number,test.radix) && number==test.expected);
    }
    Check(value.FFormatSz(PszLit("%d"),LegacyLong(-2147483647L-1)) && value.FEqualSz(PszLit("-2147483648")));
    Check(value.FFormatSz(PszLit("%d"),LegacyLong(2147483647L)) && value.FEqualSz(PszLit("2147483647")));
    Check(value.FFormatSz(PszLit("%d"),LegacyLong(-1)) && value.FEqualSz(PszLit("-1")));
    Check(value.FFormatSz(PszLit("%u"),LegacyUlong(0xFFFFFFFFu)) && value.FEqualSz(PszLit("4294967295")));
    return 0;
}
