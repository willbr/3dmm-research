/* Filename enumeration converted from Kauai filename.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_FILENAME_ENUM_H
#define MM_ENGINE_FILENAME_ENUM_H
#include "filename_case.h"
#include "collections.h"
#ifdef __cplusplus
extern "C" {
#endif
#define MM_FILENAME_ENUM_INLINE 20
#define MM_FILENAME_ENUM_RECURSE 1UL
#define MM_FILENAME_ENUM_PRE 16UL
#define MM_FILENAME_ENUM_POST 32UL
#define MM_FILENAME_ENUM_SKIP 128UL
enum { MM_FILENAME_ENUM_UNINITIALIZED = 1, MM_FILENAME_ENUM_BUILD, MM_FILENAME_ENUM_LEAF, MM_FILENAME_ENUM_POP };
typedef struct MMFilenameEnumInfo { int directory, hidden, system, link; } MMFilenameEnumInfo;
typedef struct MMFilenameEnumCursor {
    void *iterator;
    MMU32 roots;
    MMI32 root_character;
    MMFilenameEnumInfo info;
} MMFilenameEnumCursor;
typedef struct MMFilenameEnumFrame {
    void *record;
    MMFilenameEnumCursor *cursor;
    MMFilenameValue name;
    void *entry; /* Raw 1024-unit terminated name. */
} MMFilenameEnumFrame;
typedef struct MMFilenameEnumState {
    MMI32 inline_types[MM_FILENAME_ENUM_INLINE], *types, type_count;
    MMStorageAllocator type_allocator;
    MMCollectionStorage stack;
    MMU32 frame_bytes, depth;
    int initialized, recurse, stack_ready, trash_unused;
} MMFilenameEnumState;
typedef struct MMFilenameEnumHost {
    MMFilenamePathHost path;
    void *context;
    int (*first)(void *, const void *pattern, void **iterator, void *name, MMI32 capacity, MMFilenameEnumInfo *);
    int (*next)(void *, void *iterator, void *name, MMI32 capacity, MMFilenameEnumInfo *);
    void (*close)(void *, void *iterator);
    MMU32 (*roots)(void *);
    int (*root_path)(void *, unsigned index, void *name, MMI32 capacity);
    MMI32 (*name_length)(void *, const void *name);
    void (*validate)(void *, int base_only);
    void (*issue)(void *);
    void (*assertion)(void *, int code);
} MMFilenameEnumHost;
/* Owns filters and recursion storage. Construct once at a fixed address; do not
 * copy this record. Free releases allocations/iterators and permits reinit.
 * Input filters are native 32-bit signed words copied before backward cleaning;
 * they cannot alias state/frame storage. Counts with overflowing byte products
 * are outside the native contract and fail safely. NULL allocators use C heap.
 * A frame record is a host-defined byte snapshot containing cursor, filename
 * storage and entry buffer; it has frame_bytes bytes, no independent ownership
 * and supports raw copies as the legacy FES did. Frame views remain at fixed
 * addresses after pop. Name/entry units match. Output/scratch names are distinct
 * from each other and the frame. Rebuild external views after moving storage.
 * Providers follow filename_path.h/filename_case.h; enumeration fills metadata
 * too. name_length is optional and retains native String diagnostics. Enumeration
 * providers use context; path providers use their own contexts. Hooks cannot
 * unwind, reenter or mutate the operation. Callers serialize state/frame access.
 * Filters gate descent too. Roots bypass filters. Skip closes/pops without a
 * post event. Hidden/system entries are skipped before output copy; dot entries
 * and filtered values can still change the output on a false result. Flags stay
 * unchanged on false. Optional flags points to a native 32-bit unsigned word;
 * byte publication avoids aliasing native long/int profiles. Post events precede pop. Linked directories are emitted
 * without descent. Recursion failure emits the directory with PRE|POST; failed
 * wildcard setup restores the parent without closing its iterator. */
int mm_filename_enum_construct(MMFilenameEnumState *, MMU32 frame_bytes, const MMStorageAllocator *types,
                                const MMStorageAllocator *stack, int trash_unused);
void mm_filename_enum_free(MMFilenameEnumState *, const MMFilenameEnumHost *, const MMFilenameEnumFrame *);
int mm_filename_enum_init(MMFilenameEnumState *, const MMFilenameEnumHost *, const MMFilenameEnumFrame *,
                           const MMFilenameValue *directory, const void *types, MMI32 count, MMU32 flags);
int mm_filename_enum_next(MMFilenameEnumState *, const MMFilenameEnumHost *, const MMFilenameEnumFrame *,
                           const MMFilenameValue *output, void *flags, MMU32 input_flags, const MMFilenameValue *scratch);
#ifdef __cplusplus
}
#endif
#endif
