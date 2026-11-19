#include "mm/movie_audio.h"
#include "mm/audio_mixer.h"
#include <stdlib.h>

struct MMMovieAudioVoice
{
    MMMovieAudioVoice *next;
    MMMovieAudioClip clip;
    MMMovieAudioServices services;
    MMAudioCounter frames, position;
    size_t event;
    float volume;
    int loop;
};

typedef struct AudioLane
{
    struct AudioLane *next;
    MMAudioCounter key;
    MMMovieAudioVoice *head, *tail;
    int muted, solo;
} AudioLane;

struct MMMovieAudio
{
    AudioLane *lanes;
    size_t mixed, solos;
    int suspended, muted;
    MMMovieAudioStats stats;
};

static AudioLane **lane_slot(MMMovieAudio *audio, MMAudioCounter key)
{
    AudioLane **slot;
    for (slot = &audio->lanes; *slot && mm_audio_count_compare((*slot)->key, key) < 0; slot = &(*slot)->next)
    {
    }
    return slot;
}

static AudioLane *insert_lane(AudioLane **slot, MMAudioCounter key)
{
    AudioLane *lane;
    lane = (AudioLane *)calloc(1, sizeof(*lane));
    if (lane)
    {
        lane->key = key;
        lane->next = *slot;
        *slot = lane;
    }
    return lane;
}

static void remove_lane(AudioLane **slot)
{
    AudioLane *lane;
    lane = *slot;
    *slot = lane->next;
    free(lane);
}

MMAudioCounter mm_movie_audio_lane(int actor_sound, MMI32 actor, MMI32 style)
{
    MMU32 owner;
    owner = actor_sound ? (MMU32)actor : 0;
    /* Retain the native unsigned lane order and its mixing order. */
    return mm_audio_counter((owner << 16) | (MMU32)style, (actor_sound ? 0x80000000UL : 0) | (owner >> 16));
}

MMMovieAudio *mm_movie_audio_create(void)
{
    MMMovieAudio *audio;
    audio = (MMMovieAudio *)calloc(1, sizeof(*audio));
    if (audio)
        audio->suspended = 1;
    return audio;
}

void mm_movie_audio_voice_destroy(MMMovieAudioVoice *voice)
{
    if (voice)
    {
        voice->services.release(voice->services.owner);
        free(voice);
    }
}

static void drop_head(AudioLane *lane)
{
    MMMovieAudioVoice *voice;
    voice = lane->head;
    lane->head = voice->next;
    if (!lane->head)
        lane->tail = NULL;
    mm_movie_audio_voice_destroy(voice);
}

static int unfinished(const MMMovieAudioVoice *voice)
{
    return mm_audio_count_compare(voice->position, voice->frames) < 0;
}

static void collect_lane(AudioLane *lane)
{
    while (lane->head && !unfinished(lane->head))
        drop_head(lane);
}

void mm_movie_audio_destroy(MMMovieAudio *audio)
{
    if (audio)
    {
        while (audio->lanes)
        {
            while (audio->lanes->head)
                drop_head(audio->lanes);
            remove_lane(&audio->lanes);
        }
        free(audio);
    }
}

void mm_movie_audio_clear(MMMovieAudio *audio)
{
    AudioLane **slot;
    slot = &audio->lanes;
    while (*slot)
    {
        while ((*slot)->head)
            drop_head(*slot);
        if ((*slot)->muted || (*slot)->solo)
            slot = &(*slot)->next;
        else
            remove_lane(slot);
    }
}

void mm_movie_audio_collect(MMMovieAudio *audio)
{
    AudioLane **slot;
    slot = &audio->lanes;
    while (*slot)
    {
        collect_lane(*slot);
        if ((*slot)->head || (*slot)->muted || (*slot)->solo)
            slot = &(*slot)->next;
        else
            remove_lane(slot);
    }
}

void mm_movie_audio_cut(MMMovieAudio *audio)
{
    AudioLane *lane;
    MMMovieAudioVoice **next, *voice;
    for (lane = audio->lanes; lane; lane = lane->next)
    {
        lane->tail = NULL;
        next = &lane->head;
        while (*next)
        {
            voice = *next;
            if (mm_movie_audio_cut_stops(voice->loop, voice->clip.midi_events != 0))
            {
                *next = voice->next;
                mm_movie_audio_voice_destroy(voice);
            }
            else
            {
                lane->tail = voice;
                next = &voice->next;
            }
        }
    }
}

int mm_movie_audio_cut_stops(int loop, int midi)
{
    return loop && !midi;
}

