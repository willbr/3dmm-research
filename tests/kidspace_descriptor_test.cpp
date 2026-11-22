/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "kidspace_descriptor.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(FRAME_H) || defined(GOB_H)
#error Kidspace descriptor readers must not import the SDK or GUI
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
            std::fprintf(stderr, "Descriptor check failed at %d\n", __LINE__);                                         \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)
static void Word(std::vector<byte> &bytes, LegacyUlong value, bool big, unsigned count = 4)
{
    for (unsigned i = 0; i < count; ++i)
        bytes.push_back(static_cast<byte>(value >> (8 * (big ? count - i - 1 : i))));
}
int main()
{
    for (bool big : {false, true})
    {
        // Original GOKDF/LOP/cursor-map bytes, built independently of C++ structs.
        std::vector<byte> bytes;
        Word(bytes, 1, big, 2);
        Word(bytes, 0x0303, big, 2);
        const LegacyUlong words[] = {0x12345678, // GOK kind
                                     0x1234,
                                     static_cast<LegacyUlong>(-7),
                                     23,
                                     static_cast<LegacyUlong>(-11),
                                     0,
                                     31,
                                     static_cast<LegacyUlong>(-47),
                                     8, // terminating/default location
                                     0xC0,
                                     0x80,
                                     0x80000000,
                                     0x11223344,
                                     0x55667788,
                                     0x1234567,
                                     0x98765432};
        for (auto word : words)
            Word(bytes, word, big);
        CHECK((bytes.size)() == 68);
        auto file = ChunkyFile::PcflCreateTemp();
        CHECK(file);
        ChunkNumber number;
        CHECK(file->FAddPv(bytes.data(), static_cast<LegacyLong>((bytes.size)()), 'GOKD', &number));
        ChunkNumber malformed;
        CHECK(file->FAddPv(bytes.data(), static_cast<LegacyLong>((bytes.size)() - 4), 'GOKD', &malformed));
        const std::vector<byte> cursorRow(bytes.end() - 28, bytes.end());
        for (unsigned i = 0; i < 32; ++i)
            bytes.insert(bytes.end(), cursorRow.begin(), cursorRow.end());
        ChunkNumber packedNumber;
        CHECK(file->FAddPv(bytes.data(), static_cast<LegacyLong>((bytes.size)()), 'GOKD', &packedNumber));
        CHECK(file->FPackData('GOKD', packedNumber) && file->FPacked('GOKD', packedNumber));

        auto resources = ChunkyResourceFile::PcrfNew(file, 4096);
        auto empty = ChunkyFile::PcflCreateTemp();
        auto manager = ChunkyResourceManager::PcrmNew(2);
        CHECK(resources && empty && manager && manager->FAddCfl(empty, 0));
        auto cache = NewTypedSiblingCache(resources->CoreCache(), 4096);
        CHECK(cache && mm_resource_manager_add_cache(manager->CoreManager(), cache, nullptr) == MM_STORAGE_OK);
        mm_resource_cache_release(cache);
        ReleasePpo(&resources);
        ReleasePpo(&empty);
        ReleasePpo(&file);

        auto owner = FetchKidspaceDescriptor(manager, 'GOKD', number);
        CHECK(owner);
        auto same = FetchKidspaceDescriptor(manager, 'GOKD', number);
        CHECK(same == owner);
        mm_kidspace_owner_release(same);
        const auto binding = mm_kidspace_owner_binding(owner);
        CHECK(binding->cache == cache && binding->type == 'GOKD' && binding->number == number && binding->attached);
        const auto descriptor = mm_kidspace_owner_descriptor(owner);
        CHECK(descriptor->kind == 0x12345678);
        MMKidspaceLocation location;
        mm_kidspace_location(descriptor, 0x1234, &location);
        CHECK(location.hidPar == 0x1234 && location.xp == -7 && location.yp == 23 && location.zp == -11);
        mm_kidspace_location(descriptor, 0x5678, &location);
        CHECK(!location.hidPar && location.xp == 31 && location.yp == -47 && location.zp == 8);
        MMKidspaceCursor cursor;
        CHECK(mm_kidspace_cursor(descriptor, 0x80, 31, &cursor));
        CHECK(cursor.grfcustMask == 0xC0 && cursor.grfcust == 0x80 && cursor.grfbitSno == 0x80000000 &&
              cursor.cnoCurs == 0x11223344 && cursor.chidScript == 0x55667788 && cursor.cidDefault == 0x1234567 &&
              cursor.cnoTopic == 0x98765432);
        CHECK(!mm_kidspace_cursor(descriptor, 0x40, 31, &cursor) && !mm_kidspace_cursor(descriptor, 0x80, 30, &cursor));
        CHECK(mm_kidspace_cursor(descriptor, 0x80, -1, &cursor));
        bool error = true;
        CHECK(!FetchKidspaceDescriptor(manager, 'GOKD', packedNumber + 1, &error) && !error);
        CHECK(!FetchKidspaceDescriptor(manager, 'GOKD', malformed, &error) && error);
        auto packed = FetchKidspaceDescriptor(manager, 'GOKD', packedNumber);
        CHECK(packed && mm_kidspace_owner_descriptor(packed)->cursors == 33 && cache->count == 2);
        mm_kidspace_owner_priority(packed, MM_RESOURCE_TOSS_PRIORITY);
        mm_kidspace_owner_release(packed);
        CHECK(cache->count == 1);
        ReleasePpo(&manager);
        CHECK(!binding->attached && !binding->cache);
        CHECK(mm_kidspace_cursor(descriptor, 0x80, 31, &cursor) && cursor.cnoCurs == 0x11223344);
        mm_kidspace_owner_release(owner);
    }
    std::puts("Kidspace descriptors: LE/BE decoding, ordered cache, packed payload, eviction and retained owner passed.");
}
