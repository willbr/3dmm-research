/* Three-dimensional font resources converted from engine tdf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_FONT_RESOURCE_H
#define MM_ENGINE_FONT_RESOURCE_H
#include "types.h"
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
typedef struct MMFontChild { MMU32 type,number,id; } MMFontChild;
typedef struct MMFontChunk { MMU32 type,number; } MMFontChunk;
typedef struct MMFontResourceServices
{
    void *context; int host_big_endian; MMU32 system;
    MMI32 spacing,leading;
    MMFontResource *(*state)(void *,void *object);
    void *(*create)(void *);
    void (*release)(void *,void *);
    int (*initialize)(void *,void *object,void *block);
    MMI32 (*object_size)(void *);
    int (*packed)(void *,void *block);
    int (*unpack)(void *,void *block);
    MMI32 (*block_size)(void *,void *block);
    int (*read)(void *,void *block,void *,MMI32 bytes,MMI32 offset);
    int (*write)(void *,void *block,const void *,MMI32 bytes,MMI32 offset);
    int (*allocate)(void *,MMI32 bytes,int clear,void **out);
    void (*free_memory)(void *,void **);
    void (*invalidate)(void *,void *out,size_t);
    void (*bad_font)(void *);
    void (*bad_order)(void *);
    MMI32 (*child_count)(void *,void *children);
    void (*child_at)(void *,void *children,MMI32,MMFontChild *);
    void *(*file)(void *,void *cache);
    void *(*add)(void *,void *file,MMI32 bytes,MMU32 type,MMU32 *number);
    void *(*fetch_model)(void *,void *cache,MMU32 type,MMU32 number);
    int (*adopt)(void *,void *file,MMU32 type,MMU32 number,const MMFontChild *);
    MMI32 (*model_width)(void *,void *model);
    MMI32 (*model_height)(void *,void *model);
    void (*binding)(void *,void *object,void **cache,MMFontChunk *);
    int (*child_id)(void *,void *file,MMU32 type,MMU32 number,MMU32 id,MMFontChild *);
    void (*missing_model)(void *,MMU32 id);
} MMFontResourceServices;
/* Fresh owners are zeroed. Count publishes before length validation, maximum
 * after it, and each allocation before its read. Direct failure keeps partial
 * ownership; factory failure INVALIDATES outputs BEFORE releasing the object.
 * Font cache estimates use 256 pairs when packed and omit the header when final.
 * Native foreign metric arrays deliberately remain unswapped after header
 * normalization. Indices and fixed-point sums satisfy native preconditions.
 * Callbacks must not reenter or mutate unrelated owner state. */
int mm_font_fetch(void *block,void *object_out,void *size_out,const MMFontResourceServices *);
int mm_font_initialize(MMFontResource *,void *block,const MMFontResourceServices *);
void mm_font_dispose(MMFontResource *,const MMFontResourceServices *);
MMI32 mm_font_width(const MMFontResource *,MMI32 index);
MMI32 mm_font_height(const MMFontResource *,MMI32 index);
/* Authoring leaves partially created chunks/adoptions on failure, as native.
 * Name input was ignored by native; this API has no name parameter. Empty input
 * creates one zero-metric slot. A zero-width space borrows dimensions from the
 * last 'i' entry. Failure releases the currently fetched model (native leaked
 * it on adoption failure), frees arrays, then invalidates the optional 8-byte
 * chunk output. Success publishes output after freeing both arrays. */
int mm_font_author(void *cache,void *children,void *chunk_out,const MMFontResourceServices *);
/* Child lookup deliberately does not filter chunk type. */
void *mm_font_model(void *object,MMU32 id,const MMFontResourceServices *);
#ifdef __cplusplus
}
#endif
#endif
