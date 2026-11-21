/* Body duplication/restoration/reshaping converted from engine body.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_BODY_EDIT_H
#define MM_ENGINE_BODY_EDIT_H
#include "body_create.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMBodyEditServices
{
    MMBodyCreateServices shape;
    void (*copy_bindings)(void *,void *to,void *from);
    void (*copy_actors)(void *,void *body,void *to,const void *from,MMI32 count);
    void (*copy_materials)(void *,void *to,void *from,MMI32 count);
    void (*copy_bounds)(void *,void *to,void *from);
    void *(*material)(void *,void *body,MMI32 set,int *generic);
    void (*retain)(void *,void *);
    void (*show)(void *,void *body);
    void (*swap_bodies)(void *,void *,void *);
    void (*identify)(void *,void *body,void *actors,MMI32 count);
    void (*set_generic)(void *,void *body,MMI32 set,void *material);
    void (*set_custom)(void *,void *body,void *material);
    void (*set_model)(void *,void *body,MMI32 part,void *model);
    int (*highlighted)(void *,void *actors);
    void (*highlight)(void *,void *body);
} MMBodyEditServices;
/* Duplication requires a complete shape and a material for every set. Actors
 * and model references precede set-list duplication and material storage.
 * Generic materials retain one reference per matching part; custom materials
 * retain one per set. The original is re-shown BEFORE failed-copy disposal.
 * The copy is hidden once and keeps native bindings/hit metadata/bounds.
 * Storage copies do not retain references. copy_actors resets the root's
 * parent/next/prev and translates internal links into the copied array.
 * Callbacks must not reenter or independently change unrelated body state. */
void *mm_body_duplicate(void *,const MMBodyEditServices *);
/* replacement is hidden once. swap_bodies preserves the existing native
 * whole-object swap semantics, not just shape storage. Only the receiving
 * body's actor identifiers are rewritten. Its original hidden count survives. */
void mm_body_restore(void *,void *replacement,const MMBodyEditServices *);
/* Reshape duplicates before destroying the old shape. Successful initialization
 * restores visibility before materials, then shared-part models/matrices, root
 * matrix and highlight. Failure restores the duplicate and original visibility;
 * the displaced partial shape is released through the normal body destructor. */
int mm_body_change_shape(void *,void *parents,void *sets,const MMBodyEditServices *);
#ifdef __cplusplus
}
#endif
#endif
