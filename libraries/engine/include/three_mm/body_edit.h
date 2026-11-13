/* Body duplication/restoration/reshaping converted from engine body.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_BODY_EDIT_H
#define THREE_MM_ENGINE_BODY_EDIT_H
#include "body_create.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMBodyEditServices
{
    ThreeMMBodyCreateServices shape;
    void (*copy_bindings)(void *,void *to,void *from);
    void (*copy_actors)(void *,void *to,void *from,ThreeMMI32 count);
    void (*rebase_actor)(void *,void *body,void *to,void *from,ThreeMMI32 index,int root);
    void (*retain_model)(void *,void *actors,ThreeMMI32 part);
    void (*copy_materials)(void *,void *to,void *from,ThreeMMI32 count);
    void (*copy_bounds)(void *,void *to,void *from);
    void *(*material)(void *,void *body,ThreeMMI32 set,int *generic);
    void (*retain)(void *,void *);
    void (*show)(void *,void *body);
    void (*swap_bodies)(void *,void *,void *);
    void (*identify)(void *,void *body,void *actors,ThreeMMI32 index);
    void (*set_generic)(void *,void *body,ThreeMMI32 set,void *material);
    void (*set_custom)(void *,void *body,void *material);
    void *(*model)(void *,void *actors,ThreeMMI32 part);
    void (*set_model)(void *,void *body,ThreeMMI32 part,void *model);
    void (*copy_matrix)(void *,void *to,ThreeMMI32 to_index,void *from,ThreeMMI32 from_index);
    int (*highlighted)(void *,void *actors);
    void (*highlight)(void *,void *body);
} ThreeMMBodyEditServices;
/* Duplication requires a complete shape and a material for every set. Actors
 * and model references precede set-list duplication and material storage.
 * Generic materials retain one reference per matching part; custom materials
 * retain one per set. The original is re-shown BEFORE failed-copy disposal.
 * The copy is hidden once and keeps native bindings/hit metadata/bounds.
 * Storage copies do not retain references; rebase_actor resets the root's
 * parent/next/prev and translates all internal links into the copied array.
 * Callbacks must not reenter or independently change unrelated body state. */
void *three_mm_body_duplicate(void *,const ThreeMMBodyEditServices *);
/* replacement is hidden once. swap_bodies preserves the existing native
 * whole-object swap semantics, not just shape storage. Only the receiving
 * body's actor identifiers are rewritten. Its original hidden count survives. */
void three_mm_body_restore(void *,void *replacement,const ThreeMMBodyEditServices *);
/* Reshape duplicates before destroying the old shape. Successful initialization
 * restores visibility before materials, then shared-part models/matrices, root
 * matrix and highlight. Failure restores the duplicate and original visibility;
 * the displaced partial shape is released through the normal body destructor. */
int three_mm_body_change_shape(void *,void *parents,void *sets,const ThreeMMBodyEditServices *);
#ifdef __cplusplus
}
#endif
#endif
