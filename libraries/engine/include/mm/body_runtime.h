/* Body materials/visibility/costumes converted from engine body.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_BODY_RUNTIME_H
#define MM_ENGINE_BODY_RUNTIME_H
#include "body_create.h"
#include "actor_pose.h"
#ifdef __cplusplus
extern "C" {
#endif
enum MMBodyRuntimeDiagnostic
{ MM_BODY_MISSING_MATERIAL, MM_BODY_EMPTY_SET, MM_BODY_SHORT_COSTUME, MM_BODY_COSTUME_SHAPE };
typedef struct MMBodySetServices
{
    void *context;
    MMI32 (*part_set)(void *, void *body, MMI32 part);
    int (*apply_part)(void *, void *body, MMI32 part, MMI32 slot);
} MMBodySetServices;
/* Visit body parts in shape order. The costume slot counts matching parts
 * only. Returns the number applied, or -1 after the first rejected part. */
MMI32 mm_body_apply_set(void *body, MMI32 part_count, MMI32 set, const MMBodySetServices *);
typedef struct MMBodyRuntimeServices
{
    MMBodyCreateServices shape;
    void (*retain)(void *,void *);
    void *(*model)(void *,void *body,MMI32 part);
    void (*assign_model)(void *,void *body,MMI32 part,void *model);
    void *(*part_material)(void *,void *body,MMI32 part);
    void (*assign_material)(void *,void *body,MMI32 part,void *raw_material);
    void *(*material_object)(void *,void *raw_material);
    void *(*material_handle)(void *,void *material_object);
    void *(*custom)(void *,void *body,MMI32 set);
    void (*assign_custom)(void *,void *body,MMI32 set,void *custom);
    MMI32 (*custom_set)(void *,void *);
    MMI32 (*custom_count)(void *,void *);
    void *(*custom_model)(void *,void *,MMI32);
    void *(*custom_material)(void *,void *,MMI32);
    void (*invalidate_output)(void *,void **);
    void (*matrix)(void *,void *body,MMI32 actor,const MMActorMatrix *);
    void (*highlight)(void *,void *body,int);
    void (*attach)(void *,void *body);
    int (*attached)(void *,void *body);
    void (*detach)(void *,void *body);
    void (*clear_bounds)(void *,void *body);
    void (*changed)(void *,void *body);
    int (*is_generic)(void *,void *material);
    void (*diagnostic)(void *,int);
} MMBodyRuntimeServices;
/* Services expose raw renderer material handles separately from owned material
 * objects. Removing a custom costume releases its owner BEFORE clearing raw
 * part handles, which must not dereference those now potentially stale handles.
 * Generic assignment requires the caller to keep its material alive through
 * old-material removal. Custom assignment retains first, supporting self-use.
 * Same-model assignment returns without references or notification. Other
 * mutations notify when visible, including repeated highlight/material edits.
 * Show requires a positive hidden count; part/set indices and non-NULL model
 * arguments satisfy the native preconditions. Renderer callbacks must not
 * reenter or change unrelated body state. */
void mm_body_show(void *,const MMBodyRuntimeServices *);
void mm_body_hide(void *,const MMBodyRuntimeServices *);
void mm_body_highlight(void *,int,const MMBodyRuntimeServices *);
void mm_body_locate(void *,const MMActorBodyPose *,const MMBodyRuntimeServices *);
void mm_body_set_matrix(void *,MMI32,const MMActorMatrix *,const MMBodyRuntimeServices *);
void mm_body_set_model(void *,MMI32,void *,const MMBodyRuntimeServices *);
void mm_body_remove_material(void *,MMI32,const MMBodyRuntimeServices *);
void mm_body_set_generic(void *,MMI32,void *,const MMBodyRuntimeServices *);
void mm_body_set_custom(void *,void *,const MMBodyRuntimeServices *);
/* Valid sets contain a generic material on a part or an owned custom material.
 * Unused outputs use the optional native invalidation callback. Broken empty
 * set preconditions diagnose and leave the generic output untouched. */
void mm_body_get_material(void *,MMI32,int *,void **generic,void **custom,const MMBodyRuntimeServices *);
typedef struct MMBodyCostume { void **entries; MMI32 count; } MMBodyCostume;
/* Empty owner count is defined as zero (native debug used an unread poison).
 * Set requires a successfully captured owner. Capture clears old references
 * before allocation and leaves an empty owner after allocation failure. */
void mm_body_costume_init(MMBodyCostume *);
void mm_body_costume_clear(MMBodyCostume *,const MMBodyRuntimeServices *);
int mm_body_costume_capture(MMBodyCostume *,void *,const MMBodyRuntimeServices *);
void mm_body_costume_apply(const MMBodyCostume *,void *,int allow_different,const MMBodyRuntimeServices *);
#ifdef __cplusplus
}
#endif
#endif
