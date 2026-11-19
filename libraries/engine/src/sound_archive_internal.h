#ifndef MM_SOUND_ARCHIVE_INTERNAL_H
#define MM_SOUND_ARCHIVE_INTERNAL_H
#include "mm/movie_sound_archive.h"
#include "mm/tag.h"

int mm_sound_archive_match(const MMMovieSoundArchiveCue *, const MMTag *, MMU32 child, int queue);
int mm_scene_archive_edit_sound(const MMMovieArchiveEditor *, MMU32 scene, const MMMovieSoundArchiveEdit *,
                                MMU32 added_child);
int mm_actor_archive_edit_sound(const MMMovieArchiveEditor *, MMU32 scene, const MMMovieSoundArchiveEdit *);
#endif
