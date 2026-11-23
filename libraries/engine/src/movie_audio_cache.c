#include "mm/movie_audio_cache.h"
#include "mm/reference.h"
#include <stdlib.h>
#include <string.h>

typedef struct AudioAsset
{
    MMMovieAudioKey key;
    MMMovieAudioMedia media;
    MMReference references;
} AudioAsset;

struct MMMovieAudioCache
{
    AudioAsset **assets;
    size_t count, capacity, bytes, budget;
};

typedef struct AudioVoiceOwner
{
    AudioAsset *asset;
    MMMovieAudioSynth synth;
} AudioVoiceOwner;

static void destroy_asset(void *p)
{
    AudioAsset *asset = p;
    asset->media.release(asset->media.owner);
    free(asset);
}

static void release_asset(AudioAsset *asset)
{
    mm_reference_release(&asset->references, asset, destroy_asset);
}

static void evict_assets(MMMovieAudioCache *cache)
{
    while (cache->count)
        release_asset(cache->assets[--cache->count]);
    cache->bytes = 0;
}

MMMovieAudioCache *mm_movie_audio_cache_create(size_t budget)
{
    MMMovieAudioCache *cache = calloc(1, sizeof(*cache));
    if (cache)
        cache->budget = budget;
    return cache;
}

void mm_movie_audio_cache_clear(MMMovieAudioCache *cache)
{
    evict_assets(cache);
    free(cache->assets);
    cache->assets = NULL;
    cache->capacity = 0;
}

void mm_movie_audio_cache_destroy(MMMovieAudioCache *cache)
{
    if (cache)
    {
        mm_movie_audio_cache_clear(cache);
        free(cache);
    }
}

static int compare_keys(const MMMovieAudioKey *a, const MMMovieAudioKey *b)
{
    if (a->source != b->source) return a->source < b->source ? -1 : 1;
    if (a->type != b->type) return a->type < b->type ? -1 : 1;
    if (a->number != b->number) return a->number < b->number ? -1 : 1;
    if (a->scene != b->scene) return a->scene < b->scene ? -1 : 1;
    return a->child < b->child ? -1 : a->child > b->child;
}

static AudioAsset *find_asset(const MMMovieAudioCache *cache, const MMMovieAudioKey *key, size_t *row)
{
    size_t low = 0, high = cache->count, mid;
    int order;
    while (low < high)
    {
        mid = low + (high - low) / 2;
        order = compare_keys(&cache->assets[mid]->key, key);
        if (order < 0)
            low = mid + 1;
        else if (order > 0)
            high = mid;
        else
            return cache->assets[mid];
    }
    *row = low;
    return NULL;
}

static int reserve_row(MMMovieAudioCache *cache)
{
    AudioAsset **assets;
    size_t capacity;
    if (cache->count < cache->capacity)
        return 1;
    capacity = cache->capacity ? cache->capacity * 2 : 8;
    if (capacity <= cache->capacity || capacity > (size_t)-1 / sizeof(*assets))
        return 0;
    assets = realloc(cache->assets, capacity * sizeof(*assets));
    if (!assets)
        return 0;
    cache->assets = assets;
    cache->capacity = capacity;
    return 1;
}

static int load_asset(MMMovieAudioCache *cache, MMMovieAudioKey key, const MMMovieAudioDecode *decode,
                       AudioAsset **out)
{
    AudioAsset *asset;
    size_t row, bytes, units, width;
    if (key.source)
        key.scene = 0;
    asset = find_asset(cache, &key, &row);
    if (asset)
    {
        *out = asset;
        return MM_MOVIE_AUDIO_CACHE_OK;
    }
    asset = calloc(1, sizeof(*asset));
    if (!asset)
        return MM_MOVIE_AUDIO_CACHE_MEMORY;
    if (!decode->load(decode->context, &asset->media))
    {
        free(asset);
        return MM_MOVIE_AUDIO_CACHE_SOURCE;
    }
    mm_reference_init(&asset->references);
    asset->key = key;
    units = asset->media.clip.midi_events ? asset->media.clip.midi_events : asset->media.clip.sample_frames;
    width = asset->media.clip.midi_events ? sizeof(MMMovieAudioEvent) : 2 * sizeof(float);
    if (units > cache->budget / width)
    {
        release_asset(asset);
        return MM_MOVIE_AUDIO_CACHE_LIMIT;
    }
    bytes = units * width;
    if (bytes > cache->budget - cache->bytes)
    {
        evict_assets(cache);
        row = 0;
    }
    if (!reserve_row(cache))
    {
        release_asset(asset);
        return MM_MOVIE_AUDIO_CACHE_MEMORY;
    }
    memmove(cache->assets + row + 1, cache->assets + row, (cache->count - row) * sizeof(*cache->assets));
    cache->assets[row] = asset;
    ++cache->count;
    cache->bytes += bytes;
    *out = asset;
    return MM_MOVIE_AUDIO_CACHE_OK;
}

static void release_voice(void *p)
{
    AudioVoiceOwner *owner = p;
    if (owner->synth.release)
        owner->synth.release(owner->synth.owner);
    release_asset(owner->asset);
    free(owner);
}

static MMMovieAudioEvent voice_event(void *p, size_t row)
{
    const AudioVoiceOwner *owner = p;
    const MMMovieAudioMedia *media = &owner->asset->media;
    return media->events[row];
}

static void voice_message(void *p, MMU32 message)
{
    const AudioVoiceOwner *owner = p;
    owner->synth.message(owner->synth.owner, message);
}

static void voice_render(void *p, float *samples, size_t frames)
{
    const AudioVoiceOwner *owner = p;
    owner->synth.render(owner->synth.owner, samples, frames);
}

static void voice_reset(void *p)
{
    const AudioVoiceOwner *owner = p;
    owner->synth.reset(owner->synth.owner);
}

int mm_movie_audio_cache_voice(MMMovieAudioCache *cache, const MMMovieAudioKey *key,
                               const MMMovieAudioDecode *decode, MMAudioCounter offset, MMI32 volume,
                               int loop, MMMovieAudioVoice **out)
{
    AudioAsset *asset;
    AudioVoiceOwner *owner;
    MMMovieAudioVoice *voice;
    MMMovieAudioServices services = {NULL, release_voice, voice_event, voice_message, voice_render, voice_reset};
    int status;
    if (!key)
    {
        *out = NULL;
        return MM_MOVIE_AUDIO_CACHE_OK;
    }
    status = load_asset(cache, *key, decode, &asset);
    if (status != MM_MOVIE_AUDIO_CACHE_OK)
        return status;
    if (!asset->media.clip.sample_frames && !asset->media.clip.midi_events)
    {
        *out = NULL;
        return MM_MOVIE_AUDIO_CACHE_OK;
    }
    owner = calloc(1, sizeof(*owner));
    if (!owner)
        return MM_MOVIE_AUDIO_CACHE_MEMORY;
    if (asset->media.clip.midi_events && !decode->synth(decode->context, &owner->synth))
    {
        free(owner);
        return MM_MOVIE_AUDIO_CACHE_SOURCE;
    }
    mm_reference_add(&asset->references);
    owner->asset = asset;
    services.owner = owner;
    voice = mm_movie_audio_prepare(&asset->media.clip, &services, offset, volume, loop);
    if (!voice)
    {
        release_voice(owner);
        return MM_MOVIE_AUDIO_CACHE_MEMORY;
    }
    *out = voice;
    return MM_MOVIE_AUDIO_CACHE_OK;
}
