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

int marcus_archive_install_events(MMPrivateArchive *, MMU32 actor, const char *track,
                                  const MMPrivateArchiveProfile *);

struct MMMovieActorTextEdit;
struct MMTextHost;
enum { MARCUS_ARCHIVE_TEXT_OPEN = -1 };
/* Returns an actor-author status or MARCUS_ARCHIVE_TEXT_OPEN. */
int marcus_archive_replace_text(MMPrivateArchive *, MMU32 actor, const char *path,
                                const struct MMMovieActorTextEdit *, const MMPrivateArchiveProfile *,
                                const struct MMTextHost *, int *detail);

enum { MARCUS_ARCHIVE_ANIMATION_OK, MARCUS_ARCHIVE_ANIMATION_OPEN, MARCUS_ARCHIVE_ANIMATION_EDIT,
       MARCUS_ARCHIVE_ANIMATION_SAVE };
struct MMActorArchiveAnimation;
typedef struct MarcusArchiveAnimationResult
{
    int actor, scene, text, text_detail;
} MarcusArchiveAnimationResult;
int marcus_archive_animate(const char *candidate, const char *track, const MMPrivateArchiveProfile *,
                           const struct MMActorArchiveAnimation *, const char *text_path,
                           const struct MMMovieActorTextEdit *, const struct MMTextHost *,
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
