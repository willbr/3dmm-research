#ifndef MM_ENGINE_MOVIE_SCENE_JOURNAL_H
#define MM_ENGINE_MOVIE_SCENE_JOURNAL_H

#include "preview_live.h"
#include "preview_actor.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct MMMovieSceneDefinitions
{
    MMCollectionStorage storage;
    MMU32 count;
} MMMovieSceneDefinitions;

typedef struct MMMovieCameraEdit
{
    MMU32 scene;
    MMI32 frame;
    MMU32 camera;
} MMMovieCameraEdit;

typedef struct MMMovieCameraJournal
{
    MMCollectionStorage storage;
    MMU32 count;
} MMMovieCameraJournal;

/* These owners are initialized once and explicitly copied into an initialized
 * destination. Borrowed views last until mutation or disposal. Mutations
 * return 1/0; a failed allocation preserves existing records. */
int mm_movie_scene_definitions_init(MMMovieSceneDefinitions *, const MMStorageAllocator *);
void mm_movie_scene_definitions_dispose(MMMovieSceneDefinitions *);
int mm_movie_scene_definitions_copy(MMMovieSceneDefinitions *, const MMMovieSceneDefinitions *);
const MMPreviewLiveScene *mm_movie_scene_definitions_data(const MMMovieSceneDefinitions *);
int mm_movie_scene_definitions_uses_source(const MMMovieSceneDefinitions *, MMI32 source);
int mm_movie_scene_definitions_append(MMMovieSceneDefinitions *, const MMPreviewLiveScene *);
/* Preserve scene order and retarget all matching records. Existing movie scenes
 * can have no added-scene definition, so zero matches is valid. */
void mm_movie_scene_definitions_retarget(MMMovieSceneDefinitions *, const MMPreviewLiveScene *);

int mm_movie_camera_journal_init(MMMovieCameraJournal *, const MMStorageAllocator *);
void mm_movie_camera_journal_dispose(MMMovieCameraJournal *);
int mm_movie_camera_journal_copy(MMMovieCameraJournal *, const MMMovieCameraJournal *);
const MMMovieCameraEdit *mm_movie_camera_journal_data(const MMMovieCameraJournal *);
/* Sort by scene and signed frame. Repeated keys replace the previous camera;
 * INT32_MIN remains the first-frame sentinel until the scene range is known. */
int mm_movie_camera_journal_set(MMMovieCameraJournal *, const MMMovieCameraEdit *);
const MMMovieCameraEdit *mm_movie_camera_journal_find(const MMMovieCameraJournal *, MMU32 scene, MMU32 *count);
MMPreviewLiveCameraEdits mm_movie_camera_journal_preview(const MMMovieCameraJournal *, MMU32 scene);

typedef struct MMMovieTrackFileServices
{
    void (*retain)(void *file);
    void (*release)(void *file);
} MMMovieTrackFileServices;

typedef struct MMMovieActorTrackEdit
{
    MMU32 scene, actor;
    MMI32 source;
    void *file;
} MMMovieActorTrackEdit;

typedef struct MMMovieActorTrackJournal
{
    MMCollectionStorage storage;
    MMU32 count;
    const MMMovieTrackFileServices *files;
} MMMovieActorTrackJournal;

/* Own one file reference per immutable event track, in scene/actor order.
 * Copies use the same file services and retain independent references for undo.
 * Failed copies or insertions preserve the destination and all references.
 * Callbacks are synchronous and do not reenter the journal. */
int mm_movie_track_journal_init(MMMovieActorTrackJournal *, const MMStorageAllocator *,
                               const MMMovieTrackFileServices *);
void mm_movie_track_journal_dispose(MMMovieActorTrackJournal *);
int mm_movie_track_journal_copy(MMMovieActorTrackJournal *, const MMMovieActorTrackJournal *);
int mm_movie_track_journal_set(MMMovieActorTrackJournal *, const MMMovieActorTrackEdit *);
const MMMovieActorTrackEdit *mm_movie_track_journal_data(const MMMovieActorTrackJournal *);
const MMMovieActorTrackEdit *mm_movie_track_journal_find(const MMMovieActorTrackJournal *, MMU32 scene, MMU32 actor);
int mm_movie_track_journal_has_scene(const MMMovieActorTrackJournal *, MMU32 scene);

typedef struct MMMovieActorPoseEdit
{
    MMU32 scene, actor;
    float offset[3];
    MMPreviewActorShape shape;
    int has_shape;
} MMMovieActorPoseEdit;

typedef struct MMMoviePoseJournal
{
    MMCollectionStorage storage;
    MMU32 count;
} MMMoviePoseJournal;

/* Maintain scene/actor order. Undo copies keep their own storage. A missing
 * pose has zero offset and identity shape. Returning to zero removes an
 * offset-only row. Explicit shape edits, including identity, retain a row. */
int mm_movie_pose_journal_init(MMMoviePoseJournal *, const MMStorageAllocator *);
void mm_movie_pose_journal_dispose(MMMoviePoseJournal *);
int mm_movie_pose_journal_copy(MMMoviePoseJournal *, const MMMoviePoseJournal *);
const MMMovieActorPoseEdit *mm_movie_pose_journal_data(const MMMoviePoseJournal *);
const MMMovieActorPoseEdit *mm_movie_pose_journal_find(const MMMoviePoseJournal *, MMU32 scene, MMU32 actor);
int mm_movie_pose_journal_has_scene(const MMMoviePoseJournal *, MMU32 scene);
int mm_movie_pose_journal_move(MMMoviePoseJournal *, MMU32 scene, MMU32 actor, const float delta[3], float offset[3]);
int mm_movie_pose_journal_shape(MMMoviePoseJournal *, MMU32 scene, MMU32 actor, const MMPreviewActorShape *);
/* Include an actor with an appearance edit in a private export journal.
 * A preexisting row retains both its offset and its shape. */
int mm_movie_pose_journal_include(MMMoviePoseJournal *, MMU32 scene, MMU32 actor);

#ifdef __cplusplus
}
#endif
#endif
