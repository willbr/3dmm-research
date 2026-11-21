#ifndef MM_BRENDER_BODY_RENDERER_H
#define MM_BRENDER_BODY_RENDERER_H

#include "mm/body_create.h"
#include "mm/body_edit.h"
#include "mm/body_geometry.h"
#include "mm/body_runtime.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Layout of the live actor array and its adjacent portable draw bindings. */
int mm_brender_body_layout(MMI32 count, size_t limit, size_t *bytes, size_t *bindings_offset);
void mm_brender_body_create_services(MMBodyCreateServices *);
void mm_brender_body_edit_services(MMBodyEditServices *);
void mm_brender_body_runtime_services(MMBodyRuntimeServices *);

/* Release after bodies have gone and before BrEnd. */
void mm_brender_body_release_highlight(void);
void mm_brender_body_highlight_colour(MMI32);

/* Submit the root and parts to the live actor array, or attach an evaluated
 * binding to a temporary scene tree. The draw view's owner keeps its handles
 * valid through both paths. The live selection highlight is handled separately. */
void mm_brender_body_submit(const MMBodyDrawView *, void *actors);
void *mm_brender_body_attach(void *, void *parent, MMI32 index, const MMBodyPartBinding *);

void *mm_brender_body_owner(const void *actor);
void mm_brender_body_geometry_services(MMBodyGeometryServices *);
void mm_brender_body_model_bounds(const void *model, MMBodyBounds *);

#ifdef __cplusplus
}
#endif
#endif
