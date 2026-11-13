/* Three-dimensional text resources converted from engine tdt.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_TEXT_RESOURCE_H
#define THREE_MM_ENGINE_TEXT_RESOURCE_H
#include "text_generation.h"
#include "tag.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMTextDescriptor { ThreeMMI32 shape; ThreeMMTag font; } ThreeMMTextDescriptor;
typedef struct ThreeMMTextNames { void *table; } ThreeMMTextNames;
typedef struct ThreeMMTextResourceServices
{
    void *context;
    int host_big_endian; ThreeMMU32 system;
    ThreeMMTextGenerationServices generation;
    ThreeMMTextDescriptor *(*state)(void *,void *object);
    void *(*create)(void *);
    void *(*text)(void *,void *object);
    void (*assign)(void *,void *destination,const void *source);
    void (*space)(void *,void *text);
    void (*clear)(void *,void *text);
    void *(*saved_text)(void *);
    void (*provider_copy)(void *,void *destination,void *source);
    int (*has_provider)(void *,void *object);
    void *(*provider_font)(void *,void *object,const ThreeMMTag *);
    void *(*default_font)(void *,const ThreeMMTag *);
    void (*publish_tag)(void *,void *output,const ThreeMMTag *);
    int (*read_template)(void *,void *object,void *file,ThreeMMU32 type,ThreeMMU32 number);
    int (*write_template)(void *,void *object,void *file,ThreeMMU32 type,void *number_out);
    int (*child)(void *,void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 *child_number);
    void *(*find)(void *,void *file,ThreeMMU32 number);
    int (*unpack)(void *,void *block);
    ThreeMMI32 (*block_size)(void *,void *block);
    int (*read)(void *,void *block,void *data,ThreeMMI32 bytes);
    void *(*add)(void *,void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 *child_number);
    int (*write)(void *,void *block,const void *data,ThreeMMI32 bytes);
    void (*delete_child)(void *,void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 child_number);
    void (*publish_error)(void *,void *output,int value);
    void *(*tag_list)(void *);
    int (*append_tag)(void *,void *list,const ThreeMMTag *);
    ThreeMMI32 (*name_count)(void *,void *table);
    ThreeMMI32 (*name_id)(void *,void *table,ThreeMMI32 index);
    void (*name_text)(void *,void *table,ThreeMMI32 index,void *text);
    void (*missing_name)(void *);
    void (*bad_order)(void *);
} ThreeMMTextResourceServices;
/* Text/tag storage is borrowed. Runtime cache bits survive copying and rollback;
 * on-file tags discard their pointer pad and load a NULL cache. Creation/change
 * normalize empty text to a space, loading does not. Even a no-op change rebuilds
 * generated owners. Failure restores text, shape, then font; generated owners
 * are replaced atomically by text_generation. Duplicate providers copy only
 * after successful fresh construction. Callbacks must not reenter owners. */
void *three_mm_text_create(const void *text,ThreeMMI32 shape,const ThreeMMTag *,const ThreeMMTextResourceServices *);
void *three_mm_text_duplicate(void *object,const ThreeMMTextResourceServices *);
int three_mm_text_change(void *object,const void *text,ThreeMMI32 shape,const ThreeMMTag *,const ThreeMMTextResourceServices *);
void three_mm_text_info(void *object,void *text,void *shape,void *font,const ThreeMMTextResourceServices *);
void *three_mm_text_font(void *object,const ThreeMMTextResourceServices *);
/* Read publishes base-template fields first, then font and shape, retaining
 * partial state on direct failure. Tag enumeration clears error before allocation
 * and publishes failure before releasing the list. The 24-byte record allows
 * trailing bytes and swaps whenever its marker differs from the native marker.
 * Write keeps the parent/output number on child failure, deletes only the child
 * on payload-write failure, and always writes a zero pointer pad. */
int three_mm_text_read(void *object,void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMTextResourceServices *);
int three_mm_text_write(void *object,void *file,ThreeMMU32 type,void *number_out,const ThreeMMTextResourceServices *);
void *three_mm_text_tags(void *file,ThreeMMU32 type,ThreeMMU32 number,void *error_out,const ThreeMMTextResourceServices *);
/* Set-once table publishes before retain. Lookup takes the first matching ID;
 * an absent table clears silently, an absent entry warns before clearing. */
int three_mm_text_set_names(ThreeMMTextNames *,void *table,const ThreeMMTextResourceServices *);
int three_mm_text_action_name(const ThreeMMTextNames *,ThreeMMI32 id,void *text,const ThreeMMTextResourceServices *);
#ifdef __cplusplus
}
#endif
#endif
