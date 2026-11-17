/* Filename storage/copy/validation converted from Kauai filename.cpp and String.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_FILENAME_OWNED_H
#define MM_ENGINE_FILENAME_OWNED_H
#include "filename_value.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMFilenameOwned8 { MMFilenameState state; unsigned char path[257]; } MMFilenameOwned8;
typedef struct MMFilenameOwned16 { MMFilenameState state; unsigned short path[257]; } MMFilenameOwned16;
enum { MM_FILENAME_CHECK_COUNT = 1, MM_FILENAME_CHECK_TERMINATOR, MM_FILENAME_CHECK_NULL,
       MM_FILENAME_CHECK_EMPTY, MM_FILENAME_CHECK_NAMED_EMPTY, MM_FILENAME_CHECK_FULL,
       MM_FILENAME_CHECK_DIR, MM_FILENAME_CHECK_SEPARATOR, MM_FILENAME_CHECK_NO_LEAF,
       MM_FILENAME_CHECK_FILE, MM_FILENAME_CHECK_LEAF };
typedef struct MMFilenameOwnedHost {
    void *context;
    void (*check_path)(void *, const MMStringValue *);
    void (*memory)(void *, const void *, size_t);
    void (*assertion)(void *, int code, MMI32 observed);
    void (*arguments)(void *, const void *, MMI32 count, unsigned unit);
    size_t (*full_path)(void *, const void *, void *, size_t, void **leaf);
    int (*equal)(void *, const void *, const void *);
    int (*separator)(void *, unsigned);
} MMFilenameOwnedHost;
/* Records own 257 counted units; no allocation, pointer members or C++ lifetime.
 * Rebuild borrowed views after copying/relocation. Both profiles coexist in one
 * C library. Owner/context/copy checks are borrowed and never stored in records.
 * Init clears only count/terminator; copy publishes type before path checks and
 * copies only count+2 units, retaining the unused destination tail. Assignment
 * checks destination/source/destination; construction checks source/destination.
 * Base-object metadata copying remains the temporary native facade's concern.
 * check_path is optional, synchronous and diagnostic only (the native facade
 * retains its assertion-depth guard); it may call check_path below. Memory,
 * assertion and argument hooks cannot mutate, unwind or retain borrowed views.
 * Full validation requires full_path/equal/separator for nonnil records. Full
 * path returns length and a null leaf for directories or a leaf inside output.
 * Flags use MM_FILENAME_FILE/DIR/EMPTY, with zero permitting all. Checks retain
 * native ordering; invalid counts are diagnosed without out-of-bounds scans.
 * Set/export inputs and outputs do not overlap storage; self-copy retains the
 * supplied copy diagnostic and memmove behavior. Callers serialize mutation. */
MMFilenameValue mm_filename_owned8_view(MMFilenameOwned8 *, void *owner);
MMFilenameValue mm_filename_owned16_view(MMFilenameOwned16 *, void *owner);
void mm_filename_owned_init(const MMFilenameValue *, const MMFilenameOwnedHost *);
void mm_filename_owned_copy(const MMFilenameValue *, const MMFilenameValue *, int assignment, const MMFilenameOwnedHost *);
void mm_filename_owned_set_path(const MMFilenameValue *, const void *, MMI32 count, const MMFilenameOwnedHost *);
void mm_filename_owned_export(const MMFilenameValue *, const MMStringValue *, const MMFilenameOwnedHost *);
void mm_filename_owned_check_path(const MMStringValue *, const MMFilenameOwnedHost *);
void mm_filename_owned_validate(const MMFilenameValue *, int flags, const MMFilenameOwnedHost *);
#ifdef __cplusplus
}
#endif
#endif
