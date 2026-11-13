/* Three-dimensional font resources converted from engine tdf.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_FONT_RESOURCE_H
#define THREE_MM_ENGINE_FONT_RESOURCE_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { THREE_MM_FONT_TYPE=0x54444620 };
typedef struct ThreeMMFontResource
{
    ThreeMMI32 count,maximum_height;
    void *widths,*heights;
} ThreeMMFontResource;
typedef struct ThreeMMFontChild { ThreeMMU32 type,number,id; } ThreeMMFontChild;
typedef struct ThreeMMFontChunk { ThreeMMU32 type,number; } ThreeMMFontChunk;
typedef struct ThreeMMFontResourceServices
{
    void *context; int host_big_endian; ThreeMMU32 system;
    ThreeMMI32 spacing,leading;
    ThreeMMFontResource *(*state)(void *,void *object);
    void *(*create)(void *);
    void (*release)(void *,void *);
    int (*initialize)(void *,void *object,void *block);
    ThreeMMI32 (*object_size)(void *);
    int (*packed)(void *,void *block);
    int (*unpack)(void *,void *block);
    ThreeMMI32 (*block_size)(void *,void *block);
    int (*read)(void *,void *block,void *,ThreeMMI32 bytes,ThreeMMI32 offset);
    int (*write)(void *,void *block,const void *,ThreeMMI32 bytes,ThreeMMI32 offset);
    int (*allocate)(void *,ThreeMMI32 bytes,int clear,void **out);
    void (*free_memory)(void *,void **);
    void (*invalidate)(void *,void *out,size_t);
    void (*bad_font)(void *);
    void (*bad_order)(void *);
    ThreeMMI32 (*child_count)(void *,void *children);
    void (*child_at)(void *,void *children,ThreeMMI32,ThreeMMFontChild *);
    void *(*file)(void *,void *cache);
    void *(*add)(void *,void *file,ThreeMMI32 bytes,ThreeMMU32 type,ThreeMMU32 *number);
    void *(*fetch_model)(void *,void *cache,ThreeMMU32 type,ThreeMMU32 number);
    int (*adopt)(void *,void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMFontChild *);
    ThreeMMI32 (*model_width)(void *,void *model);
    ThreeMMI32 (*model_height)(void *,void *model);
    void (*binding)(void *,void *object,void **cache,ThreeMMFontChunk *);
    int (*child_id)(void *,void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 id,ThreeMMFontChild *);
    void (*missing_model)(void *,ThreeMMU32 id);
} ThreeMMFontResourceServices;
/* Fresh owners are zeroed. Count publishes before length validation, maximum
 * after it, and each allocation before its read. Direct failure keeps partial
 * ownership; factory failure INVALIDATES outputs BEFORE releasing the object.
 * Font cache estimates use 256 pairs when packed and omit the header when final.
 * Native foreign metric arrays deliberately remain unswapped after header
 * normalization. Indices and fixed-point sums satisfy native preconditions.
 * Callbacks must not reenter or mutate unrelated owner state. */
int three_mm_font_fetch(void *block,void *object_out,void *size_out,const ThreeMMFontResourceServices *);
int three_mm_font_initialize(ThreeMMFontResource *,void *block,const ThreeMMFontResourceServices *);
void three_mm_font_dispose(ThreeMMFontResource *,const ThreeMMFontResourceServices *);
ThreeMMI32 three_mm_font_width(const ThreeMMFontResource *,ThreeMMI32 index);
ThreeMMI32 three_mm_font_height(const ThreeMMFontResource *,ThreeMMI32 index);
/* Authoring leaves partially created chunks/adoptions on failure, as native.
 * Name input was ignored by native; this API has no name parameter. Empty input
 * creates one zero-metric slot. A zero-width space borrows dimensions from the
 * last 'i' entry. Failure releases the currently fetched model (native leaked
 * it on adoption failure), frees arrays, then invalidates the optional 8-byte
 * chunk output. Success publishes output after freeing both arrays. */
int three_mm_font_author(void *cache,void *children,void *chunk_out,const ThreeMMFontResourceServices *);
/* Child lookup deliberately does not filter chunk type. */
void *three_mm_font_model(void *object,ThreeMMU32 id,const ThreeMMFontResourceServices *);
#ifdef __cplusplus
}
#endif
#endif
