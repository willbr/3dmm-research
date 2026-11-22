/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "studio.h"
#include "mm/browser_thumbnail.h"
#undef size
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
            std::fprintf(stderr, "Studio layout failed at %d\n", __LINE__);                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (false)
int main()
{
    const byte little[] = {1, 0, 4, 3, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55};
    const byte big[] = {0, 1, 3, 4, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const byte *fixtures[] = {little, big};
    MMMemoryStream stream;
    MMBrowserThumbnail record;
    for (const auto fixture : fixtures)
    {
        CHECK(mm_memory_stream_init(&stream, fixture, sizeof(little)));
        CHECK(mm_browser_thumbnail_decode(&stream.stream, &record));
        CHECK(record.type == 0x11223344 && record.value == 0x55667788);
        CHECK(record.graphic == record.value && !record.has_graphic);
    }
    CHECK(mm_memory_stream_init(&stream, little, sizeof(little) - 1));
    CHECK(!mm_browser_thumbnail_decode(&stream.stream, &record) && record.value == 0x55667788);
    byte invalid[sizeof(little)];
    std::memcpy(invalid, little, sizeof(invalid));
    invalid[1] = 2;
    CHECK(mm_memory_stream_init(&stream, invalid, sizeof(invalid)));
    CHECK(!mm_browser_thumbnail_decode(&stream.stream, &record) && record.value == 0x55667788);

    const MMChunkEntry rows[] = {{'ROOT', 1, 0, 0, 0, 3}, {'ROOT', 5}, {'ZZZZ', 0}};
    const MMChunkAccess archive = {
        const_cast<MMChunkEntry *>(rows), 3,
        [](void *data, MMU32 position, MMChunkEntry *out) -> int {
            *out = static_cast<const MMChunkEntry *>(data)[position];
            return 1;
        },
        [](void *, MMU32, MMU32 position, MMChunkChild *out) -> int {
            const MMChunkChild children[] = {{'THUM', 3, 0}, {'GOKD', 1, 1}, {'THUM', 6, 1}};
            *out = children[position];
            return 1;
        }};
    MMU32 first;
    CHECK(mm_browser_thumbnail_first(&archive, 'ROOT', MM_BROWSER_THUMBNAIL_ANY, &first) == MM_CHUNK_OK && first == 0);
    CHECK(mm_browser_thumbnail_first(&archive, 'ROOT', 5, &first) == MM_CHUNK_OK && first == 1);
    CHECK(mm_browser_thumbnail_first(&archive, 'ROOT', 1, &first) == MM_CHUNK_OK && first == 0);
    MMU32 visited = 0;
    auto reject = [](void *data, MMU32, MMU32 number, MMU32) -> int {
        auto &visited = *static_cast<MMU32 *>(data);
        visited = visited * 10 + number;
        return 0;
    };
    CHECK(!mm_browser_thumbnail_visit(&archive, first, 0, reject, &visited) && visited == 1);
    visited = 0;
    CHECK(mm_browser_thumbnail_visit(&archive, first, 'THUM', reject, &visited) && visited == 36);

    ThumbnailDescriptor descriptor = {};
    descriptor.tag.sid = -33;
    descriptor.tag.cache = reinterpret_cast<MMResourceCache *>(&descriptor);
    descriptor.tag.ctg = 0x11223344;
    descriptor.tag.cno = 0x55667788;
    byte prefix[offsetof(TAG, ctg)];
    std::memcpy(prefix, &descriptor, sizeof(prefix));
    CHECK(descriptor.grfontMask == descriptor.tag.ctg && descriptor.grfont == descriptor.tag.cno);
    descriptor.grfontMask = 0x87654321;
    descriptor.grfont = 0x76543210;
    CHECK(descriptor.tag.ctg == 0x87654321 && descriptor.tag.cno == 0x76543210);
    descriptor.chid = 0x12345678;
    CHECK(descriptor.tag.cno == 0x12345678 && !std::memcmp(prefix, &descriptor, sizeof(prefix)));
    CHECK(descriptor.tag.sid == -33 && descriptor.tag.cache == reinterpret_cast<MMResourceCache *>(&descriptor));
    std::puts("Studio thumbnails: both byte orders, malformed records, archive traversal, and TAG aliases passed.");
    return 0;
}
