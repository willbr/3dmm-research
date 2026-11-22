#ifndef MARCUS_ARCHIVE_FILE_H
#define MARCUS_ARCHIVE_FILE_H
#include "mm/archive_reader.h"
#include "mm/private_archive.h"
#ifdef __cplusplus
extern "C" {
#endif

MMPrivateArchive *marcus_archive_open(const char *path, int mode, const MMPrivateArchiveProfile *);

typedef struct MarcusArchiveInput
{
    void *file;
    MMArchiveReader *reader;
} MarcusArchiveInput;

/* Start with a zeroed input. Failure preserves the current input. */
int marcus_archive_input_open(MarcusArchiveInput *, const char *path, const MMArchiveReaderOptions *,
                               int expanded);
void marcus_archive_input_close(MarcusArchiveInput *);

struct MMMovieActorTextEdit;
struct MMMoviePoseJournal;
struct MMMovieTextJournal;
struct MMMovieActorTrackJournal;
struct MMMovieSceneDefinitions;
struct MMMovieRangeJournal;
struct MMMoviePropJournal;
struct MMMovieCameraJournal;
struct MarcusBoardStore;
struct MMTextHost;
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
