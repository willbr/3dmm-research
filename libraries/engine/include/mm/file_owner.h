/* File lifecycle and I/O policy converted from Kauai file.cpp/file_native.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_FILE_OWNER_H
#define MM_ENGINE_FILE_OWNER_H
#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MM_FILE_WRITE 1UL
#define MM_FILE_DENY_WRITE 2UL
#define MM_FILE_DENY_READ 4UL
#define MM_FILE_PERMISSIONS 7UL
#define MM_FILE_TEMP 16UL
#define MM_FILE_MARK 32UL
enum { MM_FILE_ERROR_NONE, MM_FILE_ERROR_WRITE, MM_FILE_ERROR_READ, MM_FILE_ERROR_SEEK, MM_FILE_ERROR_CRITICAL };
enum { MM_FILE_ISSUE_GENERAL = 1, MM_FILE_ISSUE_PERMISSION, MM_FILE_ISSUE_DELETE };
enum { MM_FILE_CHECK_WRITE = 1, MM_FILE_CHECK_CREATE, MM_FILE_CHECK_EOF };
enum { MM_FILE_ARGUMENT_RESIZE = 1, MM_FILE_ARGUMENT_READ, MM_FILE_ARGUMENT_WRITE };
typedef struct MMFileOwner {
    MMU32 flags;
    MMI32 error;
    int open, ever_open, wrote;
    void *handle;
} MMFileOwner;
typedef struct MMFileOwnerHost {
    void *context;
    void (*enter)(void *);
    void (*leave)(void *);
    void *(*open)(void *, int create, MMU32 flags);
    void (*close)(void *, void *handle);
    int (*flush)(void *, void *handle);
    int (*remove)(void *);
    int (*seek)(void *, void *handle, MMU32 position);
    int (*resize)(void *, void *handle, MMU32 bytes);
    int (*size)(void *, void *handle, MMU32 *bytes);
    int (*read)(void *, void *handle, void *, MMU32 bytes);
    int (*write)(void *, void *handle, const void *, MMU32 bytes);
    void (*issue)(void *, int code);
    void (*validate)(void *, int base_only);
    void (*arguments)(void *, int operation, const void *, MMI32 bytes, MMI32 position);
    void (*assertion)(void *, int code);
    int debug_read;
} MMFileOwnerHost;
/* Initialize once before opening. The owner owns its handle, not its path or
 * host. Do not copy a live owner or reinitialize it before closing its handle.
 * Primitive callbacks bind a live native path via context. All callbacks
 * are synchronous; they cannot unwind, destroy the owner or mutate its state.
 * They may inspect publication state. Locks must be recursive: close calls flush,
 * reads optionally query size, and I/O can reopen. Seek assumes an entered lock.
 * Enter/leave and diagnostic hooks may be null; physical services are required.
 * Counts/positions are native nonnegative signed 32-bit values. Zero-byte I/O
 * succeeds without opening/locking, even at critical error. No transfer retries
 * occur. Failures report before raising error severity. Failed permission upgrade
 * reopens old permissions; double failure latches critical. Flags retain marks;
 * set_flags never clears a mark. Final close may delete an ever-opened temp path,
 * including repeated final-close attempts, and reports deletion failures.
 * Default init flags need not grant write permission: diagnostic rejection of a
 * write is advisory, preserving native behavior. Flush failure is ignored.
 * This record contains no reference count, registry links or name-swap policy. */
void mm_file_owner_init(MMFileOwner *, MMU32 flags);
int mm_file_owner_open(MMFileOwner *, const MMFileOwnerHost *, int create, MMU32 flags);
void mm_file_owner_close(MMFileOwner *, const MMFileOwnerHost *, int final);
void mm_file_owner_flush(MMFileOwner *, const MMFileOwnerHost *);
void mm_file_owner_seek(MMFileOwner *, const MMFileOwnerHost *, MMI32 position);
int mm_file_owner_resize(MMFileOwner *, const MMFileOwnerHost *, MMI32 bytes);
MMU32 mm_file_owner_size(MMFileOwner *, const MMFileOwnerHost *);
int mm_file_owner_read(MMFileOwner *, const MMFileOwnerHost *, void *, MMI32 bytes, MMI32 position);
int mm_file_owner_write(MMFileOwner *, const MMFileOwnerHost *, const void *, MMI32 bytes, MMI32 position);
int mm_file_owner_set_flags(MMFileOwner *, const MMFileOwnerHost *, MMU32 flags, MMU32 mask);
void mm_file_owner_set_temp(MMFileOwner *, const MMFileOwnerHost *, int temporary);
#ifdef __cplusplus
}
#endif
#endif
