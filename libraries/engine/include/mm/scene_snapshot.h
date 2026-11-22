#ifndef MM_ENGINE_SCENE_SNAPSHOT_H
#define MM_ENGINE_SCENE_SNAPSHOT_H
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct MMPrivateArchive;
struct MMChunkEndpoint;
struct MMMovieArchiveEditor;
struct MMMovieArchiveText;
struct MMResourceCache;

typedef struct MMSceneSnapshot
{
    struct MMPrivateArchive *archive;
    MMU32 number;
} MMSceneSnapshot;

typedef struct MMSceneSnapshotServices
{
    void *context;
    struct MMPrivateArchive *(*open)(void *); /* Transfers an empty writable archive. */
    void (*begin)(void *), (*end)(void *);
    int (*write)(void *, void *scene, struct MMPrivateArchive *, MMU32 *number);
} MMSceneSnapshotServices;

/* Save into a zero-initialized record. Dispose the record even after a failed
 * write. The scene and service context are borrowed only during the call. */
int mm_scene_snapshot_save(MMSceneSnapshot *, void *scene, const MMSceneSnapshotServices *);
void mm_scene_snapshot_dispose(MMSceneSnapshot *);
/* Transfer a private scene as an unlinked root. Rebind local asset references
 * before publishing its number. The destination editor and endpoint share an index. */
int mm_scene_snapshot_copy(const MMSceneSnapshot *, const struct MMChunkEndpoint *,
                           const struct MMMovieArchiveEditor *, MMU32 *number);
int mm_scene_snapshot_copy_cache(const MMSceneSnapshot *, struct MMResourceCache *,
                                 const struct MMMovieArchiveText *, MMU32 *number);

#ifdef __cplusplus
}
#endif
#endif
