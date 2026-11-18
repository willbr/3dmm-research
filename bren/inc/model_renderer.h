#ifndef MM_BRENDER_MODEL_RENDERER_H
#define MM_BRENDER_MODEL_RENDERER_H

#include "mm/model_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bind the BRender operations of a model provider. File access, object
 * allocation and diagnostics remain the caller's services. */
void mm_brender_model_services(MMModelServices *);
void *mm_brender_model_owner(const void *handle);
int mm_brender_model_prelight(const MMModelResource *, MMI32 lights, void (*multiple_lights)(void *));

#ifdef __cplusplus
}
#endif
#endif
