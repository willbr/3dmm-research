/* Movie sound integration converted from engine movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MOVIE_SOUND_H
#define THREE_MM_ENGINE_MOVIE_SOUND_H
#include "tag.h"
#include "chunky_file.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { THREE_MM_SOUND_MOVIE=0x4d564945, THREE_MM_SOUND_SCENE=0x5343454e,
       THREE_MM_SOUND_CHUNK=0x4d534e44, THREE_MM_SOUND_CREATOR=0x534f4320 };
#define THREE_MM_SOUND_NO_SCENE ((ThreeMMU32)0xffffffffUL)
enum { THREE_MM_SOUND_BAD_CHILD_ENUM, THREE_MM_SOUND_NO_CURRENT_SCENE,
       THREE_MM_SOUND_CANNOT_COPY, THREE_MM_SOUND_BAD_CHUNK_ENUM };
typedef struct ThreeMMMovieSoundState
{
    void *cache;
    ThreeMMU32 movie,scene;
} ThreeMMMovieSoundState;
typedef struct ThreeMMMovieSoundServices
{
    void *context;
    void (*read)(void *,ThreeMMMovieSoundState *);
    void *(*file)(void *cache);
    ThreeMMI32 (*count)(void *file,ThreeMMU32 type,ThreeMMU32 number);
    int (*child)(void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMI32 index,ThreeMMChunkChild *);
    int (*find)(void *file,ThreeMMU32 parent_type,ThreeMMU32 parent,ThreeMMU32 slot,ThreeMMU32 type,ThreeMMU32 *number);
    int (*adopt)(void *file,ThreeMMU32 parent_type,ThreeMMU32 parent,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 slot);
    int (*open_tag)(void *,ThreeMMTag *,void *cache);
    void (*close_tag)(void *,ThreeMMTag *);
    int (*save_tag)(void *,ThreeMMTag *,void *cache,int redirect);
    int (*ensure_autosave)(void *);
    int (*use_temp)(void *);
    void (*inspect_source)(void *file); /* Optional legacy unused filename query. */
    int (*copy_chunk)(void *source,ThreeMMU32 type,ThreeMMU32 number,void *destination,ThreeMMU32 *);
    void (*delete_chunk)(void *file,ThreeMMU32 type,ThreeMMU32 number);
    int (*save_file)(void *file,ThreeMMU32 creator);
    int (*is_midi)(void *source_file);
    int (*copy_midi)(void *source_file,void *destination,ThreeMMU32 *,void *name);
    int (*copy_wave)(void *source_file,void *destination,ThreeMMI32 style,ThreeMMU32 *,void *name);
    int (*has_bad_sound_error)(void *);
    void (*mark_gc)(void *);
    ThreeMMI32 (*sound_count)(void *file);
    int (*sound_at)(void *file,ThreeMMI32 index,ThreeMMU32 *number);
    ThreeMMI32 (*references)(void *file,ThreeMMU32 type,ThreeMMU32 number);
    int (*child_at_slot)(void *file,ThreeMMU32 type,ThreeMMU32 number,ThreeMMU32 slot);
    void (*diagnostic)(void *,int);
} ThreeMMMovieSoundServices;
/* All operations borrow live movie/cache/file state. Only ensure_autosave and
 * use_temp may replace the movie's working state; other callbacks cannot reenter
 * or change it independently. File graph operations and tag operations may edit
 * their documented data. Counts include every child type; free slots search only
 * sound links. Missing current scene makes next_scene return zero (legacy false).
 */
ThreeMMU32 three_mm_movie_sound_next_scene(const ThreeMMMovieSoundServices *);
ThreeMMU32 three_mm_movie_sound_next_movie(const ThreeMMMovieSoundServices *);
int three_mm_movie_sound_is_child(void *file,ThreeMMU32 type,ThreeMMU32 number,const ThreeMMMovieSoundServices *);
int three_mm_movie_sound_adopt(void *file,ThreeMMU32 scene,const ThreeMMMovieSoundServices *);
/* A non-document SID still obtains the selected file before returning unchanged.
 * Equal chunk numbers never reopen/rebind the tag, even with another cache.
 * Changed numbers open a local tag before closing/replacing the original; failed
 * open leaves the original unchanged. Scene/cache overrides preserve native use.
 */
int three_mm_movie_sound_resolve(ThreeMMTag *,ThreeMMU32 child,ThreeMMU32 scene,void *cache,const ThreeMMMovieSoundServices *);
int three_mm_movie_sound_save(ThreeMMTag *,const ThreeMMMovieSoundServices *);
/* Adoption publishes its candidate child ID before attempting the graph edit.
 * It repeats current-scene lookup when choosing a new slot, as in native code. */
int three_mm_movie_sound_child(ThreeMMU32 number,ThreeMMU32 *child,const ThreeMMMovieSoundServices *);
/* Copy/import publish factory outputs without rollback. Failed root adoption
 * deletes the copied root but leaves the output number; failed save retains the
 * adoption. Missing scene diagnoses and continues (its result was unused).
 * Only successful physical-file import marks close-time sound GC. */
int three_mm_movie_sound_copy(void *source,ThreeMMU32 number,ThreeMMU32 *output,const ThreeMMMovieSoundServices *);
int three_mm_movie_sound_import(void *source,ThreeMMI32 style,ThreeMMU32 *output,void *name,const ThreeMMMovieSoundServices *);
/* Scan every sound, including roots not linked to the movie. Fewer than two
 * parent links means unused; with valid output requested, continue until an
 * unused chunk has any child at slot zero. Trusted chunk enumeration failure is
 * a native assertion precondition; C initializes its otherwise undefined number
 * to zero before diagnosing/continuing. Child enumeration failure in is_child
 * returns false; in adopt it diagnoses and stops with success. */
int three_mm_movie_sound_unused(int *valid,const ThreeMMMovieSoundServices *);
#ifdef __cplusplus
}
#endif
#endif
