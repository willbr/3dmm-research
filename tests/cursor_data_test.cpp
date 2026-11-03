/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "cursor_data.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "kauai_core.h"
#include "groups.h"
#undef size
ASSERTNAME
void WarnProc(schar *, LegacyLong, schar *)
{
}
bool FAssertProc(schar *, LegacyLong, schar *, void *, LegacyLong)
{
    std::abort();
}
#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "Cursor data check failed at %d\n", __LINE__);                                        \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main()
{
    auto file = ChunkyFile::PcflCreateTemp();
    CHECK(file);
    for (short order : {kboCur, kboOther})
        for (int invalid = 0; invalid < 5; ++invalid)
        {
            auto group = GeneralGroup::PggNew(sizeof(CursorOnFile));
            CHECK(group);
            for (int width : {16, 32})
            {
                CursorOnFile header = {curtMonochrome, 3, 4, static_cast<byte>(width), static_cast<byte>(width)};
                std::vector<byte> planes(width * width / 4);
                for (std::size_t i = 0; i < planes.size(); ++i)
                    planes[i] = static_cast<byte>(i * 37 + width);
                if (width == 32)
                    switch (invalid)
                    {
                    case 1:
                        header.curt = 1;
                        break;
                    case 2:
                        header.xp = 32;
                        break;
                    case 3:
                        header.dyp = 16;
                        break;
                    case 4:
                        planes.pop_back();
                        break;
                    }
                if (order == kboOther)
                    SwapBytesBom(&header, kbomCurf);
                CHECK(group->FAdd(static_cast<LegacyLong>(planes.size()), nullptr, planes.data(), &header));
            }
            DataBlock block;
            ChunkNumber number;
            CHECK(file->FAdd(group->CbOnFile(), 'GGCR', &number, &block) && group->FWrite(&block, order));
            ReleasePpo(&group);
            KauaiCursors::Monochrome image;
            CHECK(KauaiCursors::Read(block, 32, 32, image));
            const int expected = invalid ? 16 : 32;
            CHECK(image.width == expected && image.height == expected && image.pitch == expected / 8 &&
                  image.hotX == 3 && image.hotY == 4 && image.andBits.size() == std::size_t(expected * expected / 8));
            for (std::size_t i = 0; i < image.andBits.size(); ++i)
                CHECK(image.andBits[i] == static_cast<byte>(i * 37 + expected) &&
                      image.xorBits[i] == static_cast<byte>((i + image.andBits.size()) * 37 + expected));
            CHECK(KauaiCursors::Read(block, 24, 24, image) && image.width == 16);
            auto previous = image.andBits;
            CHECK(!KauaiCursors::Read(block, 8, 8, image) && image.width == 16 && image.andBits == previous);
            CHECK(!KauaiCursors::Read(block, 0, 32, image) && image.andBits == previous);
            CHECK(KauaiCursors::Read(block, INT32_MAX, INT32_MAX, image) && image.width == expected);
        }
    // Reject an incompatible GG fixed-record size and an empty cursor list.
    for (int fixed : {4, 8})
    {
        auto group = GeneralGroup::PggNew(fixed);
        CHECK(group);
        if (fixed == 4)
        {
            byte header[4] = {};
            CHECK(group->FAdd(0, nullptr, nullptr, header));
        }
        DataBlock block;
        ChunkNumber number;
        CHECK(file->FAdd(group->CbOnFile(), 'GGCR', &number, &block) && group->FWrite(&block));
        KauaiCursors::Monochrome image;
        CHECK(!KauaiCursors::Read(block, 32, 32, image));
        ReleasePpo(&group);
    }
    ReleasePpo(&file);
    std::puts("Cursor data: endian records, size selection, original planes/hotspots and malformed candidates passed.");
    return 0;
}
