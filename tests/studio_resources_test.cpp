/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <cstdio>
#include <cstdlib>
#include "studio_resources.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Studio resource tables must not import the Windows SDK
#endif
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *file, LegacyLong line, schar *message, void *, LegacyLong)
{
    std::fprintf(stderr, "%s:%d %s\n", file, static_cast<int>(line), message ? message : "");
    std::_Exit(2);
}
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "Studio resources failed at %d\n", __LINE__);                                         \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)
int main()
{
    const LegacyLong expected[] = {0x11223344, -1, (-2147483647 - 1)};
    const short orders[] = {kboCur, kboOther};
    const LegacyLong widths[] = {0, 4, 8};
    for (short order : orders)
        for (LegacyLong width : widths)
        {
            auto source = StringTable_GST::PgstNew(width);
            CHECK(source);
            for (LegacyLong value : expected)
            {
                LegacyLong extra[2] = {value, 0x55667788};
                if (order != kboCur)
                    SwapBytesRglw(extra, 2);
                String text(PszLit("Studio resource"));
                CHECK(source->FAddStn(&text, width ? extra : pvNil));
            }
            auto file = ChunkyFile::PcflCreateTemp();
            CHECK(file);
            ChunkNumber number;
            DataBlock block;
            CHECK(file->FAdd(source->CbOnFile(), 'GST ', &number, &block));
            CHECK(source->FWrite(&block, order));
            auto table = StudioResources::ReadStringTable(&block);
            if (width == 8)
                CHECK(!table); // Valid generic GST, unsupported Studio schema.
            else
            {
                CHECK(table && table->CbExtra() == width && table->IvMac() == 3);
                for (LegacyLong index = 0; index < 3; ++index)
                {
                    String text;
                    table->GetStn(index, &text);
                    CHECK(text.FEqualSz(PszLit("Studio resource")));
                    if (width)
                    {
                        struct
                        {
                            LegacyLong value;
                            LegacyLong canary;
                        } extra = {0, 0x55667788};
                        table->GetExtra(index, &extra.value);
                        CHECK(extra.value == expected[index] && extra.canary == 0x55667788);
                    }
                }
            }
            ReleasePpo(&table);
            ReleasePpo(&source);
            ReleasePpo(&file);
        }
    std::puts("Studio resources: native/swapped 32-bit extras, strings and unsupported-width rejection passed.");
    return 0;
}
