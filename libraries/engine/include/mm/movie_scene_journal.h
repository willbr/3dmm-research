#ifndef MM_ENGINE_MOVIE_SCENE_JOURNAL_H
#define MM_ENGINE_MOVIE_SCENE_JOURNAL_H

#include "preview_live.h"
#include "preview_actor.h"
#include "text_resource.h"

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

typedef struct MMMovieSceneRange
{
    MMU32 scene;
    MMI32 first, last;
} MMMovieSceneRange;

typedef struct MMMovieRangeJournal
{
    MMCollectionStorage storage;
    MMU32 count;
} MMMovieRangeJournal;

/* Store one authored frame range per scene, in scene-number order. */
int mm_movie_range_journal_init(MMMovieRangeJournal *, const MMStorageAllocator *);
void mm_movie_range_journal_dispose(MMMovieRangeJournal *);
int mm_movie_range_journal_copy(MMMovieRangeJournal *, const MMMovieRangeJournal *);
const MMMovieSceneRange *mm_movie_range_journal_data(const MMMovieRangeJournal *);
const MMMovieSceneRange *mm_movie_range_journal_find(const MMMovieRangeJournal *, MMU32 scene);
int mm_movie_range_journal_set(MMMovieRangeJournal *, const MMMovieSceneRange *);

int mm_movie_camera_journal_init(MMMovieCameraJournal *, const MMStorageAllocator *);
void mm_movie_camera_journal_dispose(MMMovieCameraJournal *);
int mm_movie_camera_journal_copy(MMMovieCameraJournal *, const MMMovieCameraJournal *);
const MMMovieCameraEdit *mm_movie_camera_journal_data(const MMMovieCameraJournal *);
/* Sort by scene and signed frame. Repeated keys replace the previous camera;
 * INT32_MIN remains the first-frame sentinel until the scene range is known. */
int mm_movie_camera_journal_set(MMMovieCameraJournal *, const MMMovieCameraEdit *);
const MMMovieCameraEdit *mm_movie_camera_journal_find(const MMMovieCameraJournal *, MMU32 scene, MMU32 *count);
MMPreviewLiveCameraEdits mm_movie_camera_journal_preview(const MMMovieCameraJournal *, MMU32 scene);

typedef struct MMMovieEditFileServices
{
    void (*retain)(void *file);
    void (*release)(void *file);
} MMMovieEditFileServices;

enum { MM_MOVIE_PROP_NAME_BYTES = 192 };

typedef struct MMMoviePropEdit
{
    MMPreviewLiveProp definition;
    void *file;
    char name[MM_MOVIE_PROP_NAME_BYTES];
} MMMoviePropEdit;

typedef struct MMMoviePropJournal
{
    MMCollectionStorage storage;
    MMU32 count;
    const MMMovieEditFileServices *files;
} MMMoviePropJournal;

/* Preserve insertion order and own the name and an optional generated template
 * reference. Copies use the same file services and retain their own references. */
int mm_movie_prop_journal_init(MMMoviePropJournal *, const MMStorageAllocator *, const MMMovieEditFileServices *);
void mm_movie_prop_journal_dispose(MMMoviePropJournal *);
int mm_movie_prop_journal_copy(MMMoviePropJournal *, const MMMoviePropJournal *);
const MMMoviePropEdit *mm_movie_prop_journal_data(const MMMoviePropJournal *);
int mm_movie_prop_journal_append(MMMoviePropJournal *, const MMPreviewLiveProp *, const char *name, void *file);
int mm_movie_prop_journal_started(const MMMoviePropJournal *, MMU32 scene, MMI32 frame);
int mm_movie_prop_placement_valid(const float point[3]);

typedef struct MMMovieLiveDefinitions
{
    const MMMovieSceneDefinitions *scenes;
    const MMMoviePropJournal *props;
} MMMovieLiveDefinitions;

/* The definitions and journals must remain alive and unchanged while Preview
 * reads this view. */
