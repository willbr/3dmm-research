/* Filename path control converted from Kauai filename.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_FILENAME_PATH_H
#define MM_ENGINE_FILENAME_PATH_H
#include "filename_value.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MM_FILENAME_CREATE_DIR 1UL
#define MM_FILENAME_MOVE_TO_DIR 2UL
enum { MM_FILENAME_NO, MM_FILENAME_YES, MM_FILENAME_MAYBE };
enum { MM_FILENAME_FOUND, MM_FILENAME_MISSING, MM_FILENAME_INFO_ERROR };
enum { MM_FILENAME_GENERAL = 1, MM_FILENAME_DELETE, MM_FILENAME_RENAME,
       MM_FILENAME_MISMATCH, MM_FILENAME_HIDDEN, MM_FILENAME_DIR_CREATE };
enum { MM_FILENAME_PATH_OPEN = 1, MM_FILENAME_PATH_DIRECTORY, MM_FILENAME_PATH_APPEND };
typedef struct MMFilenameInfo { int directory, hidden, system; } MMFilenameInfo;
typedef struct MMFilenamePathHost {
    MMFilenameValueHost value;
    void *context;
    MMI32 (*full_path)(void *, const void *input, void *output, MMI32 capacity);
    MMI32 (*find_path)(void *, const void *input, const void *search, void *output, MMI32 capacity);
    unsigned (*separator)(void *);
    int (*info)(void *, const void *path, MMFilenameInfo *);
    int (*remove)(void *, const void *path);
    int (*rename)(void *, const void *source, const void *destination);
    int (*make_directory)(void *, const void *path);
    int (*equal)(void *, const void *a, const void *b);
    MMU32 (*volume_kinds)(void *, const void *path);
    int (*is_open)(void *, const MMFilenameValue *);
    void (*copy_name)(void *, const MMFilenameValue *destination, const MMFilenameValue *source);
    void (*issue)(void *, const MMFilenameValue *, int code);
    void (*assertion)(void *, int code);
} MMFilenamePathHost;
/* Views/storage follow filename_value.h. All character units match. Inputs,
 * outputs and scratch views are distinct and live throughout calls. Scratch
 * views start initialized; copy_name can preserve native owner metadata as well
 * as type/path. A null copy_name uses C value copies. Other physical primitives
 * are required for the requested operation. Callback effects are restricted to
 * their designated output/filesystem operation; callbacks cannot unwind, destroy
 * owners or mutate unrelated records. Callers serialize mutation. Copy diagnostics
 * also receive temporary path copies. No allocation or hidden globals occur.
 * Full/find return a terminated native path and its length, or zero; capacity is
 * the legacy 255 units, including terminator. Canonicalization/search semantics
 * belong to the native provider. Build/search failure clear before reporting.
 * Down/up failures preserve the original owner. Up also preserves leaf output on
 * failure and adds no error. Down creates on any non-yes status when requested.
 * Existence strips one trailing directory separator except for a one-unit root;
 * mismatch precedes hidden/system diagnostics. Rename changes no logical name.
 * Directory/open-file assertions are advisory. is_open is skipped without an
 * assertion hook. Equality retains native argument order. */
int mm_filename_build(const MMFilenameValue *, const MMFilenamePathHost *, const MMStringValue *input, MMI32 default_type);
int mm_filename_search(const MMFilenameValue *, const MMFilenamePathHost *, const MMStringValue *input, const MMStringValue *search);
int mm_filename_exists(const MMFilenameValue *, const MMFilenamePathHost *);
int mm_filename_delete(const MMFilenameValue *, const MMFilenamePathHost *);
int mm_filename_rename(const MMFilenameValue *, const MMFilenamePathHost *, const MMFilenameValue *destination);
int mm_filename_equal(const MMFilenameValue *, const MMFilenamePathHost *, const MMFilenameValue *other);
int mm_filename_same_directory(const MMFilenameValue *, const MMFilenamePathHost *, const MMFilenameValue *other,
                                const MMFilenameValue *scratch_a, const MMFilenameValue *scratch_b);
int mm_filename_down(const MMFilenameValue *, const MMFilenamePathHost *, const MMStringValue *leaf, MMU32 flags,
                     const MMFilenameValue *scratch);
int mm_filename_up(const MMFilenameValue *, const MMFilenamePathHost *, const MMStringValue *leaf, MMU32 flags);
MMU32 mm_filename_volume(const MMFilenameValue *, const MMFilenamePathHost *);
#ifdef __cplusplus
}
#endif
#endif
