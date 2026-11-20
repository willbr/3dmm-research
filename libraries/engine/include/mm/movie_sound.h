/* Movie sound integration converted from engine movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_SOUND_H
#define MM_ENGINE_MOVIE_SOUND_H
#include "tag.h"
#include "chunk_edit.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_SOUND_MOVIE=0x4d564945, MM_SOUND_SCENE=0x5343454e,
       MM_SOUND_CHUNK=0x4d534e44, MM_SOUND_CREATOR=0x534f4320, MM_SOUND_PAYLOAD_SLOT=0 };
#define MM_SOUND_NO_SCENE ((MMU32)0xffffffffUL)
enum { MM_SOUND_BAD_CHILD_ENUM, MM_SOUND_NO_CURRENT_SCENE,
       MM_SOUND_CANNOT_COPY, MM_SOUND_BAD_CHUNK_ENUM, MM_SOUND_MISSING_CHUNK, MM_SOUND_DELETE_FAILED };
typedef struct MMMovieSoundState
{
    void *cache;
    MMU32 movie,scene;
} MMMovieSoundState;
typedef struct MMMovieSoundServices
{
    void *context;
    void (*read)(void *,MMMovieSoundState *);
    void *(*file)(void *cache);
    MMChunkEdit (*edit)(void *file);
    int (*open_tag)(void *,MMTag *,void *cache);
    void (*close_tag)(void *,MMTag *);
    int (*save_tag)(void *,MMTag *,void *cache,int redirect);
    int (*ensure_autosave)(void *);
    int (*use_temp)(void *);
    void (*inspect_source)(void *file); /* Optional legacy unused filename query. */
    int (*copy_chunk)(void *source,MMU32 type,MMU32 number,void *destination,MMU32 *);
    int (*save_file)(void *file,MMU32 creator);
    int (*is_midi)(void *source_file);
    int (*copy_midi)(void *source_file,void *destination,MMU32 *,void *name);
    int (*copy_wave)(void *source_file,void *destination,MMI32 style,MMU32 *,void *name);
    int (*has_bad_sound_error)(void *);
    void (*mark_gc)(void *);
    void (*diagnostic)(void *,int);
} MMMovieSoundServices;
/* Locate the first unused sound slot. Other chunk types can use the same
 * slot. Returns a chunk status and publishes the slot only on success. */
int mm_movie_sound_free_slot(const MMChunkAccess *, MMU32 parent_type, MMU32 parent, MMU32 *slot);
/* All operations borrow live movie/cache/file state. Only ensure_autosave and
 * use_temp may replace the movie's working state; other callbacks cannot reenter
 * or change it independently. File graph operations and tag operations may edit
 * their documented data. Counts include every child type; free slots search only
 * sound links. Missing current scene makes next_scene return zero (legacy false).
 * Child enumeration failure in is_child returns false. In adopt it diagnoses
 * and stops with success.
 */
MMU32 mm_movie_sound_next_scene(const MMMovieSoundServices *);
MMU32 mm_movie_sound_next_movie(const MMMovieSoundServices *);
int mm_movie_sound_is_child(void *file,MMU32 type,MMU32 number,const MMMovieSoundServices *);
int mm_movie_sound_adopt(void *file,MMU32 scene,const MMMovieSoundServices *);
/* A non-document SID still obtains the selected file before returning unchanged.
 * Equal chunk numbers never reopen/rebind the tag, even with another cache.
 * Changed numbers open a local tag before closing/replacing the original; failed
 * open leaves the original unchanged. Scene/cache overrides preserve native use.
 */
int mm_movie_sound_resolve(MMTag *,MMU32 child,MMU32 scene,void *cache,const MMMovieSoundServices *);
int mm_movie_sound_save(MMTag *,const MMMovieSoundServices *);
/* Adoption publishes its candidate child ID before attempting the graph edit.
 * It repeats current-scene lookup when choosing a new slot, as in native code. */
int mm_movie_sound_child(MMU32 number,MMU32 *child,const MMMovieSoundServices *);
/* Copy/import publish factory outputs without rollback. Failed root adoption
 * deletes the copied root but leaves the output number; failed save retains the
 * adoption. Missing scene diagnoses and continues (its result was unused).
 * Only successful physical-file import marks close-time sound GC. */
int mm_movie_sound_copy(void *source,MMU32 number,MMU32 *output,const MMMovieSoundServices *);
int mm_movie_sound_import(void *source,MMI32 style,MMU32 *output,void *name,const MMMovieSoundServices *);
/* Scan every sound, including roots not linked to the movie. Fewer than two
 * parent links means unused; with valid output requested, continue until an
 * unused chunk has any child at slot zero. An unreadable index entry returns
 * a chunk error and preserves the results found up to that entry. */
int mm_movie_sound_scan_unused(const MMChunkAccess *, int *unused, int *valid);
/* Use the movie's working index and report an unreadable entry to the host. */
int mm_movie_sound_unused(int *valid,const MMMovieSoundServices *);
#ifdef __cplusplus
}
#endif
#endif