void mm_movie_audio_suspend(MMMovieAudio *audio, int suspended)
{
    audio->suspended = suspended;
}

void mm_movie_audio_mute(MMMovieAudio *audio, int muted)
{
    audio->muted = muted;
}

void mm_movie_audio_stats(const MMMovieAudio *audio, MMMovieAudioStats *stats)
{
    *stats = audio->stats;
}

int mm_movie_audio_pending(const MMMovieAudio *audio)
{
    const AudioLane *lane;
    const MMMovieAudioVoice *voice;
    for (lane = audio->lanes; lane; lane = lane->next)
        for (voice = lane->head; voice; voice = voice->next)
            if (!voice->loop && unfinished(voice))
                return 1;
    return 0;
}

static int audible(const MMMovieAudio *audio, int muted, int solo)
{
    return !muted && (!audio->solos || solo);
}

int mm_movie_audio_set_mix(MMMovieAudio *audio, MMAudioCounter key, int muted, int solo)
{
    AudioLane **slot, *lane;
    slot = lane_slot(audio, key);
    lane = *slot;
    if (!lane || mm_audio_count_compare(lane->key, key))
    {
        if (!muted && !solo)
            return 1;
        lane = insert_lane(slot, key);
        if (!lane)
            return 0;
    }
    audio->mixed -= lane->muted || lane->solo;
    audio->solos -= lane->solo != 0;
    lane->muted = muted;
    lane->solo = solo;
    audio->mixed += muted || solo;
    audio->solos += solo != 0;
    if (!muted && !solo && !lane->head)
        remove_lane(slot);
    return 1;
}

void mm_movie_audio_track_mix(const MMMovieAudio *audio, MMAudioCounter key, int *muted, int *solo, int *enabled)
{
    const AudioLane *lane;
    for (lane = audio->lanes; lane && mm_audio_count_compare(lane->key, key) < 0; lane = lane->next)
    {
    }
    *muted = *solo = 0;
    if (lane && !mm_audio_count_compare(lane->key, key))
    {
        *muted = lane->muted;
        *solo = lane->solo;
    }
    *enabled = audible(audio, *muted, *solo);
}

void mm_movie_audio_reset_mix(MMMovieAudio *audio)
{
    AudioLane **slot;
    slot = &audio->lanes;
    while (*slot)
    {
        if ((*slot)->head)
        {
            (*slot)->muted = (*slot)->solo = 0;
            slot = &(*slot)->next;
        }
        else
            remove_lane(slot);
    }
    audio->mixed = audio->solos = 0;
}

int mm_movie_audio_mix_active(const MMMovieAudio *audio)
{
    return audio->mixed != 0;
}

static size_t limit_frames(size_t count, MMAudioCounter available)
{
    if (mm_audio_count_compare(available, mm_audio_count_size(count)) < 0)
        return mm_audio_count_to_size(available);
    return count;
}

static size_t render_midi(MMMovieAudioVoice *voice, MMAudioCounter position, float *scratch, size_t count)
{
    MMMovieAudioEvent event;
    MMAudioCounter at;
    void *owner;
    owner = voice->services.owner;
    while (voice->event < voice->clip.midi_events)
    {
        event = voice->services.event(owner, voice->event);
        at = mm_audio_count_multiply(event.milliseconds, 48);
        if (mm_audio_count_compare(at, position) > 0)
        {
            count = limit_frames(count, mm_audio_count_sub(at, position));
            break;
        }
        voice->services.message(owner, event.message);
        ++voice->event;
    }
    voice->services.render(owner, scratch, count);
    return count;
}

