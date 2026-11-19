#ifndef MM_ENGINE_MOVIE_SCENE_JOURNAL_H
#define MM_ENGINE_MOVIE_SCENE_JOURNAL_H

#include "preview_live.h"

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

#ifdef __cplusplus
}
#endif
#endif
