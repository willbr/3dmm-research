#ifndef MM_PRIVATE_ARCHIVE_HOST_H
#define MM_PRIVATE_ARCHIVE_HOST_H
#include "kauai_core.h"
#include "mm/native_archive_file.h"

inline MMPrivateArchiveProfile KauaiPrivateArchiveProfile()
{
#ifdef CHUNK_BIG_INDEX
    constexpr int expanded = 1;
#else
    constexpr int expanded = 0;
#endif
    return mm_native_archive_profile(koskCur, MacWin(koskUniWin, koskUniMac), expanded, DebugShip(1, 0));
}
#endif
