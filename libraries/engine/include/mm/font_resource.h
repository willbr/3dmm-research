/* Three-dimensional font resources converted from engine tdf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_FONT_RESOURCE_H
#define MM_ENGINE_FONT_RESOURCE_H
#include "chunk_edit.h"
#include "resource_cache.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_FONT_TYPE=0x54444620 };
typedef struct MMFontResource
{
    MMI32 count,maximum_height;
    void *widths,*heights;
} MMFontResource;
typedef struct MMFontChunk { MMU32 type,number; } MMFontChunk;
typedef struct MMFontResourceServices
{
    void *context; int host_big_endian; MMU32 system;
    MMI32 spacing,leading,object_bytes;
    MMFontResource *(*state)(void *,void *object);
    void *(*create)(void *);
    void (*release)(void *,void *);
    MMBlockTransform decode;
    const MMStorageAllocator *allocator; /* NULL selects the C heap. */
    void (*invalidate)(void *,void *out,size_t);
    void (*bad_font)(void *);
    void (*bad_order)(void *);
    MMI32 (*child_count)(void *,void *children);
    void (*child_at)(void *,void *children,MMI32,MMChunkChild *);
    MMResourceBlockReader model_reader;
    MMI32 (*model_width)(void *,void *model);
    MMI32 (*model_height)(void *,void *model);
    void (*missing_model)(void *,MMU32 id);
} MMFontResourceServices;
/* Fresh owners are zeroed. Count publishes before length validation, maximum
 * after it, and each allocation before its read. Direct failure keeps partial
 * ownership; factory failure INVALIDATES outputs BEFORE releasing the object.
 * Font cache estimates use 256 pairs when packed and omit the header when final.
 * Native foreign metric arrays deliberately remain unswapped after header
 * normalization. Indices and fixed-point sums satisfy native preconditions.
 * Callbacks must not reenter or mutate unrelated owner state. */
int mm_font_fetch(MMOwnedBlock *,void *object_out,void *size_out,const MMFontResourceServices *);
int mm_font_initialize(MMFontResource *,MMOwnedBlock *,const MMFontResourceServices *);
/* Read decoded metrics and return a movie status. The caller owns allocations
 * published before a failure. */
int mm_font_read_stream(MMFontResource *,const MMReadStream *,const MMFontResourceServices *);
void mm_font_dispose(MMFontResource *,const MMFontResourceServices *);
MMI32 mm_font_width(const MMFontResource *,MMI32 index);
MMI32 mm_font_height(const MMFontResource *,MMI32 index);
typedef struct MMFontOwner MMFontOwner;
typedef struct MMFontOwnerHost
{
    const MMStorageAllocator *objects, *metrics;
    MMBlockTransform decode;
    void (*mark_memory)(void *);
    void (*bad_font)(void *), (*bad_order)(void *);
    void (*invalidate)(void *, void *out, size_t bytes);
} MMFontOwnerHost;
/* Cached fonts borrow the allocators for their lifetime. NULL allocators use
 * the C heap. The font metrics remain valid while the owner is retained. */
int mm_font_owner_read(MMOwnedBlock *, void **object, MMI32 *bytes, const MMFontOwnerHost *);
const MMResourceObjectServices *mm_font_owner_services(void);
const MMFontResource *mm_font_owner_metrics(const MMFontOwner *);
void mm_font_owner_release(MMFontOwner *);
void *mm_font_owner_model(const MMFontOwner *, MMU32 id, const MMFontResourceServices *);
/* The editor borrows the cache's live archive. Authoring leaves partially
 * created chunks/adoptions on failure, as native.
 * Name input was ignored by native; this API has no name parameter. Empty input
 * creates one zero-metric slot. A zero-width space borrows dimensions from the
 * last 'i' entry. Failure releases the currently fetched model (native leaked
 * it on adoption failure), frees arrays, then invalidates the optional 8-byte
 * chunk output. Success publishes output after freeing both arrays. */
int mm_font_author(MMResourceCache *,const MMChunkEdit *,void *children,void *chunk_out,const MMFontResourceServices *);
/* Child lookup deliberately does not filter chunk type. */
void *mm_font_model(const MMResourceObject *,MMU32 id,const MMFontResourceServices *);
#ifdef __cplusplus
}
#endif
#endif
