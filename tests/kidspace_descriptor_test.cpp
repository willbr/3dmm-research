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
        auto resources = ChunkyResourceFile::PcrfNew(file, 4096);
        CHECK(resources);
        auto descriptor = static_cast<PKidspaceGraphicObjectDescriptorLocation>(
            resources->PbacoFetch('GOKD', number, KidspaceGraphicObjectDescriptorLocation::FReadGkds));
        CHECK(descriptor && descriptor->Gokk() == 0x12345678);
        LOP location;
        descriptor->GetLop(0x1234, &location);
        CHECK(location.hidPar == 0x1234 && location.xp == -7 && location.yp == 23 && location.zp == -11);
        descriptor->GetLop(0x5678, &location);
        CHECK(!location.hidPar && location.xp == 31 && location.yp == -47 && location.zp == 8);
        CursorMapEntry cursor;
        CHECK(descriptor->FGetCume(0x80, 31, &cursor));
        CHECK(cursor.grfcustMask == 0xC0 && cursor.grfcust == 0x80 && cursor.grfbitSno == 0x80000000 &&
              cursor.cnoCurs == 0x11223344 && cursor.chidScript == 0x55667788 && cursor.cidDefault == 0x1234567 &&
              cursor.cnoTopic == 0x98765432);
        CHECK(!descriptor->FGetCume(0x40, 31, &cursor) && !descriptor->FGetCume(0x80, 30, &cursor));
        ReleasePpo(&descriptor);
        ReleasePpo(&resources);
        ReleasePpo(&file);
    }
    std::puts("Kidspace descriptors: original LE/BE bytes, parent/default locations and high-bit cursor masks passed.");
}
