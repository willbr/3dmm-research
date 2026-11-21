#ifndef MM_BROWSER_THUMBNAIL_H
#define MM_BROWSER_THUMBNAIL_H
#include "resource_cache.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MM_BROWSER_THUMBNAIL_ANY 0xffffffffUL
typedef struct MMBrowserThumbnail
{
    MMU32 type, value, graphic;
    int has_graphic;
} MMBrowserThumbnail;

typedef int (*MMBrowserThumbnailVisit)(void *, MMU32 type, MMU32 number, MMU32 slot);

/* Resolve a specific root, or the first row of its type for an unnumbered query. */
int mm_browser_thumbnail_first(const MMChunkAccess *, MMU32 type, MMU32 number, MMU32 *position);
/* With a child type, visit the matching children of the selected root and skip
 * failed visitors. With a zero child type, visit consecutive roots of the same
 * type and stop on a failed visitor. Structural errors stop either scan. */
int mm_browser_thumbnail_visit(const MMChunkAccess *, MMU32 first, MMU32 child_type,
                               MMBrowserThumbnailVisit, void *context);
/* Decode the 12-byte .THD record. The last word is a number or a slot according
 * to the query. A missing slot-zero GOKD uses that word as its graphic number. */
int mm_browser_thumbnail_decode(const MMReadStream *, MMBrowserThumbnail *);
int mm_browser_thumbnail_read(MMResourceCache *, MMU32 type, MMU32 number, MMBlockTransform,
                              MMBrowserThumbnail *);

#ifdef __cplusplus
}
#endif
#endif
