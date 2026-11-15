/* Template construction/header/default costumes converted from engine tmpl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_TEMPLATE_CREATE_H
#define MM_ENGINE_TEMPLATE_CREATE_H
#include "types.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum MMTemplateChunk
{
    MM_TMPL=0x544d504c, MM_TDT=0x54445420, MM_GLPI=0x474c5049,
    MM_GLBS=0x474c4253, MM_GGCM=0x4747434d, MM_CMTL=0x434d544c,
    MM_ACTN=0x4143544e, MM_GLDC=0x474c4443
};
enum MMTemplateTable { MM_TEMPLATE_PARENTS, MM_TEMPLATE_SETS, MM_TEMPLATE_COSTUMES };
enum MMTemplateDiagnostic
{ MM_TEMPLATE_BAD_ORDER, MM_TEMPLATE_OBSOLETE, MM_TEMPLATE_BUILD_COSTUMES,
  MM_TEMPLATE_COSTUME_LIMIT, MM_TEMPLATE_WRONG_SET };
typedef struct MMTemplateState
{
    void *parents,*sets,*costumes;
    MMI32 set_count,custom_count,action_count;
    MMU32 x,y,z,flags;
} MMTemplateState;
typedef struct MMTemplateChild { MMU32 type,number,id; } MMTemplateChild;
typedef struct MMTemplateCreateServices
{
    void *context;
    int host_big_endian,debug_checks;
    MMU32 system;
    void (*read)(void *,void *object,MMTemplateState *);
    void (*write)(void *,void *object,const MMTemplateState *);
    void (*release)(void *,void *);
    int (*child)(void *,void *file,MMU32,MMU32,MMU32 id,MMU32 type,MMTemplateChild *);
    int (*enumerate)(void *,void *file,MMU32,MMU32,MMI32,MMTemplateChild *);
    void *(*find)(void *,void *file,MMU32,MMU32);
    int (*unpack)(void *,void *block);
    MMI32 (*block_size)(void *,void *block,int packed);
    int (*block_read)(void *,void *block,void *,size_t);
    int (*read_name)(void *,void *object,void *file,MMU32,MMU32);
    int (*add_chunk)(void *,void *file,MMU32,const void *,size_t,MMU32 *);
    int (*set_name)(void *,void *object,void *file,MMU32,MMU32);
    void (*delete_chunk)(void *,void *file,MMU32,MMU32);
    void *(*create)(void *,int text);
    int (*initialize)(void *,void *object,void *file,MMU32,MMU32);
    MMI32 (*object_size)(void *,int text);
    MMI32 (*table_size)(void *,void *);
    void (*invalidate)(void *,void *,size_t);
    int (*header)(void *,void *object,void *file,MMU32,MMU32);
    void *(*read_table)(void *,void *block,int kind,int *other);
    void (*validate_table)(void *,void *,int kind,MMI32 expected);
    MMI32 (*count)(void *,void *list);
    MMI32 (*item)(void *,void *list,MMI32);
    unsigned char *(*list_data)(void *,void *list);
    unsigned char *(*fixed_data)(void *,void *group,MMI32);
    unsigned char *(*variable_data)(void *,void *group,MMI32);
    void *(*create_cache)(void *,void *file);
    void *(*create_costumes)(void *);
    void *(*fetch_custom)(void *,void *cache,MMU32,MMU32);
    MMI32 (*custom_set)(void *,void *custom);
    int (*append_costumes)(void *,void *group,const MMI32 *,MMI32 count);
    void *(*create_body)(void *,void *parents,void *sets);
    int (*default_costume)(void *,void *object,void *body);
    MMI32 (*costume_id)(void *,void *object,MMI32 set,MMI32 index);
    void *(*custom)(void *,void *object,MMI32 id);
    int (*allocate_refs)(void *,MMI32 count,void **);
    void (*apply_custom)(void *,void *body,void *custom);
    void (*free_memory)(void *,void **);
    void (*diagnostic)(void *,int);
} MMTemplateCreateServices;
/* Initialization expects fresh/empty owners. Groups and header fields publish
 * as the native operation reaches them; direct-call failure does not unwind.
 * Factories release partial objects. Virtual text initialization/default-costume
 * dispatch is preserved. Group counts/allocation products/cache estimates fit
 * signed 32-bit storage. Callbacks do not reenter or mutate unrelated state. */
/* The legacy GGCM builder had 50 stack slots per set. Counts through 50 retain
 * that behavior; a 51st match now diagnoses/fails instead of overflowing. */
int mm_template_read_header(void *,void *file,MMU32,MMU32,const MMTemplateCreateServices *);
/* Header padding is defined as zero; the native writer left two stack bytes
 * undefined. Name assignment deliberately uses TMPL even for another supplied
 * chunk type; failure deletes the added chunk and retains its published number. */
int mm_template_write_header(void *,void *file,MMU32,MMU32 *,const MMTemplateCreateServices *);
int mm_template_fetch(void *file,MMU32,MMU32,void *block,void **object,MMI32 *bytes,const MMTemplateCreateServices *);
int mm_template_initialize(void *,void *file,MMU32,MMU32,const MMTemplateCreateServices *);
void mm_template_dispose(void *,const MMTemplateCreateServices *);
void *mm_template_create_body(void *,const MMTemplateCreateServices *);
/* Fetch every default custom material before applying any; failure releases
 * fetched references without changing the body. All sets have a first ID. */
int mm_template_default_costume(void *,void *body,const MMTemplateCreateServices *);
#ifdef __cplusplus
}
#endif
#endif
