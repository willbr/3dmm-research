/* Body construction/shape ownership converted from engine body.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_BODY_CREATE_H
#define THREE_MM_ENGINE_BODY_CREATE_H
#include "movie_data.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMBodyShape
{
    void *sets, *materials, *actors;
    ThreeMMI32 set_count, part_count, hidden;
} ThreeMMBodyShape;
enum ThreeMMBodyActorRole { THREE_MM_BODY_ROOT, THREE_MM_BODY_HIGHLIGHT, THREE_MM_BODY_PART };
typedef struct ThreeMMBodyCreateServices
{
    void *context;
    void (*read)(void *,void *body,ThreeMMBodyShape *);
    void (*write)(void *,void *body,const ThreeMMBodyShape *);
    void *(*highlight)(void *);
    void *(*allocate_highlight)(void *);
    void (*install_highlight)(void *,void *);
    void *(*create)(void *);
    void (*release)(void *,void *);
    void (*validate_lists)(void *,void *parents,void *sets);
    void *(*duplicate_sets)(void *,void *);
    ThreeMMI32 (*count)(void *,void *);
    ThreeMMI32 (*item)(void *,void *,ThreeMMI32);
    int (*allocate_materials)(void *,ThreeMMI32,void **);
    int (*allocate_actors)(void *,ThreeMMI32,void **);
    void (*initialize_actor)(void *,void *body,void *actors,ThreeMMI32 index,int role);
    void (*link)(void *,void *actors,ThreeMMI32 parent,ThreeMMI32 child);
    void (*hide)(void *,void *body);
    void (*release_model)(void *,void *actors,ThreeMMI32 part);
    void (*remove_material)(void *,void *body,ThreeMMI32 set);
    void (*free_memory)(void *,void **);
} ThreeMMBodyCreateServices;
/* Lists have equal lengths, signed 16-bit entries, and parent indices -1 or
 * less than the child index. Set count does not exceed part count. Counts fit
 * native signed-short iteration and
 * allocation products fit the host allocator. Both allocation callbacks clear
 * their storage and retain native zero-size allocation behavior. duplicate_sets
 * returns an owned list reference. free_memory accepts NULL slots and clears
 * released slots. release_model ignores absent models and clears released ones.
 * Bodies start with zeroed ownership.
 * Writes publish partial state before renderer/resource callbacks. Callbacks
 * must not reenter or mutate unrelated body fields. Initialization does not
 * roll back: the factory releases its partial body on failure. */
int three_mm_body_shape_initialize(void *,void *parents,void *sets,const ThreeMMBodyCreateServices *);
int three_mm_body_initialize(void *,void *parents,void *sets,const ThreeMMBodyCreateServices *);
void *three_mm_body_create(void *parents,void *sets,const ThreeMMBodyCreateServices *);
/* Visible bodies hide before releasing models, then materials, actor storage,
 * material storage and the duplicated set list, in that order. Material
 * removal is conditional on actor storage, including partial failures. Counts
 * are retained; released ownership pointers are published as NULL. */
void three_mm_body_shape_dispose(void *,const ThreeMMBodyCreateServices *);
#ifdef __cplusplus
}
#endif
#endif
