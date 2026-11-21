/* Body duplication/restoration/reshaping converted from engine body.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_BODY_EDIT_H
#define MM_ENGINE_BODY_EDIT_H
#include "body_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMBodyEditServices
{
    MMBodyRuntimeServices runtime;
    void (*copy_actors)(void *,void *body,void *to,const void *from,MMI32 count);
    void (*identify)(void *,void *body,void *actors,MMI32 count);
    int (*highlighted)(void *,void *actors);
} MMBodyEditServices;
/* Duplication requires a complete shape. Actors
 * and model references precede set-list duplication and material storage.
 * Generic materials retain one reference per matching part; custom materials
 * retain one per set. The original is re-shown BEFORE failed-copy disposal.
 * The copy is hidden once and keeps native bindings/hit metadata/bounds.
 * Storage copies do not retain references. copy_actors resets the root's
 * parent/next/prev and translates internal links into the copied array.
 * Callbacks must not reenter or independently change unrelated body state. */
void *mm_body_duplicate(void *,const MMBodyEditServices *);
/* Replacement is hidden once. Exchange owned state while preserving both
 * facade identities and their ownership. Each tree identifies its current
 * body. The receiver keeps its original hidden count. A visible displaced
 * tree stays attached until its new owner hides or releases it. */
void mm_body_restore(void *,void *replacement,const MMBodyEditServices *);
/* Reshape requires a material for each old set shared with the new shape.
 * It duplicates before destroying the old shape. Successful initialization
 * restores visibility before materials, then shared-part models/matrices, root
 * matrix and highlight. Failure restores the duplicate and original visibility;
 * the displaced partial shape is released through the normal body destructor. */
int mm_body_change_shape(void *,void *parents,void *sets,const MMBodyEditServices *);
#ifdef __cplusplus
}
#endif
#endif
