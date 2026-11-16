/* Body construction/shape ownership converted from engine body.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_BODY_CREATE_H
#define MM_ENGINE_BODY_CREATE_H
#include "movie_data.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMBodyShape
{
    void *sets, *materials, *actors;
    MMI32 set_count, part_count, hidden;
} MMBodyShape;
enum MMBodyActorRole { MM_BODY_ROOT, MM_BODY_HIGHLIGHT, MM_BODY_PART };
typedef struct MMBodyCreateServices
{
    void *context;
    MMBodyShape *(*state)(void *,void *body);
    void *(*highlight)(void *);
    void *(*allocate_highlight)(void *);
    void (*install_highlight)(void *,void *);
    void *(*create)(void *);
    void (*release)(void *,void *);
    void (*validate_lists)(void *,void *parents,void *sets);
    void *(*duplicate_sets)(void *,void *);
    MMI32 (*count)(void *,void *);
    MMI32 (*item)(void *,void *,MMI32);
    int (*allocate_materials)(void *,MMI32,void **);
    int (*allocate_actors)(void *,MMI32,void **);
    void (*initialize_actor)(void *,void *body,void *actors,MMI32 index,int role);
    void (*link)(void *,void *actors,MMI32 parent,MMI32 child);
    void (*hide)(void *,void *body);
    void (*release_model)(void *,void *actors,MMI32 part);
    void (*remove_material)(void *,void *body,MMI32 set);
    void (*free_memory)(void *,void **);
} MMBodyCreateServices;
/* Lists have equal lengths, signed 16-bit entries, and parent indices -1 or
 * less than the child index. Set count does not exceed part count. Counts fit
 * native signed-short iteration and
 * allocation products fit the host allocator. Both allocation callbacks clear
 * their storage and retain native zero-size allocation behavior. duplicate_sets
 * returns an owned list reference. free_memory accepts NULL slots and clears
 * released slots. release_model ignores absent models and clears released ones.
 * Bodies start with zeroed ownership. state returns a stable borrowed C owner;
 * access cannot fail, reenter or mutate it. Local value snapshots preserve
 * publication points before renderer/resource callbacks. Do not copy live owners.
 * Writes publish partial state before renderer/resource callbacks. Callbacks
 * must not reenter or mutate unrelated body fields. Initialization does not
 * roll back: the factory releases its partial body on failure. */
void mm_body_shape_init(MMBodyShape *);
int mm_body_shape_initialize(void *,void *parents,void *sets,const MMBodyCreateServices *);
int mm_body_initialize(void *,void *parents,void *sets,const MMBodyCreateServices *);
void *mm_body_create(void *parents,void *sets,const MMBodyCreateServices *);
/* Visible bodies hide before releasing models, then materials, actor storage,
 * material storage and the duplicated set list, in that order. Material
 * removal is conditional on actor storage, including partial failures. Counts
 * are retained; released ownership pointers are published as NULL. */
void mm_body_shape_dispose(void *,const MMBodyCreateServices *);
#ifdef __cplusplus
}
#endif
#endif
