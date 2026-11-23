#ifndef MARCUS_ARCHIVE_FILE_H
#define MARCUS_ARCHIVE_FILE_H
#include "mm/movie_publish.h"
#include "mm/native_archive_file.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { MARCUS_WORKING_MOVIE, MARCUS_WORKING_BOARD, MARCUS_WORKING_TIMING };
typedef struct MarcusWorkingFiles
{
    void *context;
    /* Only OK transfers ownership of the new directory and the candidate path. */
    MMMovieStageResult (*reserve)(void *, const char *directory, const char *leaf);
    int (*copy_writable)(void *);
} MarcusWorkingFiles;

/* The caller's file lease owns the candidate after reservation, even if copying fails.
 * The stamp is a decimal clock value supplied by the filesystem host. */
int marcus_archive_stage_working_file(int kind, const char *stamp, int copy_source, const MarcusWorkingFiles *);

struct MMTextHost;
/* Start with a zeroed input. Failure preserves the current input. */
int marcus_archive_input_open_profile(MMNativeArchiveInput *, const char *path, const MMPrivateArchiveProfile *);
/* Read a counted archive name into a bounded UTF-8 buffer. Return the bytes
 * written. Sound assets retain their historical byte truncation. */
size_t marcus_archive_name_utf8(MMArchiveReader *, MMU32 position, const MMPrivateArchiveProfile *,
                                const struct MMTextHost *, char *output, size_t capacity, int whole_characters);
/* Encode one counted legacy string, terminating the output and truncating at
 * a character boundary. On invalid UTF-16, written stays unchanged. */
int marcus_archive_text_utf8(const struct MMTextHost *, const void *text, size_t bytes, int encoding,
                             char *output, size_t capacity, size_t *written);
int marcus_archive_source_utf8(const MMMovieSourceName *, const MMPrivateArchiveProfile *,
                               const struct MMTextHost *, char output[MM_MOVIE_SOURCE_NAME_BYTES], size_t *written);
const char *marcus_archive_directory_error(int status, int payload);

struct MarcusObject;
typedef struct MarcusArchiveClipboard MarcusArchiveClipboard;
typedef struct MarcusClipboardError
{
    const char *message;
    int actor, source, scene, registration;
} MarcusClipboardError;

/* Copy into a fresh private archive. Publish the clipboard only after saving
 * its track and retained assets. The host owns both the file and clipboard. */
MarcusArchiveClipboard *marcus_archive_copy_object(const char *movie, const char *candidate,
    MMU32 scene, MMI32 frame, const struct MarcusObject *, const MMPrivateArchiveProfile *,
    const struct MMTextHost *, MMU32 creator, MarcusClipboardError *);
/* Paste into a disposable working copy. The retained track and source rows
 * are immutable, so the same clipboard can be used in different movies. */
int marcus_archive_paste_object(const MarcusArchiveClipboard *, const char *track, const char *candidate,
    MMU32 scene, MMI32 frame, MMI32 id, const MMPrivateArchiveProfile *, const struct MMTextHost *,
    MMU32 creator, MarcusClipboardError *);
void marcus_archive_clipboard_destroy(MarcusArchiveClipboard *);

struct MMMovieActorTextEdit;
struct MMMoviePoseJournal;
struct MMMovieTextJournal;
struct MMMovieActorTrackJournal;
struct MMMovieSceneDefinitions;
struct MMMovieRangeJournal;
struct MMMoviePropJournal;
struct MMMovieCameraJournal;
struct MarcusBoardStore;
enum { MARCUS_ARCHIVE_TEXT_OPEN = -1 };

typedef struct MarcusArchiveDrafts
{
    void *context;
    /* Open a retained journal file for reading and transfer the archive reference. */
    MMPrivateArchive *(*open)(void *, const void *file);
    const struct MMTextHost *text;
} MarcusArchiveDrafts;

typedef struct MarcusArchiveActorResult
{
    int status, text, text_detail, costume;
} MarcusArchiveActorResult;

typedef struct MarcusArchiveMovieEdits
{
    const struct MMMovieSceneDefinitions *scenes;
    const struct MarcusBoardStore *boards;
    const struct MMMovieRangeJournal *ranges;
    const struct MMMoviePropJournal *props;
    const struct MMMoviePoseJournal *poses;
    const struct MMMovieTextJournal *texts;
    const struct MMMovieActorTrackJournal *costumes;
    const struct MMMovieCameraJournal *cameras;
} MarcusArchiveMovieEdits;

enum
{
    MARCUS_MATERIALIZE_OK, MARCUS_MATERIALIZE_OPEN, MARCUS_MATERIALIZE_SCENE,
    MARCUS_MATERIALIZE_SOURCE, MARCUS_MATERIALIZE_BOARD, MARCUS_MATERIALIZE_FRAME,
    MARCUS_MATERIALIZE_LIBRARY, MARCUS_MATERIALIZE_TEXT_OPEN, MARCUS_MATERIALIZE_TEXT_NAME,
    MARCUS_MATERIALIZE_INSERT, MARCUS_MATERIALIZE_MEMORY, MARCUS_MATERIALIZE_ACTOR,
    MARCUS_MATERIALIZE_ROLLCALL, MARCUS_MATERIALIZE_SAVE
};
typedef struct MarcusArchiveMovieResult
{
    int status, detail;
    const char *board_error;
    MarcusArchiveActorResult actor;
} MarcusArchiveMovieResult;

/* The path is a private working file. Apply and save all journal edits in
 * movie order. Discard the working file if any stage fails. */
int marcus_archive_materialize(const char *path, int empty_movie, const char *templates,
                               const MarcusArchiveMovieEdits *, const MarcusArchiveDrafts *,
                               const MMPrivateArchiveProfile *, MMU32 creator, MarcusArchiveMovieResult *);

enum { MARCUS_ARCHIVE_ANIMATION_OK, MARCUS_ARCHIVE_ANIMATION_OPEN, MARCUS_ARCHIVE_ANIMATION_EDIT,
       MARCUS_ARCHIVE_ANIMATION_SAVE };
struct MMActorArchiveAnimation;
typedef struct MarcusArchiveAnimationResult
{
    MarcusArchiveActorResult actor;
    int scene;
} MarcusArchiveAnimationResult;
int marcus_archive_animate(const char *candidate, const char *track, const MMPrivateArchiveProfile *,
                           const struct MMActorArchiveAnimation *, const struct MMMovieActorTextEdit *,
                           const MarcusArchiveDrafts *,
                           MMU32 creator, MarcusArchiveAnimationResult *);

#ifdef __cplusplus
}
#include <filesystem>
#include <memory>
using MarcusArchive = std::unique_ptr<MMPrivateArchive, decltype(&mm_private_archive_close)>;
MarcusArchive MarcusOpenArchive(const std::filesystem::path &, int mode);
class String;
MMChunkNameValue MarcusArchiveName(String &, void *wire);
#endif
#endif
