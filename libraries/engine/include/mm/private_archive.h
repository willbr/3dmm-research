#ifndef MM_ENGINE_PRIVATE_ARCHIVE_H
#define MM_ENGINE_PRIVATE_ARCHIVE_H
#include "chunk_owner.h"
#include "movie_archive_edit.h"
#include "text_store.h"

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
/* Archive references and borrowed views are confined to the owning thread. */
void mm_private_archive_retain(MMPrivateArchive *);
void mm_private_archive_close(MMPrivateArchive *);
MMChunkOwnerRead *mm_private_archive_reader(MMPrivateArchive *);
MMArchiveWriter mm_private_archive_writer(MMPrivateArchive *);
MMMovieArchiveEditor mm_private_archive_editor(MMPrivateArchive *);
/* Borrow the archive and editor. Stored TEXT uses a retained file extent
 * independent of the editor's transient decoded block. A NULL archive
 * supplies the format for an empty draft. */
MMRichTextArchive mm_private_archive_rich_text(MMPrivateArchive *, const MMMovieArchiveEditor *);
int mm_private_archive_copy(MMPrivateArchive *source, MMPrivateArchive *destination, MMU32 type,
                            MMU32 number, int clone, MMU32 *copy);
struct MMChunkEndpoint;
struct MMChunkFileCopy;
struct MMChunkEndpoint mm_private_archive_copy_endpoint(MMPrivateArchive *, struct MMChunkFileCopy *);
struct MMChunkEndpoint mm_private_archive_copy_target(MMPrivateArchive *, struct MMChunkFileCopy *);
int mm_private_archive_bind_font(MMPrivateArchive *, const struct MMChunkEndpoint *source,
                                MMU32 template_number, MMU32 font_number);
int mm_private_archive_save(MMPrivateArchive *, MMU32 creator);

#ifdef __cplusplus
}
#endif
#endif
