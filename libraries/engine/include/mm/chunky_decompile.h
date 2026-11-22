#ifndef MM_ENGINE_CHUNKY_DECOMPILE_H
#define MM_ENGINE_CHUNKY_DECOMPILE_H
#include "chunk_storage.h"
#include "chunky_listing.h"
#include "movie_title.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MMChunkyBodyResult
{
    MM_CHUNKY_BODY_FALLBACK,
    MM_CHUNKY_BODY_WRITTEN,
    MM_CHUNKY_BODY_STOP,
    MM_CHUNKY_BODY_ERROR
} MMChunkyBodyResult;

typedef struct MMChunkyDecompileHooks
{
    void *context;
    /* Optional specialized bodies run before the generic tag rules. */
    MMChunkyBodyResult (*special)(void *, const MMChunkEntry *);
    /* Called for GLOP only. A zero result requests the standard fallback. */
    int (*script)(void *, const MMChunkEntry *);
} MMChunkyDecompileHooks;

typedef enum MMChunkyDecompileIssue
{
    MM_CHUNKY_DECOMPILE_COLLECTION,
    MM_CHUNKY_DECOMPILE_PAYLOAD
} MMChunkyDecompileIssue;

typedef struct MMChunkyDecompile
{
    MMChunkAccess chunks;
    void *archive;
    const MMChunkPayloadServices *payloads;
    const MMChunkTitleIO *names;
    const MMChunkyCollectionListing *listing;
    const MMChunkyDecompileHooks *hooks;
    void (*issue)(void *listing_context, MMChunkyDecompileIssue);
} MMChunkyDecompile;

/* Borrow the archive and sink synchronously. Callbacks may fetch payloads but
 * must preserve index order and size. Visits existing entries and child links
 * in index order. Recoverable body failures fall back to stored bytes. Returns
 * zero on an unreadable index/link or a fatal specialized body failure. */
int mm_chunky_decompile(const MMChunkyDecompile *);

#ifdef __cplusplus
}
#endif
#endif
