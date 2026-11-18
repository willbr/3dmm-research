#ifndef MM_ENGINE_DAMAGE_MAP_H
#define MM_ENGINE_DAMAGE_MAP_H
#include "pixel_storage.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMDamageMap
{
    MMPixelBuffer mask;
    MMPixelRect bounds;
    int bounds_valid;
} MMDamageMap;

/* Initialize a zeroed or currently owned map. Init and clone replace the map
 * only after all allocation succeeds. */
int mm_damage_init(MMDamageMap *, MMI32 width, MMI32 height);
int mm_damage_clone(MMDamageMap *target, const MMDamageMap *source);
void mm_damage_reset(MMDamageMap *);
/* A null rectangle marks or clears the whole map. */
void mm_damage_set(MMDamageMap *, const MMPixelRect *rectangle, int marked);
int mm_damage_bounds(MMDamageMap *, MMPixelRect *out);

typedef int (*MMDamageEmit)(void *context, const MMPixelRect *rectangle);
/* Enumerate runs in band order, merging rows with identical coverage. The
 * callback borrows each rectangle. A rejection stops the visit and keeps the
 * map intact. Take clears the map only after a successful visit. */
int mm_damage_visit(const MMDamageMap *, MMDamageEmit, void *context);
int mm_damage_take(MMDamageMap *, MMDamageEmit, void *context);
/* Move damage within a clipped area and clear its exposed pixels. */
int mm_damage_scroll(MMDamageMap *, const MMPixelRect *, MMI32 dx, MMI32 dy);

#ifdef __cplusplus
}
#endif
#endif
