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

/* Apply actor edits in scene and actor order. Returns zero if the merged
 * journal could not be allocated. Otherwise out describes the first failed
 * actor edit or success. Discard the private archive on either failure. */
int marcus_archive_place_edits(MMPrivateArchive *, const struct MMMoviePoseJournal *,
                              const struct MMMovieTextJournal *, const struct MMMovieActorTrackJournal *,
                              const MarcusArchiveDrafts *, MarcusArchiveActorResult *);

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
