/* File-text policy converted from Kauai file.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_FILE_TEXT_H
#define MM_ENGINE_FILE_TEXT_H
#include "block_owner.h"
#include "text.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMFileTextConverter
    {
        void *context;
        int current_encoding, default_byte_encoding, native_unicode_encoding, native_big_endian;
        MMU32 output_unit;
        /* NULL output queries character count. Nonpositive query skips the chunk,
         * retaining legacy policy. A conversion must return the queried count. */
        MMI32 (*convert)(void *, const void *, MMU32, int, void *, MMU32);
    } MMFileTextConverter;
    /* file owns a reference. A successful conversion adopts a new temporary file
     * and releases the original; a no-op retains it. An even-length BOM probe may
     * strip two bytes from the range even if later work fails. Other failed work
     * leaves its reference/range intact. Requested encoding zero means infer.
     * A failed SPEED input allocation uses a bounded 512-byte stack buffer; output
     * growth uses exact NORMAL allocation. Services/callbacks may not re-enter. */
    int mm_file_text_translate(MMBlockFile *file, const MMBlockFileServices *,
                                     const MMFileTextConverter *, const MMStorageAllocator *, int requested);
#ifdef __cplusplus
}
#endif
#endif
