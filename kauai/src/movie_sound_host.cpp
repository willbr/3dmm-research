/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "kauai_core.h"
#include "handle_services.h"
#include "movie_sound.h"
#include "sndm.h"
#include "mm/movie_session.h"

ASSERTNAME

const MMSoundResourceHost *mm_kauai_movie_sound_host(void)
{
    static const auto services = [] {
        MMSoundResourceServices s = {};
        const unsigned short one = 1;
        s.host_big_endian = *reinterpret_cast<const unsigned char *>(&one) == 0;
        s.system = koskCur;
        s.invalidate_output = [](void *, void *out, size_t bytes) { TrashPvCb(out, static_cast<LegacyLong>(bytes)); };
        s.decode = KauaiDecodeBlockHandle;
        s.graph = [](void *, void *file, MMMovieSessionGraph *out) {
            *out = mm_movie_owner_graph(static_cast<PChunkyFile>(file)->ReadOwner());
        };
        s.read_name = [](void *, void *file, MMResourceCache *cache, MMU32 type, MMU32 number,
                         MMSoundOwner *sound) -> int {
            const auto storage = mm_sound_owner_name(sound);
            if (cache)
                return ReadResourceName(cache, type, number, storage);
            String name;
            const bool found = static_cast<PChunkyFile>(file)->FGetName(type, number, &name);
            mm_string_value_copy(&storage, name.Pst());
            return found;
        };
        s.publish_invalid = [](void *, void *out, int invalid) { *static_cast<bool *>(out) = invalid != 0; };
        s.bad_order = [](void *) { Assert(false, "bad MovieSoundFile"); };
        return s;
    }();
    // Preserve the historical cache charge, including the native title width.
    constexpr size_t prefix = sizeof(void *) == 8 ? 40 : 28 + DebugShip(4, 0);
    constexpr size_t bytes =
        prefix + sizeof(MMSoundResource) + sizeof(MMI32) + (MM_STRING_MAX + 2) * sizeof(achar) + 1;
    constexpr auto cacheBytes = static_cast<MMI32>((bytes + sizeof(void *) - 1) & ~(sizeof(void *) - 1));
    static const MMSoundResourceHost host = {
        &services, KauaiResourceObjectAllocator(), cacheBytes, sizeof(achar),
#ifdef DEBUG
        [](void *p) { MarkPv(p); },
        mm_resource_cache_mark
#else
        nullptr, nullptr
#endif
    };
    return &host;
}

MMSoundLastMidi *mm_kauai_movie_sound_device(MMSoundResourceServices *s, void *manager)
{
    s->context = manager;
    s->playing = [](void *p, MMI32 instance) -> int { return static_cast<PSoundManager>(p)->FPlaying(instance); };
    s->stop = [](void *p, MMI32 queue) { static_cast<PSoundManager>(p)->StopAll(queue, sclNil); };
    s->play = [](void *p, const MMSoundResource *sound, MMI32 queue, MMI32 volume, MMI32 repeat,
                 MMU32 start, MMI32 priority, MMI32 soundClass) -> MMI32 {
        return static_cast<PSoundManager>(p)->SiiPlay({sound->cache, nullptr}, sound->type, sound->number, queue, volume,
                                                     repeat, start, priority, soundClass);
    };
    return manager ? &static_cast<PSoundManager>(manager)->_lastMovieMidi : nullptr;
}
