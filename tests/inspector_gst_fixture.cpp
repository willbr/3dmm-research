/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "kauai_core.h"
#include <cstdio>
#include <cstdlib>
ASSERTNAME
void WarnProc(char *file, LegacyLong line, char *message)
{
    std::fprintf(stderr, "%s:%" LEGACY_PRId ": %s\n", file, line, message);
}
bool FAssertProc(char *file, LegacyLong line, char *message, void *, LegacyLong)
{
    WarnProc(file, line, message);
    std::exit(1);
}
static void Verify(bool value, int line)
{
    if (!value)
    {
        std::fprintf(stderr, "GST fixture check failed at line %d\n", line);
        std::exit(1);
    }
}
#define Check(value) Verify((value), __LINE__)
int main(int argc, char **argv)
{
    Check(argc == 2);
    String path;
    path.SetSzs(argv[1]);
    Filename filename;
    Check(filename.FBuildFromPath(&path));
    auto *file = ChunkyFile::PcflCreate(&filename, fcflWriteEnable);
    Check(file != pvNil);
    for (int kind = 0; kind < 5; ++kind)
    {
        auto *table = StringTable_GST::PgstNew(kind == 1 || kind == 4 ? 4 : 0);
        Check(table != pvNil);
        if (kind == 0)
        {
            String text;
            text.SetSzs(PszLit("Alpha"));
            Check(table->FAddStn(&text));
            text.SetNil();
            Check(table->FAddStn(&text));
            text.SetSzs(PszLit("Line\nbreak"));
            Check(table->FAddStn(&text));
            text.SetSzs(PszLit("Tab\tvalue"));
            Check(table->FAddStn(&text));
            text.SetSzs(PszLit("Alpha"));
            Check(table->FAddStn(&text));
        }
        else if (kind == 1 || kind == 4)
        {
            const byte extra[] = {0, 0x7f, 0x80, 0xff};
            for (int i = 0; i < (kind == 4 ? 300 : 32); ++i)
            {
                char label[32];
                std::snprintf(label, sizeof(label), "Entry %d", i);
                String text;
                text.SetSzs(label);
                Check(table->FAddStn(&text, const_cast<byte *>(extra)));
            }
        }
        else if (kind == 3)
        {
            String text;
            text.SetSzs(PszLit("A"));
            Check(table->FAddStn(&text));
        }
        DataBlock block;
        ChunkNumber number;
        Check(file->FAdd(table->CbOnFile(), kctgGst, &number, &block));
        file->Move(kctgGst, number, kctgGst, kind);
        Check(table->FWrite(&block, kind == 1 ? kboOther : kboCur));
        if (kind == 1)
            Check(file->FPackData(kctgGst, kind));
        if (kind == 3)
        {
            // One string occupies bytes 20..21; corrupt its stored offset.
            byte invalid[] = {0xff, 0xff, 0xff, 0xff};
            Check(block.FWriteRgb(invalid, 4, 22));
        }
        ReleasePpo(&table);
    }
    byte truncated = 0;
    Check(file->FPutPv(&truncated, 1, kctgGst, 5));
    // Independently encoded UTF-16LE GST: header, one Pascal string, one offset.
    byte wide[] = {1,   0,   5,   5, 4, 0,   0, 0,   1, 0,   0, 0,   10, 0, 0, 0, 255,
                   255, 255, 255, 4, 0, 'W', 0, 'i', 0, 'd', 0, 'e', 0,  0, 0, 0, 0};
    Check(file->FPutPv(wide, sizeof(wide), kctgGst, 6));
    // A header whose count/stride product would overflow a 32-bit reader.
    byte overflow[] = {1, 0, 3, 3, 252, 255, 255, 127, 0, 0, 0, 64, 0, 0, 0, 0, 255, 255, 255, 255};
    Check(file->FPutPv(overflow, sizeof(overflow), kctgGst, 7));
    byte embeddedNull[] = {1, 0, 3,   3,   4,   0,   0, 0,   1, 0,   0, 0, 4, 0,
                           0, 0, 255, 255, 255, 255, 3, 'A', 0, 'B', 0, 0, 0, 0};
    Check(file->FPutPv(embeddedNull, sizeof(embeddedNull), kctgGst, 8));
    byte wideBig[] = {0,   1,   4,   4, 0, 0, 0,   4, 0,   0, 0,   1, 0,   0, 0, 10, 255,
                      255, 255, 255, 0, 4, 0, 'W', 0, 'i', 0, 'd', 0, 'e', 0, 0, 0,  0};
    Check(file->FPutPv(wideBig, sizeof(wideBig), kctgGst, 9));
    Check(file->FSave('TEST'));
    ReleasePpo(&file);
    return 0;
}