MMPreviewLiveDefinitions mm_movie_definitions_preview(MMMovieLiveDefinitions *);

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
    const MMMovieEditFileServices *files;
} MMMovieActorTrackJournal;

/* Own one file reference per immutable event track, in scene/actor order.
 * Copies use the same file services and retain independent references for undo.
 * Failed copies or insertions preserve the destination and all references.
 * Callbacks are synchronous and do not reenter the journal. */
int mm_movie_track_journal_init(MMMovieActorTrackJournal *, const MMStorageAllocator *,
                               const MMMovieEditFileServices *);
void mm_movie_track_journal_dispose(MMMovieActorTrackJournal *);
int mm_movie_track_journal_copy(MMMovieActorTrackJournal *, const MMMovieActorTrackJournal *);
int mm_movie_track_journal_set(MMMovieActorTrackJournal *, const MMMovieActorTrackEdit *);
const MMMovieActorTrackEdit *mm_movie_track_journal_data(const MMMovieActorTrackJournal *);
const MMMovieActorTrackEdit *mm_movie_track_journal_find(const MMMovieActorTrackJournal *, MMU32 scene, MMU32 actor);
int mm_movie_track_journal_has_scene(const MMMovieActorTrackJournal *, MMU32 scene);

typedef struct MMMovieActorTextEdit
{
    MMU32 scene, actor;
    MMI32 source;
    void *file;
    MMTextAuthorName name;
    int retail_font, custom_material;
} MMMovieActorTextEdit;

typedef struct MMMovieTextJournal
{
    MMCollectionStorage storage;
    MMU32 count;
    const MMMovieEditFileServices *files;
} MMMovieTextJournal;

/* Store one generated text draft per scene and actor. Undo copies keep the
 * validated name and an independent reference to the completed draft. */
int mm_movie_text_journal_init(MMMovieTextJournal *, const MMStorageAllocator *, const MMMovieEditFileServices *);
void mm_movie_text_journal_dispose(MMMovieTextJournal *);
int mm_movie_text_journal_copy(MMMovieTextJournal *, const MMMovieTextJournal *);
const MMMovieActorTextEdit *mm_movie_text_journal_data(const MMMovieTextJournal *);
const MMMovieActorTextEdit *mm_movie_text_journal_find(const MMMovieTextJournal *, MMU32 scene, MMU32 actor);
int mm_movie_text_journal_set(MMMovieTextJournal *, const MMMovieActorTextEdit *);

typedef struct MMMovieTextDescription
{
    const char *utf8;
    size_t bytes;
    MMI32 font_source, shape;
    MMU32 font;
} MMMovieTextDescription;

enum { MM_MOVIE_TEXT_KEEP_FONT, MM_MOVIE_TEXT_RETAIL_FONT, MM_MOVIE_TEXT_UNAVAILABLE_FONT };
int mm_movie_text_font_change(const MMMovieTextDescription *before, const MMMovieTextDescription *after,
                              MMI32 retail_source);
enum { MM_MOVIE_TEXT_CHANGED = 1, MM_MOVIE_TEXT_WRITE_MATERIAL = 2 };
struct MMMaterialAppearance;
/* A NULL requested material preserves the current appearance. A preset is a
 * change even if its numeric appearance equals the current material. On any
 * edit, an existing draft material must be carried over to the new template. */
int mm_movie_text_change(const MMMovieTextDescription *before, const MMMovieTextDescription *after,
                         const struct MMMaterialAppearance *current, const struct MMMaterialAppearance *requested,
                         int preset, const MMMovieActorTextEdit *previous);

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
/* Export a sorted union of pose and appearance keys. The result keeps all
 * transforms and the initialized destination survives allocation failure. */
int mm_movie_pose_journal_export(MMMoviePoseJournal *, const MMMoviePoseJournal *,
                                 const MMMovieTextJournal *, const MMMovieActorTrackJournal *);

#ifdef __cplusplus
}
#endif
#endif
