#include "movie_sound.h"

int mm_kauai_movie_sound_read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                             void **out, MMI32 *bytes)
{
    return mm_sound_fetch(cache, type, number, block, out, bytes, mm_kauai_movie_sound_host());
}

MMSoundOwner *mm_kauai_movie_sound_fetch(MMResourceCache *cache, MMU32 type, MMU32 number)
{
    return mm_resource_cache_fetch_owned_chunk(cache, type, number, mm_kauai_movie_sound_read,
                                               mm_sound_owner_services());
}

void mm_kauai_movie_sound_play(void *manager, const MMFrameSoundRequest *request)
{
    MMSoundResourceServices services = {0};
    MMSoundLastMidi *last = mm_kauai_movie_sound_device(&services, manager);
    mm_sound_play(mm_sound_owner_state(request->sound), last, request->object, request->loop,
                  request->queued, request->volume, request->priority, request->actor, request->offset,
                  &services);
}
