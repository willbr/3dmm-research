#ifndef MM_BRENDER_BODY_RENDERER_H
#define MM_BRENDER_BODY_RENDERER_H

#include "mm/body_create.h"
#include "mm/body_geometry.h"

#ifdef __cplusplus
extern "C" {
#endif

void mm_brender_body_matrix(void *, void *actors, MMI32 index, const MMActorMatrix *);
void mm_brender_body_model(void *, void *actors, MMI32 index, void *model);
void mm_brender_body_material(void *, void *actors, MMI32 index, void *material);

/* Submit the root and parts to the live actor array, or attach an evaluated
 * binding to a temporary scene tree. The draw view's owner keeps its handles
 * valid through both paths. The live selection highlight is handled separately. */
void mm_brender_body_submit(const MMBodyDrawView *, void *actors);
void *mm_brender_body_attach(void *, void *parent, MMI32 index, const MMBodyPartBinding *);

void *mm_brender_body_owner(const void *actor);
void mm_brender_body_geometry_services(MMBodyGeometryServices *);

#ifdef __cplusplus
}
#endif
#endif
