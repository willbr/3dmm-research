/* Three-dimensional text resources converted from engine tdt.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_TEXT_RESOURCE_H
#define MM_ENGINE_TEXT_RESOURCE_H
#include "text_generation.h"
#include "tag.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMTextDescriptor { MMI32 shape; MMTag font; } MMTextDescriptor;
typedef struct MMTextNames { void *table; } MMTextNames;
typedef struct MMTextResourceServices
{
    void *context;
    int host_big_endian; MMU32 system;
    MMTextGenerationServices generation;
    MMTextDescriptor *(*state)(void *,void *object);
    void *(*create)(void *);
    void *(*text)(void *,void *object);
    void (*assign)(void *,void *destination,const void *source);
    void (*space)(void *,void *text);
    void (*clear)(void *,void *text);
    void *(*saved_text)(void *);
    void (*provider_copy)(void *,void *destination,void *source);
    int (*has_provider)(void *,void *object);
    void *(*provider_font)(void *,void *object,const MMTag *);
    void *(*default_font)(void *,const MMTag *);
    void (*publish_tag)(void *,void *output,const MMTag *);
    int (*read_template)(void *,void *object,void *file,MMU32 type,MMU32 number);
    int (*write_template)(void *,void *object,void *file,MMU32 type,void *number_out);
    int (*child)(void *,void *file,MMU32 type,MMU32 number,MMU32 *child_number);
    void *(*find)(void *,void *file,MMU32 number);
    int (*unpack)(void *,void *block);
    MMI32 (*block_size)(void *,void *block);
    int (*read)(void *,void *block,void *data,MMI32 bytes);
    void *(*add)(void *,void *file,MMU32 type,MMU32 number,MMU32 *child_number);
    int (*write)(void *,void *block,const void *data,MMI32 bytes);
    void (*delete_child)(void *,void *file,MMU32 type,MMU32 number,MMU32 child_number);
    void (*publish_error)(void *,void *output,int value);
    void *(*tag_list)(void *);
    int (*append_tag)(void *,void *list,const MMTag *);
    MMI32 (*name_count)(void *,void *table);
    MMI32 (*name_id)(void *,void *table,MMI32 index);
    void (*name_text)(void *,void *table,MMI32 index,void *text);
    void (*missing_name)(void *);
    void (*bad_order)(void *);
} MMTextResourceServices;
/* Text/tag storage is borrowed. Runtime cache bits survive copying and rollback;
 * on-file tags discard their pointer pad and load a NULL cache. Creation/change
 * normalize empty text to a space, loading does not. Even a no-op change rebuilds
 * generated owners. Failure restores text, shape, then font; generated owners
 * are replaced atomically by text_generation. Duplicate providers copy only
 * after successful fresh construction. Callbacks must not reenter owners. */
void *mm_text_create(const void *text,MMI32 shape,const MMTag *,const MMTextResourceServices *);
void *mm_text_duplicate(void *object,const MMTextResourceServices *);
int mm_text_change(void *object,const void *text,MMI32 shape,const MMTag *,const MMTextResourceServices *);
void mm_text_info(void *object,void *text,void *shape,void *font,const MMTextResourceServices *);
void *mm_text_font(void *object,const MMTextResourceServices *);
/* Read publishes base-template fields first, then font and shape, retaining
 * partial state on direct failure. Tag enumeration clears error before allocation
 * and publishes failure before releasing the list. The 24-byte record allows
 * trailing bytes and swaps whenever its marker differs from the native marker.
 * Write keeps the parent/output number on child failure, deletes only the child
 * on payload-write failure, and always writes a zero pointer pad. */
int mm_text_read(void *object,void *file,MMU32 type,MMU32 number,const MMTextResourceServices *);
int mm_text_write(void *object,void *file,MMU32 type,void *number_out,const MMTextResourceServices *);
void *mm_text_tags(void *file,MMU32 type,MMU32 number,void *error_out,const MMTextResourceServices *);
/* Set-once table publishes before retain. Lookup takes the first matching ID;
 * an absent table clears silently, an absent entry warns before clearing. */
int mm_text_set_names(MMTextNames *,void *table,const MMTextResourceServices *);
int mm_text_action_name(const MMTextNames *,MMI32 id,void *text,const MMTextResourceServices *);
#ifdef __cplusplus
}
#endif
#endif
