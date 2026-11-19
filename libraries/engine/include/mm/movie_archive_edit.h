#ifndef MM_ENGINE_MOVIE_ARCHIVE_EDIT_H
#define MM_ENGINE_MOVIE_ARCHIVE_EDIT_H
#include "chunky_file.h"
#include "collection_storage.h"
#include "string_table.h"
#include "text.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Operations on a private archive. The host discards it if an edit fails.
 * A read stream and writer borrow storage through the next read and add. */
typedef struct MMMovieArchiveEditor
{
    void *context;
    unsigned text_unit;
    int system, big_endian;
    MMStringConvert convert_text;
    MMChunkAccess (*chunks)(void *);
    int (*read)(void *, MMU32 type, MMU32 number, MMReadStream *);
    int (*add)(void *, MMU32 type, MMU32 bytes, MMU32 *number, MMStorageWriter *);
    int (*adopt)(void *, MMU32 parent_type, MMU32 parent, MMU32 type, MMU32 number, MMU32 slot);
    void (*unlink)(void *, MMU32 parent_type, MMU32 parent, MMU32 type, MMU32 number, MMU32 slot);
} MMMovieArchiveEditor;

enum MMMovieSourceEditResult
{
    MM_MOVIE_SOURCE_EDIT_OK,
    MM_MOVIE_SOURCE_EDIT_READ,
    MM_MOVIE_SOURCE_EDIT_INVALID,
    MM_MOVIE_SOURCE_EDIT_IDS,
    MM_MOVIE_SOURCE_EDIT_MEMORY,
    MM_MOVIE_SOURCE_EDIT_WRITE,
    MM_MOVIE_SOURCE_EDIT_LINK
};

/* Register an ANSI source name (NULL selects retail). An absent source table
 * starts with a retail row at fallback_retail, the movie's implicit ID (1 or 2).
 * The last positive ID matching the VFS key wins. A new ID follows the largest
 * existing ID and starts at 3. Rewrite the candidate's GST in the editor's
 * encoding and byte order, even on a match. Publish the ID after adoption. */
int mm_movie_archive_register_source(const MMMovieArchiveEditor *, const MMTextHost *, MMU32 movie,
                                     MMI32 fallback_retail, const char *name, MMI32 *source_id);

#ifdef __cplusplus
}
#endif
#endif
