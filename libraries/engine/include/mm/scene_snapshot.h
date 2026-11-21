#ifndef MM_ENGINE_SCENE_SNAPSHOT_H
#define MM_ENGINE_SCENE_SNAPSHOT_H
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct MMPrivateArchive;

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
    void *(*read)(void *, struct MMPrivateArchive *, MMU32 number); /* Returns a detached scene reference. */
} MMSceneSnapshotServices;

/* Save into a zero-initialized record. Dispose the record even after a failed
 * write. The scene and service context are borrowed only during the call. */
int mm_scene_snapshot_save(MMSceneSnapshot *, void *scene, const MMSceneSnapshotServices *);
void *mm_scene_snapshot_read(const MMSceneSnapshot *, const MMSceneSnapshotServices *);
void mm_scene_snapshot_dispose(MMSceneSnapshot *);

#ifdef __cplusplus
}
#endif
#endif
