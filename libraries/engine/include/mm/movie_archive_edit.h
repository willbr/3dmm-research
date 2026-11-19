#ifndef MM_ENGINE_MOVIE_ARCHIVE_EDIT_H
#define MM_ENGINE_MOVIE_ARCHIVE_EDIT_H
#include "chunky_file.h"
#include "collection_storage.h"
#include "movie_title.h"
#include "string_table.h"
#include "text.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Operations on a private archive. The host discards it if an edit fails.
 * A read stream and writer borrow storage through the next read and add.
 * A NULL writer reserves a chunk without a payload write. */
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
    int (*put)(void *, MMU32 type, MMU32 number, const void *bytes, MMU32 size);
    int (*rekey)(void *, MMU32 type, MMU32 number, MMU32 new_number);
    int (*set_title)(void *, MMU32 type, MMU32 number, const MMMovieTitle *);
    /* Replace a chunk's payload through a writer while keeping its key and links. */
    int (*replace)(void *, MMU32 type, MMU32 number, MMU32 bytes, MMStorageWriter *);
} MMMovieArchiveEditor;

enum MMMovieSceneArchiveResult
{
    MM_MOVIE_SCENE_ARCHIVE_OK,
    MM_MOVIE_SCENE_ARCHIVE_ROOT_WRITE,
    MM_MOVIE_SCENE_ARCHIVE_ROOT_LINK,
    MM_MOVIE_SCENE_ARCHIVE_SLOT,
    MM_MOVIE_SCENE_ARCHIVE_CREATE,
    MM_MOVIE_SCENE_ARCHIVE_LINK
};

typedef struct MMMovieArchiveRoot
{
    MMU32 number;
    int created;
} MMMovieArchiveRoot;

/* Select the first MVIE. If absent, create it and attach existing SCEN
 * chunks in their original order of chunk numbers. Publish only on success. */
int mm_movie_archive_ensure_root(const MMMovieArchiveEditor *, MMMovieArchiveRoot *);

/* Append a named one-frame cut scene with the requested background.
 * Preserve the native allocation order for the initial and final event groups. */
int mm_movie_archive_append_scene(const MMMovieArchiveEditor *, MMU32 root, MMU32 scene,
                                  MMI32 source, MMU32 background);

enum MMSceneArchiveEditResult
{
    MM_SCENE_ARCHIVE_EDIT_OK,
    MM_SCENE_ARCHIVE_EDIT_READ_SCENE,
    MM_SCENE_ARCHIVE_EDIT_RANGE,
    MM_SCENE_ARCHIVE_EDIT_READ_FRAMES,
    MM_SCENE_ARCHIVE_EDIT_INVALID_FRAMES,
    MM_SCENE_ARCHIVE_EDIT_INSERT_CAMERA,
    MM_SCENE_ARCHIVE_EDIT_WRITE_FRAMES,
    MM_SCENE_ARCHIVE_EDIT_LINK_FRAMES,
    MM_SCENE_ARCHIVE_EDIT_WRITE_SCENE
};

/* Edit a private archive. Keep the frame group's byte order and unrelated
 * payloads. Opening edits drop the scene thumbnail. A range may only grow. */
int mm_scene_archive_set_camera(const MMMovieArchiveEditor *, MMU32 scene, MMI32 key, MMU32 camera);
int mm_scene_archive_set_range(const MMMovieArchiveEditor *, MMU32 scene, long first, long last);

/* Write the required empty roll call for a rooted archive that has no ACTR
 * chunks and no existing roll-call link. Other archives need no write. */
int mm_movie_archive_ensure_empty_rollcall(const MMMovieArchiveEditor *);

/* Follow the highest child slot of the requested type, or use zero when
 * there is no matching child. */
int mm_movie_archive_next_child(const MMChunkAccess *, MMU32 parent_type, MMU32 parent,
                                 MMU32 type, MMU32 *slot);

enum MMMovieSourceEditResult
{
    MM_MOVIE_SOURCE_EDIT_OK,
    MM_MOVIE_SOURCE_EDIT_READ,
    MM_MOVIE_SOURCE_EDIT_INVALID,
    MM_MOVIE_SOURCE_EDIT_IDS,
    MM_MOVIE_SOURCE_EDIT_MEMORY,
    MM_MOVIE_SOURCE_EDIT_WRITE,
    MM_MOVIE_SOURCE_EDIT_LINK,
    MM_MOVIE_SOURCE_EDIT_UNKNOWN
};

/* Register an ANSI source name (NULL selects retail). An absent source table
 * starts with a retail row at fallback_retail, the movie's implicit ID (1 or 2).
 * The last positive ID matching the VFS key wins. A new ID follows the largest
 * existing ID and starts at 3. Rewrite the candidate's GST in the editor's
 * encoding and byte order, even on a match. Publish the ID after adoption. */
int mm_movie_archive_register_source(const MMMovieArchiveEditor *, const MMTextHost *, MMU32 movie,
                                     MMI32 fallback_retail, const char *name, MMI32 *source_id);

typedef struct MMMovieArchiveSource
{
    MMI32 id;
    const char *name;
    MMI32 mapped;
    size_t ordinal;
} MMMovieArchiveSource;

typedef struct MMMovieArchiveSourceMap
{
    const MMMovieArchiveEditor *destination;
    const MMTextHost *text;
    MMMovieArchiveSource *sources;
    size_t count;
    MMU32 movie;
    MMI32 fallback_retail;
    int result;
} MMMovieArchiveSourceMap;

/* Borrow source names and sort the caller-owned rows by ID. For duplicate IDs
 * the earliest input row wins. Register a source only on its first use. */
void mm_movie_archive_prepare_source_map(MMMovieArchiveSourceMap *);
int mm_movie_archive_map_source(void *map, MMI32 source, MMI32 *mapped);

#ifdef __cplusplus
}
#endif
#endif
