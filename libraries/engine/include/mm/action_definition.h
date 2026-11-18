/* Action resources converted from engine tmpl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTION_DEFINITION_H
#define MM_ENGINE_ACTION_DEFINITION_H
#include "tag.h"
#include "movie_data.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum MMActionTable { MM_ACTION_CELS, MM_ACTION_MATRICES, MM_ACTION_WIRE_SOUNDS };
enum MMActionChunk { MM_ACTION_GGCL=0x4747434c, MM_ACTION_GLXF=0x474c5846, MM_ACTION_GLMS=0x474c4d53 };
typedef struct MMActionDefinition
{
    void *cels,*matrices,*sounds;
    MMU32 flags;
} MMActionDefinition;

enum { MM_ACTION_HEADER_BYTES = 8, MM_ACTION_CEL_BYTES = 8, MM_ACTION_PART_BYTES = 4, MM_ACTION_MATRIX_BYTES = 48 };

typedef struct MMActionRead
{
    void *context;
    int host_big_endian;
    int (*header)(void *, unsigned char bytes[MM_ACTION_HEADER_BYTES]);
    int (*table)(void *, int kind, void **out, int *other_order);
    void (*bad_order)(void *);
} MMActionRead;

/* The providers return movie statuses. The header flags publish before cels
 * are read. Matrices can be loaded separately when the caller needs a pose.
 * Table ownership and byte order are supplied by the table reader. */
int mm_action_read_cels(MMActionDefinition *, const MMActionRead *, int *other_order);
int mm_action_read_matrices(MMActionDefinition *, const MMActionRead *, int *other_order);
MMU32 mm_action_table_type(int kind);
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
    void *(*duplicate)(void *,void *table,int kind);
    void *(*find)(void *,void *file,MMU32,MMU32);
    int (*unpack)(void *,void *block);
    MMI32 (*block_size)(void *,void *block,int packed);
    int (*block_read)(void *,void *block,void *,size_t);
    int (*child)(void *,void *file,MMU32,MMU32,MMU32 child_type,MMU32 *number);
    void *(*read_table)(void *,void *block,int kind,int *other);
    MMI32 (*count)(void *,void *table);
    MMI32 (*table_size)(void *,void *table);
    unsigned char *(*fixed)(void *,void *group,MMI32 index);
    unsigned char *(*variable)(void *,void *group,MMI32 index);
    MMI32 (*variable_size)(void *,void *group,MMI32 index);
    unsigned char *(*data)(void *,void *list,MMI32 index);
    void (*validate_wire_sounds)(void *,void *table);
    void *(*create_sounds)(void *,MMI32 capacity);
    int (*append_sound)(void *,void *table,const MMTag *);
    void (*read_sound)(void *,void *table,MMI32 index,MMTag *);
    void (*clear_sound_type)(void *,void *out);
    void (*publish_sound)(void *,void *out,const MMTag *);
    int (*initialize)(void *,void *object,void *file,MMU32,MMU32);
    MMI32 (*object_size)(void *);
    void (*invalidate)(void *,void *out,size_t);
    void (*bad_order)(void *);
} MMActionDefinitionServices;
/* Fresh states start zeroed. Initialization publishes header and owned tables
 * in native order; direct-call failure keeps partial ownership. Factories
 * release partial objects before invalidating outputs. All table layouts,
 * indices and 32-bit allocation/cache-size products satisfy native preconditions.
 * Callbacks may observe publication but must not reenter or mutate the owner. */
void *mm_action_create(void *cels,void *matrices,MMU32 flags,const MMActionDefinitionServices *);
/* Output storage bridges native pointer/32-bit integer representations without
 * typed pointer aliasing. Estimate-only reads do not unpack or allocate. The
 * final cache estimate deliberately excludes optional sounds, as in native. */
int mm_action_fetch(void *file,MMU32,MMU32,void *block,void *object_out,void *size_out,const MMActionDefinitionServices *);
int mm_action_initialize(MMActionDefinition *,void *file,MMU32,MMU32,const MMActionDefinitionServices *);
/* Releases cels, matrices, then sounds, clearing each slot after release.
 * This includes the optional list omitted by the previous native destructor.
 * Sound tags loaded from wire borrow a NULL cache, never owning references. */
void mm_action_dispose(MMActionDefinition *,const MMActionDefinitionServices *);
MMI32 mm_action_cel_count(const MMActionDefinition *,const MMActionDefinitionServices *);
void mm_action_get_cel(const MMActionDefinition *,MMI32,void *out,const MMActionDefinitionServices *);
void mm_action_get_part(const MMActionDefinition *,MMI32 cel,MMI32 part,void *out,const MMActionDefinitionServices *);
void mm_action_get_matrix(const MMActionDefinition *,MMI32,void *out,const MMActionDefinitionServices *);
/* Clears only type when no sound. High-bit IDs are negative markers under the
 * documented native signed-sound convention, rather than table indices. */
void mm_action_get_sound(const MMActionDefinition *,MMI32,void *out,const MMActionDefinitionServices *);
#ifdef __cplusplus
}
#endif
#endif
