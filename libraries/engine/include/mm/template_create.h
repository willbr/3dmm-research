/* Template construction/header/default costumes converted from engine tmpl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_TEMPLATE_CREATE_H
#define MM_ENGINE_TEMPLATE_CREATE_H
#include "types.h"
#include "movie_data.h"
#include "collection_storage.h"
#include "part_indices.h"
#include "resource_cache.h"
#include <stddef.h>
#if USHRT_MAX != 65535U
#error Template rest angles require sixteen-bit unsigned shorts
#endif
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
enum { MM_TEMPLATE_TEXT = 2, MM_TEMPLATE_PROP = 4 };
enum MMTemplateDiagnostic
{ MM_TEMPLATE_BAD_ORDER, MM_TEMPLATE_OBSOLETE, MM_TEMPLATE_BUILD_COSTUMES,
  MM_TEMPLATE_COSTUME_LIMIT, MM_TEMPLATE_WRONG_SET };
typedef struct MMTemplateCostumeTable
{
    MMCollectionStorage storage;
    MMU32 rows, bytes;
    int big_endian;
    int allocated;
} MMTemplateCostumeTable;

/* A dense GGCM table. The collection owns both buffers and retains the
 * authored word order. Appending requires a table constructed in host order. */
int mm_template_costumes_init(MMTemplateCostumeTable *, const MMStorageAllocator *);
MMTemplateCostumeTable *mm_template_costumes_new(const MMStorageAllocator *);
void mm_template_costumes_dispose(MMTemplateCostumeTable *);
int mm_template_costumes_read(MMTemplateCostumeTable *, const MMReadStream *, MMI32 expected_rows);
int mm_template_costumes_reserve(MMTemplateCostumeTable *, MMU32 rows, MMU32 variable_bytes);
int mm_template_costumes_append(MMTemplateCostumeTable *, const MMI32 *ids, MMI32 count);
MMI32 mm_template_costumes_count(const MMTemplateCostumeTable *, MMI32 set);
int mm_template_costumes_id(const MMTemplateCostumeTable *, MMI32 set, MMI32 index, MMI32 *);
MMI32 mm_template_costumes_wire_size(const MMTemplateCostumeTable *);

typedef struct MMTemplateState
{
    MMPartIndexList *parents,*sets;
    MMTemplateCostumeTable *costumes;
    MMI32 set_count,custom_count,action_count;
    MMU32 flags;
    unsigned short x,y,z; /* Native sixteen-bit BAM rest angles. */
} MMTemplateState;
enum { MM_TEMPLATE_HEADER_BYTES = 16 };
/* Decode the rest angles and flags, retaining the owner's existing tables.
 * Returns zero for unknown byte order after decoding with the host fallback. */
int mm_template_decode_header(const unsigned char bytes[MM_TEMPLATE_HEADER_BYTES], int host_big_endian,
                              MMTemplateState *);

enum { MM_TEMPLATE_COSTUME_WORD_BYTES = 4 };
enum { MM_TEMPLATE_COSTUMES_ABSENT = -1 };
typedef struct MMTemplateRead
{
    void *context;
    int (*table)(void *, int kind, MMPartIndexList **out);
    int (*costumes)(void *, MMI32 expected, MMTemplateCostumeTable **out);
} MMTemplateRead;

/* Table reads return movie statuses, with MM_TEMPLATE_COSTUMES_ABSENT for a
 * missing GGCM link. The owner publishes each table as the reader acquires it
 * and retains partial ownership on failure. */
int mm_template_read_shape(MMTemplateState *, const MMTemplateRead *);
int mm_template_read_costumes(MMTemplateState *, const MMTemplateRead *);
MMU32 mm_template_table_type(int kind);
MMI32 mm_template_decode_costume_word(const unsigned char *, int big_endian);

typedef struct MMTemplateParts
{
    void *context;
    MMI32 count;
    int (*part_set)(void *, MMI32 index, MMI32 *set);
} MMTemplateParts;

/* Count sets up to and including the highest nonnegative set index. The
 * provider may read the associated parent and prepare the part during the
 * same traversal. Callers discard partial results when a part read fails. */
int mm_template_count_sets(MMTemplateState *, const MMTemplateParts *);
typedef struct MMTemplateChild { MMU32 type,number,id; } MMTemplateChild;
typedef struct MMTemplateCostumeScan
{
    void *context;
    int (*child)(void *, MMU32 ordinal, MMTemplateChild *);
    int (*costume_set)(void *, const MMTemplateChild *, MMI32 *set);
    int (*visit)(void *, const MMTemplateChild *, MMI32 set);
} MMTemplateCostumeScan;
/* Scan CMTL links in child order. Use a negative set to visit every set.
 * child returns 1 for a link, 0 at the end, and -1 on error. costume_set
 * returns 1 on success. visit returns 1 to continue, 0 to stop successfully,
 * and -1 on error. The scan returns one on success. */
