#ifndef MARCUS_LIVE_DOCUMENT_H
#define MARCUS_LIVE_DOCUMENT_H

#include "actor_names.h"
#include "player.h"
#include "storyboard_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MarcusSceneRows
{
    MMCollectionStorage storage;
    size_t count;
} MarcusSceneRows;

int marcus_scene_rows_init(MarcusSceneRows *, const MMStorageAllocator *);
void marcus_scene_rows_dispose(MarcusSceneRows *);
/* Newly added rows must be initialized by the caller. Failed growth
 * preserves the rows and count. */
int marcus_scene_rows_resize(MarcusSceneRows *, size_t);
/* Keep authored order, append the inserted scenes and apply range overrides.
 * The destination and base must be distinct owners. */
int marcus_scene_rows_rebuild(MarcusSceneRows *destination, const MarcusSceneRows *base,
                              const MMMovieSceneDefinitions *, const MMMovieRangeJournal *);

typedef struct MarcusLiveDocument
{
    MMI32 retail_source;
    MMMovieLiveDefinitions definitions;
    const MarcusBoardStore *boards;
    const MMMovieTextJournal *texts;
    const MMMovieActorTrackJournal *tracks;
    const MMMovieRangeJournal *ranges;
} MarcusLiveDocument;

typedef struct MarcusLiveDocumentServices
{
    void *context;
    int (*resources)(void *);
    int (*retail)(void *);
    int (*mount)(void *, MMI32 source, const void *file);
    int (*install)(void *, const MMPreviewLiveDefinitions *);
    int (*text_names)(void *, const MMMovieTextJournal *, MarcusActorNames *);
} MarcusLiveDocumentServices;

enum { MARCUS_LIVE_DOCUMENT_FAILED, MARCUS_LIVE_DOCUMENT_OK, MARCUS_LIVE_DOCUMENT_MEMORY };
/* Effects run synchronously without reentry or exceptions. Failure stops the
 * sequence and keeps the previous scene rows and actor names. Source and Preview effects that
 * already ran are left for the caller's existing edit recovery. */
int marcus_live_document_apply(const MarcusLiveDocument *, const MarcusLiveDocumentServices *,
                               MarcusSceneRows *scenes, const MarcusSceneRows *base,
                               MarcusActorNames *names, const MarcusActorNames *base_names);
/* The archive open path mounts regenerated boards before selecting the first
 * scene. It uses the same order and source rules as edit restoration. */
int marcus_live_document_mount_boards(const MarcusBoardStore *, const MarcusLiveDocumentServices *);

#ifdef __cplusplus
}
#endif
#endif