MMMovieAudioVoice *mm_movie_audio_prepare(const MMMovieAudioClip *clip, const MMMovieAudioServices *services,
                                         MMAudioCounter offset, MMI32 volume, int loop)
{
    MMMovieAudioVoice *voice;
    MMMovieAudioEvent end;
    MMAudioCounter length, at;
    float scratch[MM_MOVIE_AUDIO_BLOCK_FRAMES * 2];
    size_t count;
    if (clip->midi_events)
    {
        end = services->event(services->owner, clip->midi_events - 1);
        length = mm_audio_count_add(mm_audio_count_multiply(end.milliseconds, 48), mm_audio_counter(1, 0));
    }
    else
    {
        if (!clip->sample_frames || !clip->stereo)
            return NULL;
        length = mm_audio_count_size(clip->sample_frames);
    }
    voice = (MMMovieAudioVoice *)calloc(1, sizeof(*voice));
    if (!voice)
        return NULL;
    voice->clip = *clip;
    voice->services = *services;
    voice->frames = length;
    voice->position = offset;
    voice->volume = volume <= 0 ? 0.f : volume >= 65536 ? 1.f : volume / 65536.f;
    voice->loop = loop;
    if (loop)
        mm_audio_count_divide(offset, length, &voice->position);
    else if (mm_audio_count_compare(offset, length) > 0)
        voice->position = length;
    if (clip->midi_events)
    {
        /* Reconstruct controllers and sounding notes at the seek position. */
        at = mm_audio_counter(0, 0);
        while (mm_audio_count_compare(at, voice->position) < 0)
        {
            count = limit_frames(MM_MOVIE_AUDIO_BLOCK_FRAMES, mm_audio_count_sub(voice->position, at));
            count = render_midi(voice, at, scratch, count);
            at = mm_audio_count_add(at, mm_audio_count_size(count));
        }
    }
    return voice;
}

static int same_clip(const MMMovieAudioVoice *a, const MMMovieAudioVoice *b)
{
    return a->clip.source == b->clip.source && a->clip.type == b->clip.type && a->clip.number == b->clip.number;
}

int mm_movie_audio_submit(MMMovieAudio *audio, MMAudioCounter key, int queued, MMMovieAudioVoice *voice)
{
    AudioLane **slot, *lane;
    MMMovieAudioVoice *previous;
    slot = lane_slot(audio, key);
    lane = *slot;
    if (!lane || mm_audio_count_compare(lane->key, key))
    {
        if (!voice)
            return 1;
        lane = insert_lane(slot, key);
        if (!lane)
        {
            mm_movie_audio_voice_destroy(voice);
            return 0;
        }
    }
    collect_lane(lane);
    if (voice && voice->clip.midi_events)
        for (previous = lane->head; previous; previous = previous->next)
            if (same_clip(previous, voice) && unfinished(previous))
            {
                mm_movie_audio_voice_destroy(voice);
                return 1;
            }
    if (!queued || !voice)
        while (lane->head)
            drop_head(lane);
    if (voice)
    {
        if (lane->tail)
            lane->tail->next = voice;
        else
            lane->head = voice;
        lane->tail = voice;
        if (voice->clip.midi_events)
            ++audio->stats.midi_sounds;
        else
            ++audio->stats.wave_sounds;
    }
    return 1;
}

static void mix_lane(AudioLane *lane, int enabled, float *out, size_t frames)
{
    MMMovieAudioVoice *voice;
    size_t written, count;
    const float *in;
    float scratch[MM_MOVIE_AUDIO_BLOCK_FRAMES * 2];
    written = 0;
    for (voice = lane->head; voice && written < frames; voice = voice->next)
        while (written < frames && unfinished(voice))
        {
            count = limit_frames(frames - written, mm_audio_count_sub(voice->frames, voice->position));
            if (voice->clip.midi_events)
            {
                count = render_midi(voice, voice->position, scratch, count);
                in = scratch;
            }
            else
                in = voice->clip.stereo + mm_audio_count_to_size(voice->position) * 2;
            if (enabled)
                mm_audio_mix_stereo(out + written * 2, in, count, voice->volume);
            written += count;
            voice->position = mm_audio_count_add(voice->position, mm_audio_count_size(count));
            if (voice->loop && !unfinished(voice))
            {
                voice->position = mm_audio_counter(0, 0);
                voice->event = 0;
                if (voice->clip.midi_events)
                    voice->services.reset(voice->services.owner);
            }
        }
}

void mm_movie_audio_render(MMMovieAudio *audio, float *out, size_t frames)
{
    AudioLane *lane;
    size_t i;
    if (!out || frames > MM_MOVIE_AUDIO_BLOCK_FRAMES)
        return;
    for (i = 0; i < frames * 2; ++i)
        out[i] = 0.f;
    if (!audio->suspended)
        for (lane = audio->lanes; lane; lane = lane->next)
            mix_lane(lane, audible(audio, lane->muted, lane->solo), out, frames);
    audio->stats.rendered_frames += (unsigned long)frames;
    for (i = 0; i < frames * 2; i += 2)
        if (audio->muted)
            out[i] = out[i + 1] = 0.f;
        else if (out[i] < -.00001f || out[i] > .00001f || out[i + 1] < -.00001f || out[i + 1] > .00001f)
            ++audio->stats.audible_frames;
    mm_audio_clip_stereo(out, frames);
}
