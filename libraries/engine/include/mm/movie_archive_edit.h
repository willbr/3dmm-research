#ifndef MM_ENGINE_MOVIE_ARCHIVE_EDIT_H
#define MM_ENGINE_MOVIE_ARCHIVE_EDIT_H
#include "chunky_file.h"
#include "collection_storage.h"
#include "movie_title.h"
#include "string_table.h"
#include "tag.h"
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
    /* Update a byte range in an existing payload without changing its length. */
    int (*patch)(void *, MMU32 type, MMU32 number, MMU32 offset, const void *bytes, MMU32 size);
    void (*discard)(void *, MMU32 type, MMU32 number); /* Optional unlinked chunk cleanup. */
} MMMovieArchiveEditor;

/* Primitive access for copying and linking document-owned asset subtrees. */
typedef struct MMMovieEmbeddedArchive
{
    void *context;
    MMChunkAccess (*chunks)(void *);
    int (*copy_reference)(void *, const MMTag *, MMU32 *number, int *created);
    int (*adopt)(void *, MMU32 parent_type, MMU32 parent, const MMChunkChild *, int clear_loner);
    void (*discard)(void *, MMU32 type, MMU32 number);
} MMMovieEmbeddedArchive;

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
 * Preserve the native allocation order for the initial and final event groups.
 * Requires discard for cleanup when a scene or group write fails. */
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

typedef struct MMSceneArchiveText
{
    MMU32 slot, number;
} MMSceneArchiveText;

enum MMSceneArchiveTextResult
{
    MM_SCENE_ARCHIVE_TEXT_OK,
    MM_SCENE_ARCHIVE_TEXT_READ_START,
    MM_SCENE_ARCHIVE_TEXT_INVALID_START,
    MM_SCENE_ARCHIVE_TEXT_MEMORY,
    MM_SCENE_ARCHIVE_TEXT_LINK,
    MM_SCENE_ARCHIVE_TEXT_CHANGED,
    MM_SCENE_ARCHIVE_TEXT_WRITE_BOX,
    MM_SCENE_ARCHIVE_TEXT_WRITE_START
};

/* Resolve the first reference to each text slot in authored start-event order.
 * On success the caller owns the list, which may be empty. */
int mm_scene_archive_list_text(const MMMovieArchiveEditor *, MMU32 scene, MMSceneArchiveText **, MMU32 *count);
void mm_scene_archive_free_text_list(MMSceneArchiveText *);

typedef struct MMSceneArchiveTextEdit
{
    MMU32 scene;
    MMI32 opening_frame;
    const MMSceneArchiveText *original; /* NULL inserts at the first free text slot. */
    void *context;
    int (*write_box)(void *, const MMSceneArchiveText *original, MMU32 *number); /* NULL deletes. */
} MMSceneArchiveTextEdit;

/* Retain the existing slot on replacement. Insertion appends a start event;
 * deletion removes every start event referring to the selected slot.
 * Discard the private candidate archive on failure. */
int mm_scene_archive_edit_text(const MMMovieArchiveEditor *, const MMSceneArchiveTextEdit *);

/* Write the required empty roll call for a rooted archive that has no ACTR
 * chunks and no existing roll-call link. Other archives need no write. */
int mm_movie_archive_ensure_empty_rollcall(const MMMovieArchiveEditor *);

/* Follow the highest child slot of the requested type, or use zero when
 * there is no matching child. */
int mm_movie_archive_next_child(const MMChunkAccess *, MMU32 parent_type, MMU32 parent,
                                 MMU32 type, MMU32 *slot);
/* Copy or reuse the source subtree, adopt it once and publish the new number. */
int mm_movie_archive_embed_tag(const MMMovieEmbeddedArchive *, MMU32 parent_type, MMU32 parent, MMTag *);

/* Before detaching a scene, keep background and material subtrees that would
 * lose their last parent. Raw tags and undo can still refer to these by number.
 * The callback marks an existing asset as a loner without changing index
 * positions. No flags are published if the deletion preview fails. */
int mm_movie_archive_preserve_scene_assets(const MMChunkAccess *, MMU32 scene,
                                              const MMStorageAllocator *, void *context,
                                              void (*keep)(void *, MMU32 type, MMU32 number));

/* Write and adopt the replacement before unlinking the old child. */
int mm_movie_archive_replace_child(const MMMovieArchiveEditor *, MMU32 parent_type, MMU32 parent,
                                   const MMChunkChild *, int had_child, const unsigned char *, MMU32 bytes);
int mm_movie_archive_retarget_background(const MMMovieArchiveEditor *, MMU32 scene, MMI32 source, MMU32 background);
int mm_movie_archive_rebind_backgrounds(const MMMovieArchiveEditor *, const MMMovieEmbeddedArchive *, MMU32 scene);
enum { MM_STORYBOARD_CHUNK = 0x53544244UL };
/* The background graph has already been copied into this private archive. */
int mm_movie_archive_embed_storyboard(const MMMovieArchiveEditor *, MMU32 scene, MMU32 background,
                                       const unsigned char *metadata, MMU32 bytes);

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
