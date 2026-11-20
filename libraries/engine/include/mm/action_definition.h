/* Action resources converted from engine tmpl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTION_DEFINITION_H
#define MM_ENGINE_ACTION_DEFINITION_H
#include "tag.h"
#include "movie_data.h"
#include "collections.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum MMActionTableKind { MM_ACTION_CELS, MM_ACTION_MATRICES, MM_ACTION_WIRE_SOUNDS, MM_ACTION_SOUNDS };
enum MMActionChunk { MM_ACTION_GGCL=0x4747434c, MM_ACTION_GLXF=0x474c5846, MM_ACTION_GLMS=0x474c4d53 };
typedef struct MMActionTable
{
    MMCollectionStorage storage;
    MMCollectionHeader header;
    void *owner;
    void (*release_owner)(void *);
} MMActionTable;

typedef struct MMActionDefinition
{
    MMActionTable *cels,*matrices,*sounds;
    MMU32 flags;
} MMActionDefinition;

enum { MM_ACTION_HEADER_BYTES = 8, MM_ACTION_CEL_BYTES = 8, MM_ACTION_PART_BYTES = 4, MM_ACTION_MATRIX_BYTES = 48 };

typedef struct MMActionRead
{
    void *context;
    int host_big_endian;
    int (*header)(void *, unsigned char bytes[MM_ACTION_HEADER_BYTES]);
    int (*table)(void *, int kind, MMActionTable **out);
    void (*bad_order)(void *);
} MMActionRead;

/* Providers return movie statuses. The header flags publish before cels are
 * read. Matrices can be loaded separately when the caller needs a pose.
 * The table provider publishes each completed table and transfers ownership. */
int mm_action_read_cels(MMActionDefinition *, const MMActionRead *);
int mm_action_read_matrices(MMActionDefinition *, const MMActionRead *);
MMU32 mm_action_table_type(int kind);
void mm_action_table_init(MMActionTable *, const MMStorageAllocator *, void *owner, void (*release_owner)(void *));
void mm_action_table_dispose(MMActionTable *);
/* Read into a fresh table. Record payloads retain their wire byte order, and
 * the cel location table uses native byte order. */
int mm_action_table_read(MMActionTable *, const MMReadStream *, int kind);
/* Prepare fresh native-order tables. A cel append returns writable part
 * records which must be filled before reading or appending another cel. */
void mm_action_cels_prepare(MMActionTable *);
unsigned char *mm_action_cel_append(MMActionTable *, MMU32 sound, MMI32 distance, MMU32 parts);
int mm_action_matrices_prepare(MMActionTable *, MMU32 count);
void mm_action_matrix_put(MMActionTable *, MMU32 index, const void *matrix);
int mm_action_sounds_reserve(MMActionTable *, MMU32 count);
int mm_action_sounds_append(MMActionTable *, const MMTag *);
int mm_action_sound_tag(const MMActionTable *, MMU32 index, MMTag *);

/* Copy one wire record into native order. Each input can alias its output. */
void mm_action_decode_cel(const void *bytes, int other_order, void *out);
void mm_action_decode_part(const void *bytes, int other_order, void *out);
void mm_action_decode_matrix(const void *bytes, int other_order, void *out);
typedef struct MMActionDefinitionServices
{
    void *context; int host_big_endian;
    MMActionDefinition *(*state)(void *,void *object);
    void *(*create)(void *);
    void (*release)(void *,void *);
    MMActionTable *(*new_table)(void *,int kind);
    void *(*find)(void *,void *file,MMU32,MMU32);
    int (*unpack)(void *,void *block);
    MMI32 (*block_size)(void *,void *block,int packed);
    int (*block_read)(void *,void *block,void *,size_t);
    int (*child)(void *,void *file,MMU32,MMU32,MMU32 child_type,MMU32 *number);
    MMReadStream (*stream)(void *,void *block);
    void (*clear_sound_type)(void *,void *out);
    void (*publish_sound)(void *,void *out,const MMTag *);
    MMI32 (*object_size)(void *);
    void (*invalidate)(void *,void *out,size_t);
    void (*bad_order)(void *);
} MMActionDefinitionServices;
/* Fresh states start zeroed. Initialization publishes header and owned tables
 * in native order; direct-call failure keeps partial ownership. Factories
 * release partial objects before invalidating outputs. All table layouts,
 * indices and 32-bit allocation/cache-size products satisfy native preconditions.
 * Callbacks may observe publication but must not reenter or mutate the owner. */
/* The caller owns the tables on failure. Success transfers both tables. */
void *mm_action_create(MMActionTable *cels,MMActionTable *matrices,MMU32 flags,const MMActionDefinitionServices *);
/* Output storage bridges native pointer/32-bit integer representations without
 * typed pointer aliasing. Estimate-only reads do not unpack or allocate. The
 * final cache estimate deliberately excludes optional sounds, as in native. */
int mm_action_fetch(void *file,MMU32,MMU32,void *block,void *object_out,void *size_out,const MMActionDefinitionServices *);
/* A supplied header block is already unpacked. Pass NULL for a direct lookup. */
int mm_action_initialize(MMActionDefinition *,void *file,MMU32,MMU32,void *header,const MMActionDefinitionServices *);
/* Releases cels, matrices, then sounds, clearing each slot after release.
 * This includes the optional list omitted by the previous native destructor.
 * Sound tags loaded from wire borrow a NULL cache, never owning references. */
void mm_action_dispose(MMActionDefinition *);
MMI32 mm_action_cel_count(const MMActionDefinition *);
/* Returns -1 for an unavailable row. Getters return zero and leave the
 * output untouched when the record is unavailable. */
MMI32 mm_action_part_count(const MMActionDefinition *,MMI32 cel);
int mm_action_get_cel(const MMActionDefinition *,MMI32,void *out);
int mm_action_get_part(const MMActionDefinition *,MMI32 cel,MMI32 part,void *out);
int mm_action_get_matrix(const MMActionDefinition *,MMI32,void *out);
/* Clears only type when no sound. High-bit IDs are negative markers under the
 * documented native signed-sound convention, rather than table indices. */
void mm_action_get_sound(const MMActionDefinition *,MMI32,void *out,const MMActionDefinitionServices *);
#ifdef __cplusplus
}
#endif
#endif
