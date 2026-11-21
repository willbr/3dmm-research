/* Body construction/shape ownership converted from engine body.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_BODY_CREATE_H
#define MM_ENGINE_BODY_CREATE_H
#include "movie_data.h"
#include "actor_math.h"
#include "body_geometry.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMBodyPartBinding
{
    MMActorMatrix matrix;
    void *model_owner; /* Retained resource object. */
    void *model_handle; /* Draw handle borrowed from the model resource. */
    void *material; /* Draw handle held through a generic material or per-set costume. */
    MMI32 parent;
} MMBodyPartBinding;
typedef struct MMBodyShape
{
    void *sets, *materials, *actors;
    MMBodyPartBinding *bindings; /* Root at 0, reserved highlight at 1, then parts. */
    MMI32 set_count, part_count, hidden;
} MMBodyShape;
typedef struct MMBodyRenderBinding
{
    void *context;
    void (*attach)(void *, void *body);
    void (*changed)(void *);
} MMBodyRenderBinding;
typedef struct MMBodyState
{
    MMBodyShape shape;
    MMBodyGeometry geometry;
    MMBodyRenderBinding render;
} MMBodyState;
typedef struct MMBodyDrawView
{
    const MMBodyPartBinding *bindings;
    MMI32 part_count;
} MMBodyDrawView;
typedef int (*MMBodyDrawPart)(void *, MMI32 index, MMI32 parent, const MMBodyPartBinding *);
enum MMBodyActorRole { MM_BODY_ROOT, MM_BODY_HIGHLIGHT, MM_BODY_PART };
typedef struct MMBodyCreateServices
{
    void *context;
    MMBodyState *(*state)(void *,void *body);
    void *(*highlight)(void *);
    void *(*allocate_highlight)(void *);
    void (*install_highlight)(void *,void *);
    void *(*create)(void *);
    void (*release)(void *,void *);
    void (*validate_lists)(void *,void *parents,void *sets);
    void *(*duplicate_sets)(void *,void *);
    void (*release_sets)(void *,void *);
    MMI32 (*count)(void *,void *);
    MMI32 (*item)(void *,void *,MMI32);
    int (*allocate_materials)(void *,MMI32,void **);
    int (*allocate_actors)(void *,MMI32,void **,MMBodyPartBinding **);
    void (*initialize_actor)(void *,void *body,void *actors,MMI32 index,int role);
    void (*assign_matrix)(void *,void *actors,MMI32 index,const MMActorMatrix *);
    void (*link)(void *,void *actors,MMI32 parent,MMI32 child);
    void (*hide)(void *,void *body);
    void (*remove_material)(void *,void *body,MMI32 set);
    void (*free_memory)(void *,void **);
} MMBodyCreateServices;
/* Lists have equal lengths, signed 16-bit entries, and parent indices -1 or
 * less than the child index. Set count does not exceed part count. Counts fit
 * native signed-short iteration and
 * allocation products fit the host allocator. Both allocation callbacks clear
 * their storage and retain native zero-size allocation behavior. The actor
 * allocation returns the draw actors and their C bindings in one owner freed
 * through the actor pointer. duplicate_sets returns an owned list reference.
 * free_memory accepts NULL slots and clears released slots.
 * Bodies start with zeroed ownership. state returns a stable borrowed C owner;
 * access cannot fail, reenter or mutate it. Local value snapshots preserve
 * publication points before renderer/resource callbacks. State owns its shape
 * storage and borrows its renderer binding.
 * Writes publish partial state before renderer/resource callbacks. Callbacks
 * must not reenter or mutate unrelated body fields. Initialization does not
 * roll back: the factory releases its partial body on failure. */
void mm_body_state_init(MMBodyState *);
/* Publish the authoritative matrix and project it onto the draw actor. */
void mm_body_shape_matrix(MMBodyShape *,MMI32,const MMActorMatrix *,const MMBodyCreateServices *);
/* Compose a part through its parents and the root in renderer order. */
void mm_body_shape_part_world(const MMBodyShape *,MMI32 part,MMActorMatrix *);
/* Visit the root and the parts in parent-before-child order. Indices are in
 * the draw array. Parent -1 means the root attaches to the caller's world.
 * The selection highlight is separate. Stop when the consumer returns zero. */
int mm_body_draw(const MMBodyDrawView *, void *context, MMBodyDrawPart);
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
