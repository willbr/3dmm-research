/* Template construction/header/default costumes converted from engine tmpl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_TEMPLATE_CREATE_H
#define THREE_MM_ENGINE_TEMPLATE_CREATE_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum ThreeMMTemplateChunk
{
    THREE_MM_TMPL=0x544d504c, THREE_MM_TDT=0x54445420, THREE_MM_GLPI=0x474c5049,
    THREE_MM_GLBS=0x474c4253, THREE_MM_GGCM=0x4747434d, THREE_MM_CMTL=0x434d544c,
    THREE_MM_ACTN=0x4143544e, THREE_MM_GLDC=0x474c4443
};
enum ThreeMMTemplateTable { THREE_MM_TEMPLATE_PARENTS, THREE_MM_TEMPLATE_SETS, THREE_MM_TEMPLATE_COSTUMES };
enum ThreeMMTemplateDiagnostic
{ THREE_MM_TEMPLATE_BAD_ORDER, THREE_MM_TEMPLATE_OBSOLETE, THREE_MM_TEMPLATE_BUILD_COSTUMES,
  THREE_MM_TEMPLATE_COSTUME_LIMIT, THREE_MM_TEMPLATE_WRONG_SET };
typedef struct ThreeMMTemplateState
{
    void *parents,*sets,*costumes;
    ThreeMMI32 set_count,custom_count,action_count;
    ThreeMMU32 x,y,z,flags;
} ThreeMMTemplateState;
typedef struct ThreeMMTemplateChild { ThreeMMU32 type,number,id; } ThreeMMTemplateChild;
typedef struct ThreeMMTemplateCreateServices
{
    void *context;
    int host_big_endian,debug_checks;
    ThreeMMU32 system;
    void (*read)(void *,void *object,ThreeMMTemplateState *);
    void (*write)(void *,void *object,const ThreeMMTemplateState *);
    void (*release)(void *,void *);
    int (*child)(void *,void *file,ThreeMMU32,ThreeMMU32,ThreeMMU32 id,ThreeMMU32 type,ThreeMMTemplateChild *);
    int (*enumerate)(void *,void *file,ThreeMMU32,ThreeMMU32,ThreeMMI32,ThreeMMTemplateChild *);
    void *(*find)(void *,void *file,ThreeMMU32,ThreeMMU32);
    int (*unpack)(void *,void *block);
    ThreeMMI32 (*block_size)(void *,void *block,int packed);
    int (*block_read)(void *,void *block,void *,size_t);
    int (*read_name)(void *,void *object,void *file,ThreeMMU32,ThreeMMU32);
    int (*add_chunk)(void *,void *file,ThreeMMU32,const void *,size_t,ThreeMMU32 *);
    int (*set_name)(void *,void *object,void *file,ThreeMMU32,ThreeMMU32);
    void (*delete_chunk)(void *,void *file,ThreeMMU32,ThreeMMU32);
    void *(*create)(void *,int text);
    int (*initialize)(void *,void *object,void *file,ThreeMMU32,ThreeMMU32);
    ThreeMMI32 (*object_size)(void *,int text);
    ThreeMMI32 (*table_size)(void *,void *);
    void (*invalidate)(void *,void *,size_t);
    int (*header)(void *,void *object,void *file,ThreeMMU32,ThreeMMU32);
    void *(*read_table)(void *,void *block,int kind,int *other);
    void (*validate_table)(void *,void *,int kind,ThreeMMI32 expected);
    ThreeMMI32 (*count)(void *,void *list);
    ThreeMMI32 (*item)(void *,void *list,ThreeMMI32);
    unsigned char *(*list_data)(void *,void *list);
    unsigned char *(*fixed_data)(void *,void *group,ThreeMMI32);
    unsigned char *(*variable_data)(void *,void *group,ThreeMMI32);
    void *(*create_cache)(void *,void *file);
    void *(*create_costumes)(void *);
    void *(*fetch_custom)(void *,void *cache,ThreeMMU32,ThreeMMU32);
    ThreeMMI32 (*custom_set)(void *,void *custom);
    int (*append_costumes)(void *,void *group,const ThreeMMI32 *,ThreeMMI32 count);
    void *(*create_body)(void *,void *parents,void *sets);
    int (*default_costume)(void *,void *object,void *body);
    ThreeMMI32 (*costume_id)(void *,void *object,ThreeMMI32 set,ThreeMMI32 index);
    void *(*custom)(void *,void *object,ThreeMMI32 id);
    int (*allocate_refs)(void *,ThreeMMI32 count,void **);
    void (*apply_custom)(void *,void *body,void *custom);
    void (*free_memory)(void *,void **);
    void (*diagnostic)(void *,int);
} ThreeMMTemplateCreateServices;
/* Initialization expects fresh/empty owners. Groups and header fields publish
 * as the native operation reaches them; direct-call failure does not unwind.
 * Factories release partial objects. Virtual text initialization/default-costume
 * dispatch is preserved. Group counts/allocation products/cache estimates fit
 * signed 32-bit storage. Callbacks do not reenter or mutate unrelated state. */
/* The legacy GGCM builder had 50 stack slots per set. Counts through 50 retain
 * that behavior; a 51st match now diagnoses/fails instead of overflowing. */
int three_mm_template_read_header(void *,void *file,ThreeMMU32,ThreeMMU32,const ThreeMMTemplateCreateServices *);
/* Header padding is defined as zero; the native writer left two stack bytes
 * undefined. Name assignment deliberately uses TMPL even for another supplied
 * chunk type; failure deletes the added chunk and retains its published number. */
int three_mm_template_write_header(void *,void *file,ThreeMMU32,ThreeMMU32 *,const ThreeMMTemplateCreateServices *);
int three_mm_template_fetch(void *file,ThreeMMU32,ThreeMMU32,void *block,void **object,ThreeMMI32 *bytes,const ThreeMMTemplateCreateServices *);
int three_mm_template_initialize(void *,void *file,ThreeMMU32,ThreeMMU32,const ThreeMMTemplateCreateServices *);
void three_mm_template_dispose(void *,const ThreeMMTemplateCreateServices *);
void *three_mm_template_create_body(void *,const ThreeMMTemplateCreateServices *);
/* Fetch every default custom material before applying any; failure releases
 * fetched references without changing the body. All sets have a first ID. */
int three_mm_template_default_costume(void *,void *body,const ThreeMMTemplateCreateServices *);
#ifdef __cplusplus
}
#endif
#endif
