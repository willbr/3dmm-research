#ifndef KAUAI_MOVIE_SOUND_H
#define KAUAI_MOVIE_SOUND_H

#include "mm/frame_sound.h"
#include "mm/sound_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

const MMSoundResourceHost *mm_kauai_movie_sound_host(void);
int mm_kauai_movie_sound_read(MMResourceCache *, MMU32 type, MMU32 number, MMOwnedBlock *, void **, MMI32 *);
MMSoundOwner *mm_kauai_movie_sound_fetch(MMResourceCache *, MMU32 type, MMU32 number);
void mm_kauai_movie_sound_play(void *manager, const MMFrameSoundRequest *);
void mm_kauai_movie_sound_device(MMSoundResourceServices *, void *manager);

#ifdef __cplusplus
}
#endif
#endif
