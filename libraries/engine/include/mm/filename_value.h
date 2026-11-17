/* Filename value rules converted from Kauai filename.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_FILENAME_VALUE_H
#define MM_ENGINE_FILENAME_VALUE_H
#include "string_value.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MM_FILENAME_NIL 0x2e2e2e2cL
#define MM_FILENAME_DIRECTORY 0x2e2e2e2eL
enum { MM_FILENAME_FILE = 1, MM_FILENAME_DIR = 2, MM_FILENAME_EMPTY = 4, MM_FILENAME_BASE = 8 };
enum { MM_FILENAME_BAD_TYPE = 1, MM_FILENAME_LEAF_TYPE, MM_FILENAME_BAD_PATH,
       MM_FILENAME_EMPTY_PATH, MM_FILENAME_APPEND };
typedef struct MMFilenameState { MMI32 type; } MMFilenameState;
typedef struct MMFilenameValue {
    MMFilenameState *state;
    MMStringValue path;
    void *owner;
} MMFilenameValue;
typedef struct MMFilenameValueHost {
    void *context;
    unsigned (*upper)(void *, unsigned byte);
    int (*separator)(void *, unsigned character);
    void (*validate)(void *, const MMFilenameValue *, int flags);
    void (*assertion)(void *, int code);
    void (*failed)(void *);
} MMFilenameValueHost;
/* State owns type; path borrows 257 aligned one/two-byte counted units under
 * string_value.h. Rebuild views after copying/relocating owner storage. Character
 * inputs/outputs use the same unit and do not alias the path. Owner is only a
 * borrowed diagnostic token. Upper maps one byte using the native code page;
 * separator supplies host syntax. Other hooks are optional, synchronous and
 * cannot unwind or mutate the operation. Copy diagnostics also receive temporary
 * extension copies. Callers serialize mutation.
 * Extension recognition scans at most five units including the dot. One-byte
 * units all round-trip through signed char; two-byte units accept ASCII and
 * FF80..FFFF, preserving the native signed-byte round-trip test. Type cleaning
 * scans low byte first, discarding higher bytes after zero. Capacity is 255.
 * A change-type overflow leaves state/path untouched, with >= retaining room
 * for the dot even for an empty extension. Equal-type requests succeed first.
 * Change-leaf overflow leaves the parent directory published; set-leaf clears
 * that intermediate state before reporting failure. Nil/directory type requests
 * to change_type and paths without separators retain advisory native diagnostics.
 * This is not path canonicalization, filesystem access or enumeration policy. */
void mm_filename_value_clear(const MMFilenameValue *, const MMFilenameValueHost *);
MMI32 mm_filename_clean_type(MMI32, const MMFilenameValueHost *, const MMStringValue *extension);
MMI32 mm_filename_extension_length(const MMFilenameValue *, const MMFilenameValueHost *);
void mm_filename_derive_type(const MMFilenameValue *, const MMFilenameValueHost *);
int mm_filename_change_type(const MMFilenameValue *, const MMFilenameValueHost *, MMI32 type);
int mm_filename_change_leaf(const MMFilenameValue *, const MMFilenameValueHost *, const void *characters, MMI32 count);
int mm_filename_set_leaf(const MMFilenameValue *, const MMFilenameValueHost *, const void *characters, MMI32 count, MMI32 type);
void mm_filename_get_leaf(const MMFilenameValue *, const MMFilenameValueHost *, const MMStringValue *output);
#ifdef __cplusplus
}
#endif
#endif
