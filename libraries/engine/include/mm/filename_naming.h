/* Filename naming policy converted from Kauai filename.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_FILENAME_NAMING_H
#define MM_ENGINE_FILENAME_NAMING_H
#include "filename_path.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMFilenameNamingState { MMU32 sequence; } MMFilenameNamingState;
typedef struct MMFilenameNamingHost {
    MMFilenamePathHost path;
    MMU32 (*clock)(void *);
    MMI32 (*temp_path)(void *, void *output, MMI32 capacity);
} MMFilenameNamingHost;
/* State starts zeroed and is shared by unique and temporary naming. Each
 * increment retains the low 16 bits, matching the native short sequence and
 * four-digit hexadecimal formatting without signed overflow/conversion.
 * One clock read seeds each unique call. Try at most twenty candidates, skip
 * an exact old leaf before extension replacement, and accept only missing.
 * Failed setters and maybe/existing results still advance the sequence. The
 * last failed attempt advances once more before clearing/reporting general
 * failure. Successful setters and their errors remain visible between attempts;
 * in particular, a cleared value can recover to a relative candidate on retry.
 * Temporary naming caches its first successful directory and ignores later
 * environment changes. Provider failure leaves both caller/cache untouched;
 * unique failure clears the caller but retains the cache. temp_path uses the
 * native zero-terminated path (capacity 255 including terminator), returning
 * nonzero on success. Return length is not used to copy the path.
 * path/value/storage/callback contracts follow filename_path.h. The initialized
 * cached directory is distinct from the caller, has matching units and lives
 * across calls. Optional copy_name preserves native owner metadata; otherwise
 * C copies type/path. Providers use path.context. Callers serialize access to
 * both shared state and cache. No heap allocation or hidden globals. */
int mm_filename_unique(const MMFilenameValue *, const MMFilenameNamingHost *, MMFilenameNamingState *, MMI32 type);
int mm_filename_temp(const MMFilenameValue *, const MMFilenameNamingHost *, MMFilenameNamingState *,
                     const MMFilenameValue *cached_directory, MMI32 type);
#ifdef __cplusplus
}
#endif
#endif
