/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "studio.h"
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
    // Original 12-byte thumbnail envelope, with two 16-bit words followed by
    // two 32-bit words. This is a byte fixture, not a new serialized format.
    const byte little[] = {2, 1, 4, 3, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55};
    const byte big[] = {1, 2, 3, 4, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    const byte *fixture = BigLittle(big, little);
    BrowserThumbEntry record = {};
    CHECK(sizeof(record) == sizeof(little));
    std::memcpy(&record, fixture, sizeof(record));
    CHECK(record.bo == 0x0102 && record.osk == 0x0304 && record.ctg == 0x11223344 && record.cno == 0x55667788);
    CHECK(record.chid == 0x55667788 && record.grfontMask == 0x11223344 && record.grfont == 0x55667788);
    BrowserThumbEntry written = {};
    written.bo = 0x0102;
    written.osk = 0x0304;
    written.grfontMask = 0x11223344;
    written.grfont = 0x55667788;
    CHECK(!std::memcmp(&written, fixture, sizeof(written)));

    ThumbnailDescriptor descriptor = {};
    descriptor.tag.sid = -33;
    descriptor.tag.pcrf = reinterpret_cast<PChunkyResourceFile>(&descriptor);
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
    CHECK(descriptor.tag.sid == -33 && descriptor.tag.pcrf == reinterpret_cast<PChunkyResourceFile>(&descriptor));
    std::puts("Studio thumbnail layouts: original record bytes and TAG aliases preserved.");
    return 0;
}
