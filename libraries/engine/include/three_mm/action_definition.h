/* Action resources converted from engine tmpl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTION_DEFINITION_H
#define THREE_MM_ENGINE_ACTION_DEFINITION_H
#include "tag.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum ThreeMMActionTable { THREE_MM_ACTION_CELS, THREE_MM_ACTION_MATRICES, THREE_MM_ACTION_WIRE_SOUNDS };
enum ThreeMMActionChunk { THREE_MM_ACTION_GGCL=0x4747434c, THREE_MM_ACTION_GLXF=0x474c5846, THREE_MM_ACTION_GLMS=0x474c4d53 };
typedef struct ThreeMMActionDefinition
{
    void *cels,*matrices,*sounds;
    ThreeMMU32 flags;
} ThreeMMActionDefinition;
typedef struct ThreeMMActionDefinitionServices
{
    void *context; int host_big_endian;
    ThreeMMActionDefinition *(*state)(void *,void *object);
    void *(*create)(void *);
    void (*release)(void *,void *);
    void *(*duplicate)(void *,void *table,int kind);
    void *(*find)(void *,void *file,ThreeMMU32,ThreeMMU32);
    int (*unpack)(void *,void *block);
    ThreeMMI32 (*block_size)(void *,void *block,int packed);
    int (*block_read)(void *,void *block,void *,size_t);
    int (*child)(void *,void *file,ThreeMMU32,ThreeMMU32,ThreeMMU32 child_type,ThreeMMU32 *number);
    void *(*read_table)(void *,void *block,int kind,int *other);
    ThreeMMI32 (*count)(void *,void *table);
    ThreeMMI32 (*table_size)(void *,void *table);
    unsigned char *(*fixed)(void *,void *group,ThreeMMI32 index);
    unsigned char *(*variable)(void *,void *group,ThreeMMI32 index);
    ThreeMMI32 (*variable_size)(void *,void *group,ThreeMMI32 index);
    unsigned char *(*data)(void *,void *list,ThreeMMI32 index);
    void (*validate_wire_sounds)(void *,void *table);
    void *(*create_sounds)(void *,ThreeMMI32 capacity);
    int (*append_sound)(void *,void *table,const ThreeMMTag *);
    void (*read_sound)(void *,void *table,ThreeMMI32 index,ThreeMMTag *);
    void (*clear_sound_type)(void *,void *out);
    void (*publish_sound)(void *,void *out,const ThreeMMTag *);
    int (*initialize)(void *,void *object,void *file,ThreeMMU32,ThreeMMU32);
    ThreeMMI32 (*object_size)(void *);
    void (*invalidate)(void *,void *out,size_t);
    void (*bad_order)(void *);
} ThreeMMActionDefinitionServices;
/* Fresh states start zeroed. Initialization publishes header and owned tables
 * in native order; direct-call failure keeps partial ownership. Factories
 * release partial objects before invalidating outputs. All table layouts,
 * indices and 32-bit allocation/cache-size products satisfy native preconditions.
 * Callbacks may observe publication but must not reenter or mutate the owner. */
void *three_mm_action_create(void *cels,void *matrices,ThreeMMU32 flags,const ThreeMMActionDefinitionServices *);
/* Output storage bridges native pointer/32-bit integer representations without
 * typed pointer aliasing. Estimate-only reads do not unpack or allocate. The
 * final cache estimate deliberately excludes optional sounds, as in native. */
int three_mm_action_fetch(void *file,ThreeMMU32,ThreeMMU32,void *block,void *object_out,void *size_out,const ThreeMMActionDefinitionServices *);
int three_mm_action_initialize(ThreeMMActionDefinition *,void *file,ThreeMMU32,ThreeMMU32,const ThreeMMActionDefinitionServices *);
/* Releases cels, matrices, then sounds, clearing each slot after release.
 * This includes the optional list omitted by the previous native destructor.
 * Sound tags loaded from wire borrow a NULL cache, never owning references. */
void three_mm_action_dispose(ThreeMMActionDefinition *,const ThreeMMActionDefinitionServices *);
ThreeMMI32 three_mm_action_cel_count(const ThreeMMActionDefinition *,const ThreeMMActionDefinitionServices *);
void three_mm_action_get_cel(const ThreeMMActionDefinition *,ThreeMMI32,void *out,const ThreeMMActionDefinitionServices *);
void three_mm_action_get_part(const ThreeMMActionDefinition *,ThreeMMI32 cel,ThreeMMI32 part,void *out,const ThreeMMActionDefinitionServices *);
void three_mm_action_get_matrix(const ThreeMMActionDefinition *,ThreeMMI32,void *out,const ThreeMMActionDefinitionServices *);
/* Clears only type when no sound. High-bit IDs are negative markers under the
 * documented native signed-sound convention, rather than table indices. */
void three_mm_action_get_sound(const ThreeMMActionDefinition *,ThreeMMI32,void *out,const ThreeMMActionDefinitionServices *);
#ifdef __cplusplus
}
#endif
#endif
