#ifndef MM_ENGINE_PRIVATE_ARCHIVE_H
#define MM_ENGINE_PRIVATE_ARCHIVE_H
#include "chunk_owner.h"
#include "chunk_context.h"
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

enum { MM_PRIVATE_ARCHIVE_READ, MM_PRIVATE_ARCHIVE_EDIT, MM_PRIVATE_ARCHIVE_CREATE, MM_PRIVATE_ARCHIVE_WORKING };

/* Own the supplied file reference on every path. The file is already open
 * in the corresponding mode. CREATE expects an empty private output.
 * File providers must share tokens and retain/release semantics between
 * archives used in the same copy. Writer/editor views borrow this archive.
 * WORKING opens an existing file with staged writes and defers its free map.
 * Publishing its edits requires a save target capable of replacing the file. */
MMPrivateArchive *mm_private_archive_open(void *file, int mode, const MMChunkOwnerFileIO *,
                                         const MMBlockFileServices *, const MMPrivateArchiveProfile *);
/* Consume the decoded forest's file reference. Retain its stored child extents
 * as a read-only archive with an independent index. */
MMPrivateArchive *mm_private_archive_open_forest(const MMBlockFile *, const MMChunkOwnerFileIO *,
                                               const MMBlockFileServices *, const MMPrivateArchiveProfile *);
/* Borrow the owner whose live main file matches this token. The registry
 * follows save publication and automatically removes closed archives. */
MMPrivateArchive *mm_private_archive_find(const MMChunkContext *, void *file);
/* Consume the supplied file reference. Reuse its registered owner for a
 * WORKING open, or register a newly opened owner. */
MMPrivateArchive *mm_private_archive_open_registered(MMChunkContext *, void *file, int mode,
                                                    const MMChunkOwnerFileIO *, const MMBlockFileServices *,
                                                    const MMPrivateArchiveProfile *);
typedef struct MMNamedArchiveServices
{
    MMChunkContext *registry;
    const MMChunkOwnerFileIO *io;
    const MMBlockFileServices *blocks;
    const MMPrivateArchiveProfile *profile;
    void *(*find_file)(void *name);
    void *(*open_file)(void *name, int create, MMU32 flags);
    MMU32 (*file_flags)(void *file);
    void (*open_failed)(int create);
} MMNamedArchiveServices;
/* Reuse a registered working owner or open a file and register its archive.
 * Lookup borrows the file, open transfers it. Callers serialize registry use. */
MMPrivateArchive *mm_private_archive_open_named(const MMNamedArchiveServices *, void *name, int create);
/* Archive references and borrowed views are confined to the owning thread. */
void mm_private_archive_retain(MMPrivateArchive *);
void mm_private_archive_close(MMPrivateArchive *);
/* Reload the index of a read-only backing file and discard its staged data on
 * success. Failed reloads leave the existing graph intact. The caller reports
 * the backing file's current physical write flag. */
int mm_private_archive_reopen(MMPrivateArchive *, int backing_writable);
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