int mm_template_scan_costumes(MMI32 set, const MMTemplateCostumeScan *);

typedef struct MMTemplateDefaultServices
{
    void *context;
    int (*costume_id)(void *, MMI32 set, MMI32 index, MMI32 *id);
    int (*prepare)(void *, MMI32 set, MMI32 id);
    void (*apply)(void *, MMI32 set);
} MMTemplateDefaultServices;
/* Pick the first costume in each set in set order. Prepare every choice
 * before changing a live body. A caller preparing a private snapshot can
 * leave apply null and discard the snapshot on failure. */
int mm_template_bind_defaults(MMI32 set_count, const MMTemplateDefaultServices *);

typedef struct MMTemplateCreateServices
{
    void *context;
    int host_big_endian,debug_checks;
    MMU32 system;
    MMTemplateState *(*state)(void *,void *object);
    void (*release)(void *,void *);
    MMBlockTransform decode;
    int (*read_name)(void *,void *object,MMU32,MMU32);
    int (*add_chunk)(void *,void *file,MMU32,const void *,size_t,MMU32 *);
    int (*set_name)(void *,void *object,void *file,MMU32,MMU32);
    void (*delete_chunk)(void *,void *file,MMU32,MMU32);
    void *(*create)(void *,int text);
    int (*initialize)(void *,void *object,MMU32,MMU32,MMOwnedBlock *);
    MMI32 (*object_size)(void *,int text);
    void (*invalidate)(void *,void *,size_t);
    MMStorageAllocator table_allocator;
    void *(*create_cache)(void *);
    void *(*fetch_custom)(void *,void *cache,MMU32,MMU32);
    MMI32 (*custom_set)(void *,void *custom);
    int (*append_costumes)(void *,MMTemplateCostumeTable *,const MMI32 *,MMI32 count);
    void *(*create_body)(void *,const MMPartIndexView *parents,const MMPartIndexView *sets);
    int (*default_costume)(void *,void *object,void *body);
    MMI32 (*costume_id)(void *,void *object,MMI32 set,MMI32 index);
    void *(*custom)(void *,void *object,MMI32 id);
    int (*allocate_refs)(void *,MMI32 count,void **);
    void (*apply_custom)(void *,void *body,void *custom);
    void (*free_memory)(void *,void **);
    void (*diagnostic)(void *,int);
} MMTemplateCreateServices;
/* Fresh storage only; a live owner must first be disposed, not reinitialized. */
void mm_template_state_init(MMTemplateState *);
/* state returns a stable borrowed owner, without mutation, failure or reentry.
 * Local C snapshots preserve publication points; do not copy live owners.
 * Initialization expects fresh/empty owners. Groups and header fields publish
 * as the native operation reaches them; direct-call failure does not unwind.
 * Factories release partial objects. Virtual text initialization/default-costume
 * dispatch is preserved. Group counts/allocation products/cache estimates fit
 * signed 32-bit storage. Callbacks do not reenter or mutate unrelated state. */
/* The legacy GGCM builder had 50 stack slots per set. Counts through 50 retain
 * that behavior; a 51st match now diagnoses/fails instead of overflowing. */
/* Read from a borrowed root block when supplied, or look it up on demand. */
int mm_template_read_header(void *,MMResourceCache *,MMU32,MMU32,MMOwnedBlock *,const MMTemplateCreateServices *);
/* Header padding is defined as zero; the native writer left two stack bytes
 * undefined. Name assignment deliberately uses TMPL even for another supplied
 * chunk type; failure deletes the added chunk and retains its published number. */
int mm_template_write_header(void *,void *file,MMU32,MMU32 *,const MMTemplateCreateServices *);
void mm_template_encode_header(unsigned char bytes[MM_TEMPLATE_HEADER_BYTES], int big_endian, MMU32 system,
                               const MMTemplateState *);
int mm_template_fetch(MMResourceCache *,MMU32,MMU32,MMOwnedBlock *,void **object,MMI32 *bytes,
                      const MMTemplateCreateServices *);
int mm_template_initialize(void *,MMResourceCache *,MMU32,MMU32,MMOwnedBlock *,const MMTemplateCreateServices *);
void mm_template_dispose(void *,const MMTemplateCreateServices *);
void *mm_template_create_body(void *,const MMTemplateCreateServices *);
/* Fetch every default custom material before applying any; failure releases
 * fetched references without changing the body. All sets have a first ID. */
int mm_template_default_costume(void *,void *body,const MMTemplateCreateServices *);
#ifdef __cplusplus
}
#endif
#endif
