/* Body materials/visibility/costumes converted from engine body.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_BODY_RUNTIME_H
#define THREE_MM_ENGINE_BODY_RUNTIME_H
#include "body_create.h"
#ifdef __cplusplus
extern "C" {
#endif
enum ThreeMMBodyRuntimeDiagnostic
{ THREE_MM_BODY_MISSING_MATERIAL, THREE_MM_BODY_EMPTY_SET, THREE_MM_BODY_SHORT_COSTUME, THREE_MM_BODY_COSTUME_SHAPE };
typedef struct ThreeMMBodyRuntimeServices
{
    ThreeMMBodyCreateServices shape;
    void (*retain)(void *,void *);
    void *(*model)(void *,void *body,ThreeMMI32 part);
    void (*assign_model)(void *,void *body,ThreeMMI32 part,void *model);
    void *(*part_material)(void *,void *body,ThreeMMI32 part);
    void (*assign_material)(void *,void *body,ThreeMMI32 part,void *raw_material);
    void *(*material_object)(void *,void *raw_material);
    void *(*material_handle)(void *,void *material_object);
    void *(*custom)(void *,void *body,ThreeMMI32 set);
    void (*assign_custom)(void *,void *body,ThreeMMI32 set,void *custom);
    ThreeMMI32 (*custom_set)(void *,void *);
    ThreeMMI32 (*custom_count)(void *,void *);
    void *(*custom_model)(void *,void *,ThreeMMI32);
    void *(*custom_material)(void *,void *,ThreeMMI32);
    void (*invalidate_output)(void *,void **);
    void (*matrix)(void *,void *body,ThreeMMI32 actor,const void *);
    void (*translate)(void *,void *body,ThreeMMI32,ThreeMMI32,ThreeMMI32);
    void (*highlight)(void *,void *body,int);
    void (*attach)(void *,void *body);
    int (*attached)(void *,void *body);
    void (*detach)(void *,void *body);
    void (*clear_bounds)(void *,void *body);
    void (*changed)(void *,void *body);
    int (*is_generic)(void *,void *material);
    void (*diagnostic)(void *,int);
} ThreeMMBodyRuntimeServices;
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
void three_mm_body_show(void *,const ThreeMMBodyRuntimeServices *);
void three_mm_body_hide(void *,const ThreeMMBodyRuntimeServices *);
void three_mm_body_highlight(void *,int,const ThreeMMBodyRuntimeServices *);
void three_mm_body_locate(void *,ThreeMMI32,ThreeMMI32,ThreeMMI32,const void *,const ThreeMMBodyRuntimeServices *);
void three_mm_body_set_matrix(void *,ThreeMMI32,const void *,const ThreeMMBodyRuntimeServices *);
void three_mm_body_set_model(void *,ThreeMMI32,void *,const ThreeMMBodyRuntimeServices *);
void three_mm_body_remove_material(void *,ThreeMMI32,const ThreeMMBodyRuntimeServices *);
void three_mm_body_set_generic(void *,ThreeMMI32,void *,const ThreeMMBodyRuntimeServices *);
void three_mm_body_set_custom(void *,void *,const ThreeMMBodyRuntimeServices *);
/* Valid sets contain a generic material on a part or an owned custom material.
 * Unused outputs use the optional native invalidation callback. Broken empty
 * set preconditions diagnose and leave the generic output untouched. */
void three_mm_body_get_material(void *,ThreeMMI32,int *,void **generic,void **custom,const ThreeMMBodyRuntimeServices *);
typedef struct ThreeMMBodyCostume { void **entries; ThreeMMI32 count; } ThreeMMBodyCostume;
/* Empty owner count is defined as zero (native debug used an unread poison).
 * Set requires a successfully captured owner. Capture clears old references
 * before allocation and leaves an empty owner after allocation failure. */
void three_mm_body_costume_init(ThreeMMBodyCostume *);
void three_mm_body_costume_clear(ThreeMMBodyCostume *,const ThreeMMBodyRuntimeServices *);
int three_mm_body_costume_capture(ThreeMMBodyCostume *,void *,const ThreeMMBodyRuntimeServices *);
void three_mm_body_costume_apply(const ThreeMMBodyCostume *,void *,int allow_different,const ThreeMMBodyRuntimeServices *);
#ifdef __cplusplus
}
#endif
#endif
