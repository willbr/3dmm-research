/* HQ handle policy converted from Kauai memwin.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_HANDLE_MEMORY_H
#define MM_ENGINE_HANDLE_MEMORY_H
#include "memory.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMHandleState { MMMemoryCounter bytes, locks; } MMHandleState;
typedef struct MMHandleDebugRecord { MMHandleState state; MMMemoryCounter magic; } MMHandleDebugRecord;
typedef struct MMHandleLayout {
    MMU32 prefix, limit;
    MMMemoryCounter magic;
    int debug, expensive;
} MMHandleLayout;
enum {
    MM_HANDLE_SMALL = 1, MM_HANDLE_BIG, MM_HANDLE_RESIZE_LOCKED, MM_HANDLE_FREE_LOCKED,
    MM_HANDLE_LOCK_OVERFLOW, MM_HANDLE_NOT_LOCKED, MM_HANDLE_NIL, MM_HANDLE_MAGIC
};
typedef struct MMHandleHost {
    void *context;
    int (*allocate)(void *, void **raw, MMU32 bytes, MMU32 flags, MMI32 priority);
    int (*resize)(void *, void **raw, MMU32 new_bytes, MMU32 old_bytes, MMU32 flags, MMI32 priority);
    void (*release)(void *, void **raw);
    void (*address)(void *, const void *, size_t);
    void (*assertion)(void *, int code, const void *value, size_t);
    void (*validate_fixed)(void *, void *raw, MMI32 bytes);
    void (*mark_fixed)(void *, void *raw);
    void (*failed)(void *);
} MMHandleHost;
/* A handle is the payload address after a private host-aligned prefix. Prefix
 * covers the C state (debug record when enabled), preserves payload alignment,
 * and stays fixed for the allocation's lifetime. Sizes and prefix-plus-payload
 * totals fit nonnegative signed 32-bit diagnostics; overflow fails explicitly.
 * limit governs debug diagnostics, not the fixed provider's allocation limit.
 * State lives directly in the allocation. No C++ object or wire record is stored.
 * Raw providers use compatible storage, preserve failed resize and header bytes,
 * and clear requested allocation/growth (flags bit 1). Null providers use libc.
 * Hooks borrow synchronous views, cannot unwind or destroy the operation, and
 * may observe publication: allocation failure reports before clearing the public
 * owner; failed resize retains it; free clears it before lock diagnostics/release.
 * Locked resize fails without a raw call; free reports but still releases a locked
 * block. Unlock at zero reports and remains zero. Caller supplies synchronization.
 * Copy allocates distinct storage without locking its source; output must not own
 * another allocation. Expensive debug validation checks magic, locks and fixed
 * storage. Marking skips nil and forwards the raw fixed block in debug mode. */
int mm_handle_allocate(const MMHandleLayout *, const MMHandleHost *, void **owner, MMU32 bytes, MMU32 flags, MMI32 priority);
int mm_handle_resize(const MMHandleLayout *, const MMHandleHost *, void **owner, MMU32 bytes, MMU32 flags, MMI32 priority);
void mm_handle_free(const MMHandleLayout *, const MMHandleHost *, void **owner);
int mm_handle_copy(const MMHandleLayout *, const MMHandleHost *, void *source, void **destination, MMI32 priority);
MMI32 mm_handle_size(const MMHandleLayout *, const MMHandleHost *, void *);
void *mm_handle_pointer(const MMHandleLayout *, const MMHandleHost *, void *);
void *mm_handle_lock(const MMHandleLayout *, const MMHandleHost *, void *);
void mm_handle_unlock(const MMHandleLayout *, const MMHandleHost *, void *);
void mm_handle_validate(const MMHandleLayout *, const MMHandleHost *, void *);
void mm_handle_mark(const MMHandleLayout *, const MMHandleHost *, void *);
#ifdef __cplusplus
}
#endif
#endif
