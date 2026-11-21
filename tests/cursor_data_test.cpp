/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "kauai_core.h"
#include "groups.h"
#include "cursor_format.h"
#include "handle_services.h"
#include "mm/studio_resource.h"
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

static bool Read(DataBlock &block, int width, int height, MMStudioCursor &image)
{
    MMBlockIO io;
    MMReadStream stream;
    return block.FUnpackData() && mm_owned_block_io(block.CoreBlock(), &io) == MM_DATA_OK &&
           mm_block_io_stream(&io, &stream) == MM_DATA_OK && mm_studio_cursor_read(&stream, width, height, &image);
}

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
            MMStudioCursor image;
            CHECK(Read(block, 32, 32, image));
            const int expected = invalid ? 16 : 32;
            CHECK(image.width == expected && image.height == expected && image.pitch == expected / 8 &&
                  image.hot_x == 3 && image.hot_y == 4);
            const int planeBytes = expected * expected / 8;
            for (int i = 0; i < planeBytes; ++i)
                CHECK(image.and_bits[i] == static_cast<byte>(i * 37 + expected) &&
                      image.xor_bits[i] == static_cast<byte>((i + planeBytes) * 37 + expected));
            CHECK(Read(block, 24, 24, image) && image.width == 16);
            const MMStudioCursor previous = image;
            CHECK(!Read(block, 8, 8, image) && image.width == 16 &&
                  !memcmp(image.and_bits, previous.and_bits, sizeof(image.and_bits)));
            CHECK(!Read(block, 0, 32, image) && !memcmp(image.and_bits, previous.and_bits, sizeof(image.and_bits)));
            CHECK(Read(block, INT32_MAX, INT32_MAX, image) && image.width == expected);
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
        MMStudioCursor image;
        CHECK(!Read(block, 32, 32, image));
        ReleasePpo(&group);
    }
    // A location may not point into the location table, even if its total byte count looks valid.
    byte invalid[] = {1, 0, 3, 3, 1, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 8, 0, 0, 0,
                      0, 0, 0, 0, 8, 0, 0, 0};
    MMMemoryStream memory;
    MMStudioCursor image;
    CHECK(mm_memory_stream_init(&memory, invalid, sizeof(invalid)) &&
          !mm_studio_cursor_read(&memory.stream, 32, 32, &image));
    ReleasePpo(&file);
    std::puts("Cursor data: endian records, size selection, original planes/hotspots and malformed candidates passed.");
    return 0;
}
