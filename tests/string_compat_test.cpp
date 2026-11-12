/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
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
static void CheckTableEntry(PVirtualStringTable table, LegacyLong index, const char *label, LegacyLong expected)
{
    String text;
    LegacyLong extra;
    table->GetStn(index, &text);
    table->GetExtra(index, &extra);
    Check(text.FEqualSz(const_cast<char *>(label)) && extra == expected);
}
static void CheckStringStorage()
{
    auto file = ChunkyFile::PcflCreateTemp();
    Check(file != pvNil);
    auto dense = StringTable_GST::PgstNew(4);
    auto sparse = AllocatedStringTable::PastNew(4);
    Check(dense && sparse);
    LegacyLong one = 1, two = 2, three = 3, position;
    Check(dense->FAddRgch(PszLit("cat"), 3, &one));
    Check(dense->FInsertRgch(0, PszLit("A"), 1, &two));
    Check(dense->FInsertRgch(1, PszLit(""), 0, &three));
    dense->Move(0, 3);
    CheckTableEntry(dense, 2, "A", two);
    dense->Move(2, 0);
    Check(dense->FPutRgch(2, PszLit("longer"), 6));
    dense->Delete(1);
    CheckTableEntry(dense, 0, "A", two);
    CheckTableEntry(dense, 1, "longer", one);
    auto duplicate = dense->PgstDup();
    Check(duplicate != pvNil && dense->FPutRgch(0, PszLit("B"), 1));
    CheckTableEntry(duplicate, 0, "A", two);
    ReleasePpo(&duplicate);
    Check(dense->FFindRgch(PszLit("longer"), 6, &position) && position == 1);
    Check(!dense->FFindRgch(PszLit("missing"), 7, &position) && position == 2);
    Check(!dense->FFindRgch(PszLit(""), 0, &position, fgstSorted) && position == 0);
    Check(dense->FAddRgch(PszLit("Caf\351"), 4, &three));
    Check(sparse->FAddRgch(PszLit("one"), 3, &one));
    Check(sparse->FAddRgch(PszLit("two"), 3, &two));
    Check(sparse->FAddRgch(PszLit("three"), 5, &three));
    sparse->Delete(0);
    Check(sparse->FFree(0));
    Check(sparse->FAddRgch(PszLit("again"), 5, &one, &position) && position == 0);
    CheckTableEntry(sparse, 0, "again", one);
    sparse->Delete(1);
    // Preserve holes, entry extras and source storage through both endian writes.
    for (auto order : {kboCur, kboOther})
    {
        for (auto encoding : {koskSbWin, koskSbMac, koskUniWin, koskUniMac})
        {
            for (int allocated = 0; allocated < 2; ++allocated)
            {
                PVirtualStringTable original = allocated ? static_cast<PVirtualStringTable>(sparse) : dense;
                DataBlock block;
                ChunkNumber number;
                Check(file->FAdd(original->CbOnFile(), 'GST ', &number, &block));
                Check(original->FWrite(&block, order, encoding));
                PVirtualStringTable copy = allocated ? static_cast<PVirtualStringTable>(AllocatedStringTable::PastRead(&block)) :
                                                       StringTable_GST::PgstRead(&block);
                Check(copy != pvNil && copy->IstnMac() == original->IstnMac());
                CheckTableEntry(copy, 0, allocated ? "again" : "B", allocated ? one : two);
                if (allocated) { Check(copy->FFree(1)); CheckTableEntry(copy, 2, "three", three); }
                else
                {
                    CheckTableEntry(copy, 1, "longer", one);
                    CheckTableEntry(copy, 2, "Caf\351", three);
                    CheckTableEntry(original, 2, "Caf\351", three);
                }
                ReleasePpo(&copy);
            }
        }
    }
    sparse->Delete(2);
    Check(sparse->IstnMac() == 1);
    sparse->Delete(0);
    Check(sparse->IstnMac() == 0);
    for (auto order : {kboCur, kboOther})
    {
        DataBlock empty;
        Check(empty.FSetTemp(sparse->CbOnFile()) && sparse->FWrite(&empty, order));
        auto copy = AllocatedStringTable::PastRead(&empty);
        Check(copy && copy->IstnMac() == 0);
        ReleasePpo(&copy);
    }
    // Independently encoded UTF-16BE table; metadata is opaque to transcoding.
    byte wide[] = {0,1,4,4, 0,0,0,8, 0,0,0,1, 0,0,0,6, 255,255,255,255,
                   0,2,0,'A',0,'Z', 0,0,0,0, 1,2,3,4};
    ChunkNumber number;
    Check(file->FAddPv(wide, sizeof(wide), 'GST ', &number));
    DataBlock block;
    Check(file->FFind('GST ', number, &block));
    auto decoded = StringTable_GST::PgstRead(&block);
    Check(decoded != pvNil);
    String label;
    decoded->GetStn(0, &label);
    Check(label.FEqualSz(PszLit("AZ")));
    byte extra[4];
    decoded->GetExtra(0, extra);
    Check(extra[0] == 1 && extra[3] == 4);
    ReleasePpo(&decoded);
    ReleasePpo(&dense);
    ReleasePpo(&sparse);
    ReleasePpo(&file);
}
int main()
{
    CheckStringStorage();
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
