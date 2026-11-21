#ifndef MM_ENGINE_PRIVATE_ARCHIVE_H
#define MM_ENGINE_PRIVATE_ARCHIVE_H
#include "chunk_owner.h"
#include "movie_archive_edit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMPrivateArchive MMPrivateArchive;
typedef struct MMPrivateArchiveProfile
{
    unsigned unit;
    int encoding, swapped_unicode_encoding, expanded, trash_unused;
    MMChunkLegacyName legacy_name;
    MMStringConvert convert;
} MMPrivateArchiveProfile;

enum { MM_PRIVATE_ARCHIVE_READ, MM_PRIVATE_ARCHIVE_EDIT, MM_PRIVATE_ARCHIVE_CREATE };

/* Own the supplied file reference on every path. The file is already open
 * in the corresponding mode. CREATE expects an empty private output.
 * File providers must share tokens and retain/release semantics between
 * archives used in the same copy. Writer/editor views borrow this archive.
 * Edits save in place on private files, which callers discard after failure. */
MMPrivateArchive *mm_private_archive_open(void *file, int mode, const MMChunkOwnerFileIO *,
                                         const MMBlockFileServices *, const MMPrivateArchiveProfile *);
void mm_private_archive_close(MMPrivateArchive *);
MMArchiveWriter mm_private_archive_writer(MMPrivateArchive *);
MMMovieArchiveEditor mm_private_archive_editor(MMPrivateArchive *);
int mm_private_archive_copy(MMPrivateArchive *source, MMPrivateArchive *destination, MMU32 type,
                            MMU32 number, int clone, MMU32 *copy);
int mm_private_archive_save(MMPrivateArchive *, MMU32 creator);

#ifdef __cplusplus
}
#endif
#endif
