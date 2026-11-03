/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */
#include "picture_data.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
static void Check(bool condition)
{
    if (!condition)
        std::exit(1);
}
int main()
{
    static_assert(PictureData::HeaderBytes == 20, "picture wire header drift");
    static_assert(sizeof(RC) == 16 && sizeof(FileType) == 4 && sizeof(FilePosition) == 4,
                  "legacy data widths must not follow native long");
    // Deliberately not a decodable metafile: the data layer preserves bytes.
    const byte payload[] = {0, 0xff, 3, 1, 4, 0x80};
    RC bounds(-2, 3, 12, 23);
    PChunkyFile file = ChunkyFile::PcflCreateTemp();
    Check(file != pvNil);
    for (int big = 0; big < 2; ++big)
    {
        PictureData source, loaded;
        Check(source.FInit(bounds, payload, sizeof(payload), big != 0));
        ChunkNumber number;
        DataBlock block;
        Check(file->FAdd(26, 'META', &number, &block));
        Check(source.FWrite(&block));
        byte original[26];
        Check(block.FReadRgb(original, sizeof(original), 0));
        Check(original[big ? 3 : 0] == 0xfe && original[big ? 0 : 3] == 0xff);
        Check(original[big ? 19 : 16] == 26 && !std::memcmp(original + 20, payload, sizeof(payload)));
        Check(loaded.FRead(&block, big != 0));
        RC read = loaded.Bounds();
        Check(read == bounds && loaded.PayloadBytes() == sizeof(payload));
        Check(!std::memcmp(loaded.Payload(), payload, sizeof(payload)));
        ChunkNumber copy;
        DataBlock copied;
        Check(file->FAdd(26, 'META', &copy, &copied) && loaded.FWrite(&copied));
        byte roundTrip[26];
        Check(copied.FReadRgb(roundTrip, sizeof(roundTrip), 0) && !std::memcmp(original, roundTrip, 26));
        byte bad = 0;
        Check(block.FWriteRgb(&bad, 1, big ? 19 : 16));
        Check(!loaded.FRead(&block, big != 0));
        // Failure must leave the previously loaded payload intact.
        Check(loaded.PayloadBytes() == sizeof(payload) && !std::memcmp(loaded.Payload(), payload, sizeof(payload)));
    }
    ReleasePpo(&file);
    return 0;
}
