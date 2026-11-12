/* File-text policy converted from Kauai file.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_FILE_TEXT_H
#define THREE_MM_ENGINE_FILE_TEXT_H
#include "block_owner.h"
#include "text.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMFileTextConverter
    {
        void *context;
        int current_encoding, default_byte_encoding, native_unicode_encoding, native_big_endian;
        ThreeMMU32 output_unit;
        /* NULL output queries character count. Nonpositive query skips the chunk,
         * retaining legacy policy. A conversion must return the queried count. */
        ThreeMMI32 (*convert)(void *, const void *, ThreeMMU32, int, void *, ThreeMMU32);
    } ThreeMMFileTextConverter;
    /* file owns a reference. A successful conversion adopts a new temporary file
     * and releases the original; a no-op retains it. An even-length BOM probe may
     * strip two bytes from the range even if later work fails. Other failed work
     * leaves its reference/range intact. Requested encoding zero means infer.
     * A failed SPEED input allocation uses a bounded 512-byte stack buffer; output
     * growth uses exact NORMAL allocation. Services/callbacks may not re-enter. */
    int three_mm_file_text_translate(ThreeMMBlockFile *file, const ThreeMMBlockFileServices *,
                                     const ThreeMMFileTextConverter *, const ThreeMMStorageAllocator *, int requested);
#ifdef __cplusplus
}
#endif
#endif
