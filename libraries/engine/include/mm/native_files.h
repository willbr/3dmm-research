/* Native file services converted from Kauai platform_files_win.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_NATIVE_FILES_H
#define MM_ENGINE_NATIVE_FILES_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifdef UNICODE
typedef unsigned short MMNativePathChar;
#else
typedef char MMNativePathChar;
#endif
enum { MM_NATIVE_OPEN_EXISTING, MM_NATIVE_CREATE_ALWAYS, MM_NATIVE_CREATE_EXCLUSIVE };
enum { MM_NATIVE_FOUND, MM_NATIVE_MISSING, MM_NATIVE_ERROR };
enum { MM_NATIVE_ENTRY, MM_NATIVE_END, MM_NATIVE_ITERATION_ERROR };
typedef struct MMNativeOpenOptions { int write, deny_read, deny_write, creation; } MMNativeOpenOptions;
typedef struct MMNativePathInfo { int directory, hidden, system, read_only, link; } MMNativePathInfo;
/* Windows implementation: ANSI or UTF-16 native paths selected by UNICODE.
 * All translation units sharing an instance use the same selection. Handles
 * are opaque owned OS resources; close once. Directory names borrow at least
 * 1024 native units; first/next publish SDK name bytes and metadata on entry.
 * Sizes/seek/resize stop at signed 32-bit; I/O uses unsigned 32-bit byte counts.
 * I/O requires exact byte
 * transfers, with no retries. Failed resize may move the Windows file position.
 * Size failure leaves its output untouched. Info failure leaves metadata alone.
 * Native no-clobber/sharing/read-only and missing-parent classification remain.
 * Path outputs/leaf/error publication retain the original SDK wrapper rules;
 * current/executable/leaf queries clear nonempty output on failure, whereas
 * full/search/temp preserve the underlying SDK output. Inputs are readable
 * terminated native strings; output capacity is in units, including terminator.
 * Invalid arguments are supported only where explicitly checked by the wrapper.
 * Lookup input/output do not overlap. Native process cwd/error-mode changes are
 * process-wide and require caller coordination. Make-directories uses C heap
 * storage and the existing ownerless shell call. No C++ types or allocations.
 * POSIX implementation remains a separate migration boundary. */
void mm_native_open_options_init(MMNativeOpenOptions *);
void *mm_native_file_open(const MMNativePathChar *, const MMNativeOpenOptions *);
void mm_native_file_close(void *);
int mm_native_file_flush(void *);
int mm_native_file_seek(void *, MMU32);
int mm_native_file_resize(void *, MMU32);
int mm_native_file_size(void *, MMU32 *);
int mm_native_file_read(void *, void *, MMU32);
int mm_native_file_write(void *, const void *, MMU32);
int mm_native_path_info(const MMNativePathChar *, MMNativePathInfo *);
int mm_native_path_delete(const MMNativePathChar *);
int mm_native_path_rename(const MMNativePathChar *, const MMNativePathChar *);
int mm_native_make_directory(const MMNativePathChar *);
int mm_native_make_directories(const MMNativePathChar *);
int mm_native_delete_directory(const MMNativePathChar *);
int mm_native_paths_equal(const MMNativePathChar *, const MMNativePathChar *);
int mm_native_is_separator(MMNativePathChar);
MMNativePathChar mm_native_separator(void);
size_t mm_native_full_path(const MMNativePathChar *, MMNativePathChar *, size_t, MMNativePathChar **);
size_t mm_native_find_path(const MMNativePathChar *, const MMNativePathChar *, MMNativePathChar *, size_t);
size_t mm_native_temp_path(MMNativePathChar *, size_t);
size_t mm_native_current_directory(MMNativePathChar *, size_t);
int mm_native_change_directory(const MMNativePathChar *, MMU32 *);
size_t mm_native_executable_path(MMNativePathChar *, size_t);
size_t mm_native_lookup_leaf(const MMNativePathChar *, MMNativePathChar *, size_t);
MMU32 mm_native_root_mask(void);
int mm_native_root_path(unsigned, MMNativePathChar *, size_t);
MMU32 mm_native_volume_kinds(const MMNativePathChar *);
MMU32 mm_native_suppress_media_errors(void);
void mm_native_restore_media_errors(MMU32);
int mm_native_directory_first(const MMNativePathChar *, void **, MMNativePathChar *, MMNativePathInfo *);
int mm_native_directory_next(void *, MMNativePathChar *, MMNativePathInfo *);
void mm_native_directory_close(void *);
#ifdef __cplusplus
}
#endif
#endif
