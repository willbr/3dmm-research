/* Filename case resolution converted from Kauai filename.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_FILENAME_CASE_H
#define MM_ENGINE_FILENAME_CASE_H
#include "filename_path.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MM_FILENAME_CASE_NAME_CAPACITY 1024
enum { MM_FILENAME_CASE_ENTRY, MM_FILENAME_CASE_END, MM_FILENAME_CASE_ERROR };
typedef struct MMFilenameCaseHost {
    MMFilenamePathHost path;
    int sensitive;
    int (*first)(void *, const void *pattern, void **iterator, void *name, MMI32 capacity);
    int (*next)(void *, void *iterator, void *name, MMI32 capacity);
    void (*close)(void *, void *iterator);
    MMI32 (*name_length)(void *, const void *name);
} MMFilenameCaseHost;
/* Storage/value/diagnostic contracts follow filename_path.h. On native found,
 * return success unchanged, ignoring directory/hidden/system attributes. Other
 * lookup failures preserve the value and add no error. With sensitive zero,
 * missing also fails immediately. Otherwise require an initial '/', then prefer
 * each exact component or one unambiguous ASCII-only folded directory match.
 * Splitting uses '/' literally. Non-ASCII units and backslashes stay literal.
 * Publish path/type only after all components succeed. Close once after every
 * first attempt, including end/error/null handles; stop after a second match.
 * first/next return the iteration enum, writing a terminated raw name of at
 * most capacity-1 units on entry. Capacity is 1024 native units, all matching
 * the owner's unit width; names longer than a component must not be truncated.
 * Optional name_length retains native terminated-String diagnostics, returning
 * the actual untruncated length; a null hook uses C zero-length scanning.
 * Metadata is unused. Providers use path.context and own iterator resources
 * until close. Callbacks cannot unwind, destroy storage or mutate the operation.
 * Diagnostic/copy hooks can also receive temporary String buffers. No heap
 * allocation, native types or hidden globals. Callers serialize owner mutation. */
int mm_filename_resolve_case(const MMFilenameValue *, const MMFilenameCaseHost *);
#ifdef __cplusplus
}
#endif
#endif
