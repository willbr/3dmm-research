/* Movie sound integration converted from engine movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_SOUND_H
#define MM_ENGINE_MOVIE_SOUND_H
#include "tag.h"
#include "chunky_file.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { MM_SOUND_MOVIE=0x4d564945, MM_SOUND_SCENE=0x5343454e,
       MM_SOUND_CHUNK=0x4d534e44, MM_SOUND_CREATOR=0x534f4320 };
#define MM_SOUND_NO_SCENE ((MMU32)0xffffffffUL)
enum { MM_SOUND_BAD_CHILD_ENUM, MM_SOUND_NO_CURRENT_SCENE,
       MM_SOUND_CANNOT_COPY, MM_SOUND_BAD_CHUNK_ENUM };
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
    MMI32 (*count)(void *file,MMU32 type,MMU32 number);
    int (*child)(void *file,MMU32 type,MMU32 number,MMI32 index,MMChunkChild *);
    int (*find)(void *file,MMU32 parent_type,MMU32 parent,MMU32 slot,MMU32 type,MMU32 *number);
    int (*adopt)(void *file,MMU32 parent_type,MMU32 parent,MMU32 type,MMU32 number,MMU32 slot);
    int (*open_tag)(void *,MMTag *,void *cache);
    void (*close_tag)(void *,MMTag *);
    int (*save_tag)(void *,MMTag *,void *cache,int redirect);
    int (*ensure_autosave)(void *);
    int (*use_temp)(void *);
    void (*inspect_source)(void *file); /* Optional legacy unused filename query. */
    int (*copy_chunk)(void *source,MMU32 type,MMU32 number,void *destination,MMU32 *);
    void (*delete_chunk)(void *file,MMU32 type,MMU32 number);
    int (*save_file)(void *file,MMU32 creator);
    int (*is_midi)(void *source_file);
    int (*copy_midi)(void *source_file,void *destination,MMU32 *,void *name);
    int (*copy_wave)(void *source_file,void *destination,MMI32 style,MMU32 *,void *name);
    int (*has_bad_sound_error)(void *);
    void (*mark_gc)(void *);
    MMI32 (*sound_count)(void *file);
    int (*sound_at)(void *file,MMI32 index,MMU32 *number);
    MMI32 (*references)(void *file,MMU32 type,MMU32 number);
    int (*child_at_slot)(void *file,MMU32 type,MMU32 number,MMU32 slot);
    void (*diagnostic)(void *,int);
} MMMovieSoundServices;
/* All operations borrow live movie/cache/file state. Only ensure_autosave and
 * use_temp may replace the movie's working state; other callbacks cannot reenter
 * or change it independently. File graph operations and tag operations may edit
 * their documented data. Counts include every child type; free slots search only
 * sound links. Missing current scene makes next_scene return zero (legacy false).
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
 * unused chunk has any child at slot zero. Trusted chunk enumeration failure is
 * a native assertion precondition; C initializes its otherwise undefined number
 * to zero before diagnosing/continuing. Child enumeration failure in is_child
 * returns false; in adopt it diagnoses and stops with success. */
int mm_movie_sound_unused(int *valid,const MMMovieSoundServices *);
#ifdef __cplusplus
}
#endif
#endif
