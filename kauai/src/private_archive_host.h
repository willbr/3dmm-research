#ifndef MM_PRIVATE_ARCHIVE_HOST_H
#define MM_PRIVATE_ARCHIVE_HOST_H
#include "kauai_core.h"
#include "mm/private_archive.h"

inline MMPrivateArchiveProfile KauaiPrivateArchiveProfile()
{
    MMPrivateArchiveProfile profile = {sizeof(achar), koskCur, MacWin(koskUniWin, koskUniMac), 0, DebugShip(1, 0),
        KauaiEncodeLegacyChunkName,
        [](void *, const void *bytes, size_t count, int encoding, void *out, size_t capacity) -> size_t {
            return CchTranslateRgb(const_cast<void *>(bytes), count, encoding, static_cast<achar *>(out), capacity);
        }};
#ifdef CHUNK_BIG_INDEX
    profile.expanded = 1;
#endif
    return profile;
}
#endif
