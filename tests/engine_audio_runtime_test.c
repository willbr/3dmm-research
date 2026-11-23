#include "mm/audio_mixer.h"
#include "mm/audioman.h"
#include "mm/midi_render.h"
#include "mm/sound_queue.h"
#include "mm/sound_manager.h"
#include "mm/midi_mix.h"
#include "mm/movie_audio.h"
#include "mm/movie_audio_cache.h"
#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Fixture
{
    int fail, live, allocations, ready, messages, silences, resets, lock, releases, changed, operation, first;
    MMU32 last;
    MMI32 current;
    MMSoundQueue queue;
} Fixture;
static void *allocate(void *p, size_t n)
{
    Fixture *f = (Fixture *)p;
    void *v;
    ++f->allocations;
    if (f->fail)
        return NULL;
    v = malloc(n);
    if (v)
        ++f->live;
    return v;
}
static void release(void *p, void *v)
{
    Fixture *f = (Fixture *)p;
    --f->live;
    free(v);
}
static int ready(void *p)
{
    return ((Fixture *)p)->ready;
}
static void message(void *p, MMU32 m)
{
    Fixture *f = (Fixture *)p;
    ++f->messages;
    f->last = m;
}
static void render(void *p, float *out, size_t n)
{
    (void)p;
    while (n--)
    {
        *out++ = 0.5f;
        *out++ = -0.25f;
    }
}
static void silence(void *p)
{
    ++((Fixture *)p)->silences;
}
static void reset(void *p)
{
    ++((Fixture *)p)->resets;
}
static void arithmetic(void)
{
    MMAudioCounter a, b, q, r;
    a = mm_audio_count_multiply(0xffffffffUL, 0xffffffffUL);
    assert(a.high == 0xfffffffeUL && a.low == 1);
    q = mm_audio_count_divide(a, mm_audio_counter(0xffffffffUL, 0), &r);
    assert(q.low == 0xffffffffUL && q.high == 0 && !r.low && !r.high);
    a = mm_audio_counter(0xffffffffUL, 0xffffffffUL);
    b = mm_audio_counter(1, 0x80000000UL);
    q = mm_audio_count_divide(a, b, &r);
    assert(q.low == 1 && q.high == 0 && r.high == 0x7fffffffUL && r.low == 0xfffffffeUL);
    a = mm_audio_count_add(a, mm_audio_counter(1, 0));
    assert(!a.low && !a.high);
    a = mm_audio_count_sub(a, mm_audio_counter(1, 0));
    assert(a.low == 0xffffffffUL && a.high == 0xffffffffUL);
    assert(mm_audio_count_to_size(mm_audio_count_size((size_t)-1)) == (size_t)-1);
    assert(mm_sound_volume_scale(0xffffffffUL, 65536) == 0xffffffffUL);
    assert(mm_sound_volume_scale(0xffffffffUL, 131072) == 0xffffffffUL);
    assert(mm_sound_volume_scale(0xffffffffUL, 32768) == 0x7fff7fffUL);
}
typedef struct MixerSample
{
    int refs, retains, releases;
} MixerSample;
static void mixer_sample_retain(const void *p)
{
    MixerSample *sample = (MixerSample *)p;
    assert(sample->refs > 0);
    ++sample->refs;
    ++sample->retains;
}
static void mixer_sample_release(const void *p)
{
    MixerSample *sample = (MixerSample *)p;
    assert(sample->refs > 0);
    --sample->refs;
    ++sample->releases;
}
static void mixer(void)
{
    Fixture f;
    MMAudioAllocator a;
    MixerSample sample = {1, 0, 0};
    const MMAudioSampleServices refs = {mixer_sample_retain, mixer_sample_release};
    MMAudioMixer m;
    MMAudioCounter id, other;
    float samples[] = {0.25f, -0.5f, 0.75f, -0.25f}, out[10];
    MMAudioVoice v[2];
    int allocations;
    memset(&f, 0, sizeof(f));
    a.context = &f;
    a.allocate = allocate;
    a.release = release;
    mm_audio_mixer_init_samples(&m, &a, &refs);
    f.fail = 1;
    id = mm_audio_mixer_add(&m);
    assert(!id.low && !id.high);
    f.fail = 0;
    id = mm_audio_mixer_add(&m);
    assert(id.low == 2);
    memset(v, 0, sizeof(v));
    v[0].id = 1;
    v[0].plays = 2;
    v[0].sample = &sample;
    v[0].stereo = samples;
    v[0].frames = 2;
    v[0].volume = 1;
    assert(mm_audio_mixer_sync(&m, id, v, 1));
    mixer_sample_release(&sample); /* The published voice is the remaining owner. */
    assert(sample.refs == 1);
    allocations = f.allocations;
    mm_audio_mixer_render(&m, out, 3);
    assert(out[0] == .25f && out[2] == .75f && out[4] == .25f && f.allocations == allocations);
    f.fail = 1;
    assert(!mm_audio_mixer_sync(&m, id, v, 1));
    assert(sample.refs == 1 && sample.retains == 1 && sample.releases == 1);
    f.fail = 0;
    mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == .75f && mm_audio_mixer_done(&m, id, 1));
    assert(mm_audio_mixer_sync(&m, id, v, 1));
    mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == 0);
    v[1] = v[0];
    assert(!mm_audio_mixer_sync(&m, id, v, 2));
    assert(sample.refs == 1 && sample.retains == 2 && sample.releases == 2);
    v[0].id = 2;
    v[0].offset_ms = 0xffffffffUL;
    assert(mm_audio_mixer_sync(&m, id, v, 1));
    mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == 0 && mm_audio_mixer_done(&m, id, 2));
    v[0].id = 3;
    v[0].offset_ms = 0;
    v[0].plays = 0;
    mm_audio_mixer_begin(&m);
    mm_audio_mixer_begin(&m);
    assert(mm_audio_mixer_sync(&m, id, v, 1));
    mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == 0);
    mm_audio_mixer_end(&m);
    mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == 0);
    mm_audio_mixer_end(&m);
    mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == .25f);
    v[0].volume = -1;
    assert(!mm_audio_mixer_sync(&m, id, v, 1));
    v[0].volume = 1;
    mm_audio_mixer_volume(&m, -1);
    assert(m.volume == 1);
    mm_audio_mixer_suspend(&m, 1);
    mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == 0);
    mm_audio_mixer_suspend(&m, 0);
    mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == .75f);
    m.next = mm_audio_counter(0xffffffffUL, 0xffffffffUL);
    other = mm_audio_mixer_add(&m);
    assert(other.high == 0xffffffffUL);
    v[1] = v[0];
    v[1].id = 4;
    assert(mm_audio_mixer_sync(&m, other, v, 2) && sample.refs == 3);
    mm_audio_mixer_remove(&m, other);
    assert(sample.refs == 1);
    other = mm_audio_mixer_add(&m);
    assert(!other.low && !other.high);
    mm_audio_mixer_dispose(&m);
    assert(f.live == 0 && sample.refs == 0 && sample.retains + 1 == sample.releases);
}
static void movie_release(void *p)
{
    ++((Fixture *)p)->releases;
}

static MMMovieAudioEvent movie_event(void *p, size_t index)
{
    static const MMMovieAudioEvent events[] = {{0, 0x00643c90UL}, {1, 0x00003c80UL}};
    (void)p;
    return events[index];
}

static void movie_render(void *p, float *out, size_t frames)
{
    ((Fixture *)p)->current += (MMI32)frames;
    render(p, out, frames);
}

static void movie_mixer(void)
{
    static const float samples[] = {.75f, -.75f, .5f, -.5f};
    const MMMovieAudioClip wave = {0, 1, 3, samples, 2, 0};
    const MMMovieAudioClip midi_clip = {0, 2, 3, NULL, 0, 2};
    const MMAudioCounter zero = {0, 0}, wide = {1, 1}, seek = {48, 0};
    MMMovieAudioServices services;
    MMMovieAudioVoice *voice;
    MMMovieAudioStats stats;
    MMMovieAudio *audio;
    MMAudioCounter scene, actor, music;
    Fixture pcm, midi, duplicate;
    float out[4];
    int muted, solo, audible;
    memset(&pcm, 0, sizeof(pcm));
    memset(&midi, 0, sizeof(midi));
    memset(&duplicate, 0, sizeof(duplicate));
    memset(&services, 0, sizeof(services));
    services.owner = &pcm;
    services.release = movie_release;
    audio = mm_movie_audio_create();
    assert(audio);
    scene = mm_movie_audio_lane(0, 0, 3);
    actor = mm_movie_audio_lane(1, 0, 3);
    music = mm_movie_audio_lane(0, 0, 4);
    assert(mm_audio_count_compare(actor, scene) > 0);
    voice = mm_movie_audio_prepare(&wave, &services, zero, 65536, 0);
    assert(voice && mm_movie_audio_submit(audio, scene, 0, voice));
    voice = mm_movie_audio_prepare(&wave, &services, wide, 32768, 1);
    assert(voice && mm_movie_audio_submit(audio, scene, 1, voice));
    voice = mm_movie_audio_prepare(&wave, &services, zero, 65536, 0);
    assert(voice && mm_movie_audio_submit(audio, actor, 0, voice));
    assert(mm_movie_audio_pending(audio));
    mm_movie_audio_render(audio, out, 1);
    assert(out[0] == 0 && out[1] == 0);
    mm_movie_audio_suspend(audio, 0);
    mm_movie_audio_render(audio, out, 1);
    assert(out[0] == 1 && out[1] == -1);
    assert(mm_movie_audio_set_mix(audio, scene, 0, 1));
    mm_movie_audio_track_mix(audio, actor, &muted, &solo, &audible);
    assert(!muted && !solo && !audible);
    mm_movie_audio_render(audio, out, 2);
    assert(out[0] == .5f && out[1] == -.5f && out[2] == .25f && out[3] == -.25f);
    assert(!mm_movie_audio_pending(audio));
    mm_movie_audio_mute(audio, 1);
    mm_movie_audio_render(audio, out, 1);
    assert(out[0] == 0 && out[1] == 0);
    mm_movie_audio_cut(audio);
    mm_movie_audio_collect(audio);
    assert(pcm.releases == 3 && mm_movie_audio_mix_active(audio));
    mm_movie_audio_stats(audio, &stats);
    assert(stats.wave_sounds == 3 && stats.rendered_frames == 5 && stats.audible_frames == 3);
    mm_movie_audio_reset_mix(audio);
    mm_movie_audio_mute(audio, 0);
    assert(!mm_movie_audio_mix_active(audio));

    services.owner = &midi;
    services.event = movie_event;
    services.message = message;
    services.render = movie_render;
    services.reset = reset;
    voice = mm_movie_audio_prepare(&midi_clip, &services, seek, 65536, 1);
    assert(voice && midi.messages == 1 && midi.current == 48);
    assert(mm_movie_audio_submit(audio, music, 0, voice));
    services.owner = &duplicate;
    voice = mm_movie_audio_prepare(&midi_clip, &services, zero, 65536, 1);
    assert(voice && mm_movie_audio_submit(audio, music, 0, voice));
    assert(duplicate.releases == 1 && midi.releases == 0);
    mm_movie_audio_cut(audio);
    mm_movie_audio_render(audio, out, 2);
    assert(out[0] == .5f && out[2] == .5f && midi.messages == 3 && midi.resets == 1 && midi.current == 50);
    assert(!mm_movie_audio_pending(audio) && midi.releases == 0);
    assert(mm_movie_audio_set_mix(audio, music, 1, 1));
    mm_movie_audio_clear(audio);
    mm_movie_audio_track_mix(audio, music, &muted, &solo, &audible);
    assert(midi.releases == 1 && muted && solo && !audible);
    mm_movie_audio_stats(audio, &stats);
    assert(stats.midi_sounds == 1 && stats.rendered_frames == 7 && stats.audible_frames == 5);
    mm_movie_audio_destroy(audio);
}

typedef struct CachedAudioFixture
{
    Fixture media, synths[2];
    size_t frames;
    int reads, midi, reject_media, reject_synth, synth_count;
} CachedAudioFixture;

static int read_cached_audio(void *p, MMMovieAudioMedia *out)
{
    static const float samples[] = {.5f, -.5f, .25f, -.25f};
    CachedAudioFixture *f = p;
    ++f->reads;
    if (f->reject_media)
        return 0;
    out->clip.stereo = samples;
    out->clip.sample_frames = f->midi ? 0 : f->frames;
    out->clip.midi_events = f->midi ? 2 : 0;
    out->owner = &f->media;
    out->release = movie_release;
    out->event = movie_event;
    return 1;
}

static int create_cached_synth(void *p, MMMovieAudioSynth *out)
{
    CachedAudioFixture *f = p;
    if (f->reject_synth || f->synth_count == 2)
        return 0;
    out->owner = f->synths + f->synth_count++;
    out->release = movie_release;
    out->message = message;
    out->render = movie_render;
    out->reset = reset;
    return 1;
}

static void movie_audio_cache(void)
{
    const MMAudioCounter zero = {0, 0}, seek = {48, 0};
    CachedAudioFixture f;
    MMMovieAudioDecode decode = {NULL, read_cached_audio, create_cached_synth};
    MMMovieAudioKey key = {0, 1, 9, 3, 4};
    MMMovieAudioVoice *playing, *voice, *other;
    MMMovieAudioCache *cache = mm_movie_audio_cache_create(4 * sizeof(float));
    MMMovieAudio *audio = mm_movie_audio_create();
    float output[4];
    assert(cache && audio);
    memset(&f, 0, sizeof(f));
    f.frames = 2;
    decode.context = &f;
    assert(mm_movie_audio_cache_voice(cache, NULL, &decode, zero, 65536, 0, &voice) == MM_MOVIE_AUDIO_CACHE_OK);
    assert(!voice && !f.reads);
    assert(mm_movie_audio_cache_voice(cache, &key, &decode, zero, 65536, 0, &playing) == MM_MOVIE_AUDIO_CACHE_OK);
    assert(playing && f.reads == 1 && mm_movie_audio_submit(audio, zero, 0, playing));
    assert(mm_movie_audio_cache_voice(cache, &key, &decode, zero, 65536, 0, &voice) == MM_MOVIE_AUDIO_CACHE_OK);
    assert(voice && f.reads == 1);
    ++key.scene;
    other = voice;
    f.reject_media = 1;
    assert(mm_movie_audio_cache_voice(cache, &key, &decode, zero, 65536, 0, &other) == MM_MOVIE_AUDIO_CACHE_SOURCE);
    assert(other == voice && f.reads == 2);
    mm_movie_audio_voice_destroy(voice);
    f.reject_media = 0;
    f.frames = 3;
    assert(mm_movie_audio_cache_voice(cache, &key, &decode, zero, 65536, 0, &voice) == MM_MOVIE_AUDIO_CACHE_LIMIT);
    assert(f.media.releases == 1);
    --key.scene;
    f.frames = 2;
    assert(mm_movie_audio_cache_voice(cache, &key, &decode, zero, 65536, 0, &voice) == MM_MOVIE_AUDIO_CACHE_OK);
    assert(f.reads == 3);
    mm_movie_audio_voice_destroy(voice);
    ++key.scene;
    assert(mm_movie_audio_cache_voice(cache, &key, &decode, zero, 65536, 0, &voice) == MM_MOVIE_AUDIO_CACHE_OK);
    assert(f.reads == 4 && f.media.releases == 1);
    mm_movie_audio_voice_destroy(voice);
    mm_movie_audio_suspend(audio, 0);
    mm_movie_audio_render(audio, output, 2);
    assert(output[0] == .5f && output[3] == -.25f);
    mm_movie_audio_collect(audio);
    assert(f.media.releases == 2);
    mm_movie_audio_destroy(audio);

    key.source = 7;
    f.midi = f.reject_synth = 1;
    assert(mm_movie_audio_cache_voice(cache, &key, &decode, seek, 65536, 0, &voice) == MM_MOVIE_AUDIO_CACHE_SOURCE);
    assert(f.reads == 5 && f.media.releases == 3);
    f.reject_synth = 0;
    assert(mm_movie_audio_cache_voice(cache, &key, &decode, seek, 65536, 0, &voice) == MM_MOVIE_AUDIO_CACHE_OK);
    ++key.scene;
    assert(mm_movie_audio_cache_voice(cache, &key, &decode, zero, 65536, 0, &other) == MM_MOVIE_AUDIO_CACHE_OK);
    assert(f.reads == 5 && f.synth_count == 2 && f.synths[0].current == 48 && !f.synths[1].current);
    mm_movie_audio_cache_destroy(cache);
    assert(f.media.releases == 3);
    mm_movie_audio_voice_destroy(voice);
    assert(f.media.releases == 3 && f.synths[0].releases == 1 && !f.synths[1].releases);
    mm_movie_audio_voice_destroy(other);
    assert(f.media.releases == 4 && f.synths[1].releases == 1);
}

static void midi(void)
{
    Fixture f;
    MMAudioAllocator a;
    MMMidiRenderServices s;
    MMMidiRenderer r;
    MMMidiStreamEvent events[] = {{1, 0, 0x00643c90UL}, {1, 0, 0x00003c80UL}};
    float out[192];
    size_t tag;
    void *data;
    int before;
    memset(&f, 0, sizeof(f));
    memset(&s, 0, sizeof(s));
    a.context = &f;
    a.allocate = allocate;
    a.release = release;
    s.context = &f;
    s.ready = ready;
    s.message = message;
    s.render = render;
    s.silence = silence;
    s.reset = reset;
    mm_midi_render_init(&r, &s, &a);
    assert(!mm_midi_render_queue(&r, events, sizeof(events), 0, 1, 42) && f.live == 0);
    f.ready = 1;
    f.fail = 1;
    assert(!mm_midi_render_queue(&r, events, sizeof(events), 0, 1, 42));
    f.fail = 0;
    assert(!mm_midi_render_queue(&r, events, sizeof(events), 1, 1, 42));
    events[0].streamId = 1;
    assert(!mm_midi_render_queue(&r, events, sizeof(events), 0, 1, 42));
    events[0].streamId = 0;
    assert(mm_midi_render_queue(&r, events, sizeof(events), 0, 2, 42));
    assert(!mm_midi_render_reset(&r));
    before = f.allocations;
    assert(!mm_midi_render(&r, out, 48) && f.messages == 1 && f.last == 0x00643c90UL);
    assert(!mm_midi_render(&r, out, 48) && f.messages == 2);
    assert(!mm_midi_render(&r, out, 48) && f.messages == 3);
    assert(mm_midi_render(&r, out, 48) && f.messages == 4 && f.allocations == before);
    assert(r.notes.low == 2 && r.frames.low == 192 && r.completed.low == 1);
    assert(mm_midi_render_pop(&r, &data, &tag) && data == events && tag == 42 && f.live == 0);
    assert(mm_midi_render_reset(&r) && f.resets == 1);
    events[0].deltaTicks = events[1].deltaTicks = 0;
    assert(!mm_midi_render_queue(&r, events, sizeof(events), 0, 0, 0));
    assert(mm_midi_render_queue(&r, events, sizeof(events), 0, 1, 7));
    assert(mm_midi_render(&r, NULL, 0));
    assert(mm_midi_render_pop(&r, &data, &tag) && tag == 7);
    events[0].deltaTicks = 0xffffffffUL;
    events[1].deltaTicks = 1;
    assert(mm_midi_render_queue(&r, events, sizeof(events), 0, 1, 9));
    assert(!mm_midi_render(&r, out, 1) && r.wait_frames.high > 0);
    mm_midi_render_stop(&r);
    assert(f.silences == 1 && !r.head);
    mm_midi_render_dispose(&r);
    assert(!f.live);
}
static void enter(void *p)
{
    ++((Fixture *)p)->lock;
}
static void leave(void *p)
{
    assert(((Fixture *)p)->lock > 0);
    --((Fixture *)p)->lock;
}
static void *test_storage_resize(void *p, void *bytes, MMU32 old_size, MMU32 size, int priority)
{
    (void)old_size;
    (void)priority;
    return *(int *)p ? NULL : realloc(bytes, size);
}
static void test_storage_free(void *p, void *bytes, MMU32 size)
{
    (void)p;
    (void)size;
    free(bytes);
}
static void init_queue(Fixture *f)
{
    MMStorageAllocator allocator = {NULL, test_storage_resize, test_storage_free};
    allocator.context = &f->fail;
    assert(mm_sound_queue_init(&f->queue, &allocator, 0, 1));
}
static void object_release(void *p, void *v)
{
    (void)v;
    ++((Fixture *)p)->releases;
}
static void changed(void *p, int op, MMI32 first)
{
    Fixture *f = (Fixture *)p;
    ++f->changed;
    f->operation = op;
    f->first = first;
}
static void flush(void *p);
static MMSoundQueueServices queue_services(Fixture *f)
{
    MMSoundQueueServices s;
    memset(&s, 0, sizeof(s));
    s.context = f;
    s.queue = &f->queue;
    s.enter = enter;
    s.leave = leave;
    s.flush = flush;
    s.release = object_release;
    s.changed = changed;
    return s;
}
static void flush(void *p)
{
    MMSoundQueueServices s = queue_services((Fixture *)p);
    mm_sound_queue_flush(&s);
}
static void queue(void)
{
    Fixture f;
    MMSoundQueueServices s;
    MMSoundEntry e;
    memset(&f, 0, sizeof(f));
    memset(&e, 0, sizeof(e));
    init_queue(&f);
    s = queue_services(&f);
    e.object = &f;
    e.id = 1;
    e.sound_class = 2;
    e.priority = 99;
    mm_sound_queue_enqueue(&s, &e);
    e.id = 2;
    mm_sound_queue_enqueue(&s, &e);
    assert(mm_sound_queue_count(&f.queue) == 2 && !f.lock && f.changed == 2 && f.first == 1);
    assert(mm_sound_queue_priority(&s) == (-2147483647 - 1));
    mm_sound_queue_edit(&s, MM_SOUND_PAUSE, 1, 2, -1);
    assert(f.first == 0 && mm_sound_queue_get(&f.queue, 1).pause == 1);
    mm_sound_queue_edit(&s, MM_SOUND_PAUSE, 0, 1, -1);
    assert(mm_sound_queue_get(&f.queue, 0).pause == 2);
    mm_sound_queue_edit(&s, MM_SOUND_RESUME, 1, -1, -1);
    assert(mm_sound_queue_get(&f.queue, 0).pause == 1 && !mm_sound_queue_get(&f.queue, 1).pause);
    assert(mm_sound_queue_playing(&s, 0, 1, -1));
    mm_sound_queue_edit(&s, MM_SOUND_STOP, 0, 2, -1);
    assert(!mm_sound_queue_playing(&s, 0, 2, -1));
    mm_sound_queue_edit(&s, MM_SOUND_RESUME, 0, 2, -1);
    assert(mm_sound_queue_get(&f.queue, 1).pause == -1);
    f.queue.current = 1;
    mm_sound_queue_flush(&s);
    assert(mm_sound_queue_count(&f.queue) == 1 && !f.queue.current && f.releases == 1);
    assert(mm_sound_queue_get(&f.queue, 0).id == 2);
    e.id = 3;
    mm_sound_queue_enqueue(&s, &e); /* Reuse the second slot without growing. */
    f.fail = 1;
    e.id = 4;
    mm_sound_queue_enqueue(&s, &e);
    assert(f.releases == 2 && mm_sound_queue_count(&f.queue) == 2);
    assert(mm_sound_queue_get(&f.queue, 1).id == 3 && f.first == 1);
    mm_sound_queue_dispose(&s);
    assert(f.releases == 4 && !f.lock && mm_sound_queue_count(&f.queue) == -1);
}

typedef struct AudioManFixture
{
    Fixture host;
    char trace[16];
    size_t steps;
    int looping, original, filtered, streamed, filter_id, stream_id;
    int fail_group, fail_enlist, fail_loop;
    MMU32 now, start, volume, offset;
} AudioManFixture;

static void audio_trace(AudioManFixture *f, char code)
{
    assert(f->steps < sizeof(f->trace));
    f->trace[f->steps++] = code;
}

static void audio_expect(AudioManFixture *f, const char *expected)
{
    assert(f->steps == strlen(expected) && !memcmp(f->trace, expected, f->steps) && !f->host.lock);
    f->steps = 0;
}

static int group_allocate(void *p, MMU32 *id)
{
    AudioManFixture *f = p;
    audio_trace(f, 'A');
    *id = 42;
    return !f->fail_group;
}
static void group_start(void *p, MMU32 id) { assert(id == 42); audio_trace(p, 'B'); }
static void group_release(void *p, MMU32 id) { assert(id == 42); audio_trace(p, 'F'); }

static void *audio_loop(void *p, void *object, MMI32 plays)
{
    AudioManFixture *f = p;
    assert(f->host.lock && (object == &f->original || object == &f->streamed));
    f->looping = plays;
    audio_trace(f, 'W');
    return f->fail_loop ? NULL : &f->filtered;
}
static void *audio_identity(void *p, void *object)
{
    AudioManFixture *f = p;
    assert(f->host.lock && (object == &f->filtered || object == &f->streamed));
    return object == &f->filtered ? &f->filter_id : &f->stream_id;
}
static int audio_in_memory(void *p, void *object) { return object == &((AudioManFixture *)p)->filtered; }
static void audio_stop(void *p) { assert(((AudioManFixture *)p)->host.lock); audio_trace(p, 'X'); }
static void audio_play(void *p) { assert(((AudioManFixture *)p)->host.lock); audio_trace(p, 'P'); }
static void audio_volume(void *p, MMU32 volume)
{
    AudioManFixture *f = p;
    f->volume = volume;
    audio_trace(f, 'V');
}
static void audio_seek(void *p, MMU32 offset)
{
    AudioManFixture *f = p;
    f->offset = offset;
    audio_trace(f, 'K');
}
static void audio_source(void *p, void *object)
{
    AudioManFixture *f = p;
    assert(!object || object == &f->filtered || object == &f->streamed);
    audio_trace(f, object ? 'S' : 'D');
}
static void audio_cached_source(void *p, void *object)
{
    AudioManFixture *f = p;
    assert(object == &f->streamed);
    audio_trace(f, 'C');
}
static int audio_enlist(void *p, MMU32 id)
{
    AudioManFixture *f = p;
    assert(id == 42);
    audio_trace(f, 'G');
    return !f->fail_enlist;
}
static MMU32 audio_time(void *p)
{
    audio_trace(p, 'T');
    return ((AudioManFixture *)p)->now;
}

static void audioman_queue(void)
{
    AudioManFixture f;
    MMSoundEntry entries[3], entry;
    MMSoundQueue *queue;
    MMAudioManGroup group = {0, 0, 0};
    MMAudioManGroupServices grouping;
    MMAudioManQueueServices s;
    memset(&f, 0, sizeof(f));
    memset(&s, 0, sizeof(s));
    memset(entries, 0, sizeof(entries));
    grouping.context = &f;
    grouping.allocate = group_allocate;
    grouping.start = group_start;
    grouping.release = group_release;
    s.entries = queue_services(&f.host);
    queue = s.entries.queue;
    s.context = &f;
    s.loop = audio_loop; s.identity = audio_identity; s.in_memory = audio_in_memory;
    s.stop = audio_stop; s.play = audio_play; s.volume = audio_volume; s.seek = audio_seek;
    s.source = audio_source; s.cached_source = audio_cached_source; s.enlist = audio_enlist; s.clock = audio_time;

    mm_audioman_queue_run(&f.start, 0, &group, &s);
    audio_expect(&f, "");
    mm_audioman_group_begin(&group, &grouping);
    mm_audioman_group_begin(&group, &grouping);
    assert(group.active && group.depth == 2);
    mm_audioman_group_end(&group, &grouping);
    audio_expect(&f, "A");

    init_queue(&f.host);
    entries[0].plays = 1;
    entries[0].pause = -1;
    entries[1].object = &f.original;
    entries[1].plays = 3;
    entries[1].volume = 32768;
    entries[1].offset = 11;
    entries[2].object = &f.streamed;
    entries[2].plays = 1;
    assert(mm_array_insert(&queue->rows, 0, entries, 3) == MM_STORAGE_OK);
    f.now = 5;
    mm_audioman_queue_run(&f.start, 0, &group, &s);
    entry = mm_sound_queue_get(queue, 1);
    assert(queue->current == 1 && f.host.releases == 1 && f.looping == 3 &&
           entry.plays == 1 && entry.object == &f.filtered &&
           f.offset == 11 && f.volume == 0x7fff7fffUL && f.start == 0xfffffffaUL);
    audio_expect(&f, "WXVSKGT");

    mm_audioman_queue_notify(&f.start, &f.stream_id, &group, &s);
    audio_expect(&f, "");
    entry.pause = 1;
    mm_sound_queue_put(queue, 1, &entry);
    f.now = 8;
    mm_audioman_queue_pause(&f.start, 1, &group, &s);
    entry = mm_sound_queue_get(queue, 1);
    assert(entry.offset == 14 && queue->current == 1);
    audio_expect(&f, "TXD");
    entry.pause = 0;
    mm_sound_queue_put(queue, 1, &entry);
    f.fail_enlist = 1;
    mm_audioman_queue_run(&f.start, 1, &group, &s);
    audio_expect(&f, "XVSKGPT");
    mm_audioman_group_end(&group, &grouping);
    audio_expect(&f, "BF");

    f.fail_loop = 1;
    entry = mm_sound_queue_get(queue, 2);
    entry.plays = 2;
    mm_sound_queue_put(queue, 2, &entry);
    mm_audioman_queue_notify(&f.start, &f.filter_id, &group, &s);
    assert(queue->current == 2 && mm_sound_queue_get(queue, 2).plays == 2 && f.looping == 2);
    audio_expect(&f, "WXVCPT");
    ++f.now;
    mm_audioman_queue_notify(&f.start, &f.stream_id, &group, &s);
    assert(mm_sound_queue_get(queue, 2).plays == 1 && f.start == f.now);
    audio_expect(&f, "ST");
    mm_audioman_queue_notify(&f.start, &f.stream_id, &group, &s);
    assert(queue->current == 3);
    audio_expect(&f, "XD");
    f.fail_group = 1;
    mm_audioman_group_begin(&group, &grouping);
    assert(!group.active);
    mm_audioman_group_end(&group, &grouping);
    audio_expect(&f, "A");
    mm_sound_queue_dispose(&s.entries);
}

typedef struct MidiQueueFixture
{
    Fixture host;
    MMU32 now, start;
    MMI32 active;
    int stops;
} MidiQueueFixture;
static MMU32 queue_midi_clock(void *p)
{
    return ((MidiQueueFixture *)p)->now;
}
static MMU32 queue_midi_start(void *p)
{
    return ((MidiQueueFixture *)p)->start;
}
static void queue_midi_set_start(void *p, MMU32 start)
{
    ((MidiQueueFixture *)p)->start = start;
}
static int queue_midi_play(void *p, const MMSoundEntry *e)
{
    MidiQueueFixture *f = (MidiQueueFixture *)p;
    assert(f->host.lock > 0);
    if (!e)
    {
        f->active = 0;
        ++f->stops;
    }
    else
        f->active = e->id;
    return 1;
}
static void midi_queue(void)
{
    MidiQueueFixture f;
    MMMidiQueueServices s;
    MMSoundEntry entries[3], entry;
    MMSoundQueue *queue = &f.host.queue;
    memset(&f, 0, sizeof(f));
    memset(entries, 0, sizeof(entries));
    init_queue(&f.host);
    s.entries = queue_services(&f.host);
    s.context = &f;
    s.clock = queue_midi_clock;
    s.start = queue_midi_start;
    s.set_start = queue_midi_set_start;
    s.play = queue_midi_play;
    entries[0].pause = -1;
    entries[1].id = 1;
    entries[1].object = &entries[1];
    entries[1].offset = 11;
    entries[2].id = 2;
    entries[2].object = &entries[2];
    assert(mm_array_insert(&queue->rows, 0, entries, 3) == MM_STORAGE_OK);
    f.now = 5;
    mm_midi_queue_run(&s, 0);
    assert(f.active == 1 && queue->current == 1 && f.start == 0xfffffffaUL && !f.host.lock);
    mm_midi_queue_notify(&s, &entries[2]);
    assert(f.active == 1 && queue->current == 1);
    entry = mm_sound_queue_get(queue, 1);
    entry.pause = 1;
    mm_sound_queue_put(queue, 1, &entry);
    f.now = 8;
    mm_midi_queue_pause(&s, 1);
    entry = mm_sound_queue_get(queue, 1);
    assert(entry.offset == 14 && f.stops == 1 && queue->current == 1 && !f.host.lock);
    entry.pause = 0;
    mm_sound_queue_put(queue, 1, &entry);
    mm_midi_queue_run(&s, 1);
    assert(f.active == 1);
    mm_midi_queue_notify(&s, &entries[1]);
    assert(f.active == 2 && queue->current == 2);
    mm_midi_queue_notify(&s, &entries[2]);
    assert(!f.active && f.stops == 2 && queue->current == 3 && !f.host.lock);
    mm_sound_queue_dispose(&s.entries);
}
typedef struct AudioManDeviceFixture
{
    char trace[96];
    size_t length;
    MMU32 refs, supported, system, volume, published;
    MMI32 probed, selected;
    int fail_create, fail_init, fail_activate, fail_suspend, fail_get;
} AudioManDeviceFixture;

static void device_record(AudioManDeviceFixture *f, char event)
{
    assert(f->length + 1 < sizeof(f->trace));
    f->trace[f->length++] = event;
    f->trace[f->length] = '\0';
}

static void device_expect(AudioManDeviceFixture *f, const char *trace)
{
    assert(strcmp(f->trace, trace) == 0);
    f->length = 0;
    f->trace[0] = '\0';
}

static void *device_create(void *p)
{
    AudioManDeviceFixture *f = (AudioManDeviceFixture *)p;
    assert(!f->refs);
    device_record(f, 'C');
    if (f->fail_create)
        return NULL;
    f->refs = 1;
    return f;
}

static void device_retain(void *p, void *mixer)
{
    AudioManDeviceFixture *f = (AudioManDeviceFixture *)p;
    assert(mixer == f && f->refs);
    device_record(f, '+');
    ++f->refs;
}

static MMU32 device_release(void *p, void *mixer)
{
    AudioManDeviceFixture *f = (AudioManDeviceFixture *)p;
    assert(mixer == f && f->refs);
    device_record(f, '-');
    return --f->refs;
}

static int device_supports(void *p, MMI32 format)
{
    AudioManDeviceFixture *f = (AudioManDeviceFixture *)p;
    assert(format >= 0 && format < MM_AUDIOMAN_FORMAT_COUNT);
    device_record(f, '?');
    f->probed = format;
    return (f->supported & (1UL << format)) != 0;
}

static int device_initialize(void *p, void *mixer, MMI32 format, MMU32 voices, int remix, MMU32 ms)
{
    AudioManDeviceFixture *f = (AudioManDeviceFixture *)p;
    assert(mixer == f && f->refs && voices == 12 && remix && ms == 600);
    device_record(f, 'I');
    f->selected = format;
    return !f->fail_init;
}

static int device_activate(void *p, void *mixer)
{
    AudioManDeviceFixture *f = (AudioManDeviceFixture *)p;
    assert(mixer == f && f->refs);
    device_record(f, 'A');
    return !f->fail_activate;
}

static int device_suspend(void *p, void *mixer, int suspend)
{
    AudioManDeviceFixture *f = (AudioManDeviceFixture *)p;
    assert(mixer == f && f->refs);
    device_record(f, suspend ? 'S' : 'R');
    return !f->fail_suspend;
}

static void device_get_volume(void *p, void *mixer, MMU32 *volume)
{
    AudioManDeviceFixture *f = (AudioManDeviceFixture *)p;
    assert(mixer == f && f->refs);
    device_record(f, 'G');
    if (!f->fail_get)
        *volume = f->system;
}

static void device_set_volume(void *p, void *mixer, MMU32 volume)
{
    AudioManDeviceFixture *f = (AudioManDeviceFixture *)p;
    assert(mixer == f && f->refs);
    device_record(f, 'V');
    f->volume = volume;
}

static void device_publish_volume(void *p, MMU32 volume)
{
    AudioManDeviceFixture *f = (AudioManDeviceFixture *)p;
    device_record(f, 'P');
    f->published = volume;
}

static void device_busy(void *p)
{
    device_record((AudioManDeviceFixture *)p, 'B');
}

static void audioman_device(void)
{
    AudioManDeviceFixture f;
    MMAudioManDeviceServices s = {NULL, device_create, device_retain, device_release, device_supports,
                                  device_initialize, device_activate, device_suspend, device_get_volume,
                                  device_set_volume, device_publish_volume, device_busy};
    MMAudioManMixer mixer = {0, 0, 0};
    MMAudioManDevice first, second;
    MMSoundControl control = {0, 1};
    MMI32 format;
    memset(&f, 0, sizeof(f));
    s.context = &f;
    mm_audioman_device_init(&first);
    mm_audioman_device_init(&second);
    assert(!first.attached && first.volume == 65536 && first.system_volume == 0xffffffffUL);
    for (format = 0; format < MM_AUDIOMAN_FORMAT_COUNT; ++format)
    {
        f.supported = 1UL << format;
        assert(mm_audioman_device_open(&first, &mixer, format, &s));
        assert(f.probed == format && f.selected == format && mixer.format == format && mixer.cache_ms == 1200);
        mm_audioman_device_close(&first, &mixer, &s);
        assert(!first.attached && !mixer.object && !f.refs);
        device_expect(&f, "C?IA-");
    }
    for (format = MM_AUDIOMAN_11M16; format < MM_AUDIOMAN_FORMAT_COUNT; ++format)
    {
        f.supported = 0;
        assert(mm_audioman_device_open(&first, &mixer, format, &s));
        assert(f.probed == format && f.selected == format - 6 && mixer.format == f.selected);
        mm_audioman_device_close(&first, &mixer, &s);
        device_expect(&f, "C?IA-");
    }
    assert(!mm_audioman_device_open(&first, &mixer, MM_AUDIOMAN_44S8, &s));
    assert(first.attached);
    mm_audioman_device_close(&first, &mixer, &s);
    device_expect(&f, "C?-");
    f.fail_create = 1;
    assert(!mm_audioman_device_open(&first, &mixer, -1, &s));
    mm_audioman_device_close(&first, &mixer, &s);
    device_expect(&f, "C");
    f.fail_create = 0;
    f.supported = 1UL << MM_AUDIOMAN_22M16;
    f.fail_init = 1;
    assert(!mm_audioman_device_open(&first, &mixer, -1, &s));
    assert(f.probed == MM_AUDIOMAN_22M16 && f.selected == f.probed);
    mm_audioman_device_close(&first, &mixer, &s);
    device_expect(&f, "C?I-");
    f.fail_init = 0;
    f.fail_activate = 1;
    assert(!mm_audioman_device_open(&first, &mixer, MM_AUDIOMAN_FORMAT_COUNT, &s));
    assert(f.probed == MM_AUDIOMAN_22M16 && f.selected == f.probed);
    mm_audioman_device_close(&first, &mixer, &s);
    device_expect(&f, "C?IA-");
    f.fail_activate = 0;
    assert(mm_audioman_device_open(&first, &mixer, MM_AUDIOMAN_22M16, &s));
    assert(mm_audioman_device_open(&second, &mixer, MM_AUDIOMAN_11S8, &s));
    assert(f.refs == 2 && mixer.format == MM_AUDIOMAN_22M16);
    mm_audioman_device_close(&second, &mixer, &s);
    mm_audioman_device_close(&second, &mixer, &s);
    assert(mixer.object == &f && f.refs == 1);
    device_expect(&f, "C?IA+-");

    f.system = 0x80004000UL;
    mm_audioman_device_suspend(&first, &mixer, 0, &s);
    assert(first.system_volume == f.system && f.published == f.system && f.volume == f.system);
    mm_audioman_device_set_volume(&first, &mixer, 32768, &control, &s);
    mm_audioman_device_set_volume(&first, &mixer, 32768, &control, &s);
    assert(f.volume == 0x40002000UL);
    device_expect(&f, "RGPVV");

    control.suspended = 1;
    f.fail_suspend = 1;
    mm_audioman_device_suspend(&first, &mixer, 1, &s);
    assert(f.volume == f.system);
    mm_audioman_device_set_volume(&first, &mixer, 16384, &control, &s);
    assert(first.volume == 16384 && f.volume == f.system);
    mm_audioman_device_suspend(&first, &mixer, 0, &s);
    device_expect(&f, "VSRB");

    control.suspended = 0;
    control.active = 0;
    mm_audioman_device_set_volume(&first, &mixer, 32768, &control, &s);
    f.fail_suspend = 0;
    f.fail_get = 1;
    f.system = 0;
    mm_audioman_device_suspend(&first, &mixer, 0, &s);
    assert(f.published == 0x80004000UL && f.volume == 0x40002000UL);
    mm_audioman_device_close(&first, &mixer, &s);
    assert(!mixer.object && !f.refs);
    device_expect(&f, "RGPV-");
}

typedef struct Dispatch
{
    MMSoundEndpointList endpoints;
    int calls, retained, released, fail, created, busy[8], last_op, last_first, next_id, clear_on_release;
} Dispatch;
static void endpoint_retain(void *p, void *o)
{
    (void)o;
    ++((Dispatch *)p)->retained;
}
static void endpoint_release(void *p, void *o)
{
    Dispatch *f = (Dispatch *)p;
    (void)o;
    ++f->released;
    if (f->clear_on_release)
        f->endpoints.rows.count = 0;
}
static void *endpoint_create(void *p)
{
    Dispatch *f = (Dispatch *)p;
    ++f->created;
    return f->busy + f->created;
}
static MMI32 endpoint_call(void *p, void *o, int op, MMI32 first, MMI32 second)
{
    Dispatch *f = (Dispatch *)p;
    (void)second;
    ++f->calls;
    f->last_op = op;
    f->last_first = first;
    if (op == MM_SOUND_ALL_PLAYING)
        return *(int *)o;
    if (op == MM_SOUND_GET_VOLUME)
        return *(int *)o;
    return 0;
}
static MMI32 endpoint_play(void *p, void *o, const MMSoundPlay *v)
{
    (void)p;
    (void)o;
    return v->number;
}
static MMI32 endpoint_id(void *p)
{
    return ++((Dispatch *)p)->next_id;
}
static void dispatch(void)
{
    Dispatch f;
    MMStorageAllocator allocator;
    MMSoundManagerServices s;
    MMSoundControl c;
    MMSoundEndpoint e;
    MMSoundPlay p;
    MMI32 index;
    memset(&f, 0, sizeof(f));
    memset(&s, 0, sizeof(s));
    memset(&p, 0, sizeof(p));
    allocator.context = &f.fail;
    allocator.resize = test_storage_resize;
    allocator.release = test_storage_free;
    assert(mm_sound_endpoints_init(&f.endpoints, &allocator, 0, 1));
    s.context = &f;
    s.endpoints = &f.endpoints;
    s.retain = endpoint_retain;
    s.release = endpoint_release;
    s.call = endpoint_call;
    s.play = endpoint_play;
    s.create_queue = endpoint_create;
    s.allocate_id = endpoint_id;
    c.active = 1;
    c.suspended = 2;
    f.fail = 1;
    assert(!mm_sound_manager_add(&s, &c, 10, f.busy) && f.retained == 1 && f.released == 1);
    f.fail = 0;
    assert(mm_sound_manager_add(&s, &c, 10, f.busy));
    assert(f.calls == 3);
    assert(mm_sound_manager_add(&s, &c, 10, f.busy + 1) && f.endpoints.rows.count == 1 && f.released == 2);
    assert(mm_sound_manager_find(&s, 10, &e, &index) && !index && e.object == f.busy + 1);
    p.type = 10;
    p.number = 123;
    assert(mm_sound_manager_play(&s, &p, -1, 1) == 123 && !f.next_id);
    p.sound_class = 2;
    assert(mm_sound_manager_play(&s, &p, -1, 1) == 1 && f.next_id == 1);
    mm_sound_manager_remove(&s, 10);
    assert(f.endpoints.rows.count == 0 && f.released == 3);
    assert(mm_sound_ensure_queue(&s, 10, -2, -1, &e, &index) && f.endpoints.rows.count == 1 && f.created == 1);
    assert(mm_sound_ensure_queue(&s, 10, -2, -1, &e, &index) && f.created == 1);
    assert(mm_sound_ensure_queue(&s, 20, -2, -1, &e, &index) && f.endpoints.rows.count == 1 && e.tag == 20);
    f.busy[1] = 1;
    assert(mm_sound_ensure_queue(&s, 30, -2, -1, &e, &index) && f.created == 2 && f.endpoints.rows.count == 2);
    f.fail = 1;
    f.busy[2] = 1;
    assert(!mm_sound_ensure_queue(&s, 40, -2, -1, &e, &index) && f.released == 4);
    f.fail = 0;
    f.busy[1] = f.busy[2] = 0;
    mm_sound_queues_flush(&s, -1);
    assert(f.endpoints.rows.count == 1 && f.released == 5);
    mm_sound_activate(&s, &c, 0, 1);
    assert(!c.active && f.last_op == MM_SOUND_SUSPEND && f.last_first);
    mm_sound_suspend(&s, &c, 0, 1);
    assert(c.suspended == 1 && f.last_first);
    assert(mm_sound_next_id(2147483647) == (-2147483647 - 1));
    assert(mm_sound_manager_add(&s, &c, 40, f.busy));
    f.clear_on_release = 1;
    index = f.released;
    mm_sound_endpoints_dispose(&s, 0);
    assert(f.released == index + 1 && f.endpoints.rows.count == 0 && !f.endpoints.storage.buffers[0].data);
}
typedef struct StreamFixture
{
    MMMidiStreamEvent events[2200], output[2200];
    MMI32 count, at, output_count, batches, fail;
} StreamFixture;
static int stream_next(void *p, MMMidiEvent *e)
{
    StreamFixture *f = (StreamFixture *)p;
    if (f->at == 2010)
        return 0;
    memset(e, 0, sizeof(*e));
    e->timestamp = f->at + 1;
    e->count = 3;
    e->send[0] = (unsigned char)(0xb0 | (f->at % 16));
    e->send[1] = (unsigned char)((f->at / 16) % 120);
    e->send[2] = (unsigned char)(f->at % 128);
    ++f->at;
    return 1;
}
static int stream_append(void *p, const MMMidiStreamEvent *e, MMI32 n)
{
    StreamFixture *f = (StreamFixture *)p;
    ++f->batches;
    if (f->fail == f->batches)
        return 0;
    memcpy(f->events + f->count, e, n * sizeof(*e));
    f->count += n;
    return 1;
}
static int stream_prepend(void *p, const MMMidiStreamEvent *e, MMI32 n)
{
    StreamFixture *f = (StreamFixture *)p;
    ++f->batches;
    if (f->fail == f->batches)
        return 0;
    memmove(f->output + n, f->output, f->output_count * sizeof(*e));
    memcpy(f->output, e, n * sizeof(*e));
    f->output_count += n;
    return 1;
}
static void stream_owner(StreamFixture *f)
{
    MMMidiStream source, prefix;
    MMStorageAllocator allocator = {NULL, test_storage_resize, test_storage_free};
    void *data;
    MMI32 skip, bytes;
    allocator.context = &f->fail;
    assert(mm_midi_stream_init(&source, NULL, 0));
    assert(mm_midi_stream_init(&prefix, &allocator, 1));
    assert(mm_array_insert(&source.events, 0, f->events, f->count) == MM_STORAGE_OK);
    data = mm_midi_stream_lock(&source, &bytes);
    assert(data && bytes == f->count * 12);
    assert(mm_midi_stream_prepare_seek(&prefix, &source, 101, &skip) && skip == 102 * 12);
    assert(mm_midi_stream_dispose(&source) == MM_STORAGE_LOCKED);
    data = mm_midi_stream_lock(&prefix, &bytes);
    assert(data && bytes == 102 * 12);
    skip = -1;
    assert(!mm_midi_stream_prepare_seek(&prefix, &source, 1900, &skip) && skip == -1);
    assert(mm_midi_stream_dispose(&prefix) == MM_STORAGE_LOCKED);
    assert(!memcmp(data, f->output, 101 * 12));
    assert(mm_midi_stream_unlock(&prefix) == MM_STORAGE_OK);
    f->fail = 1;
    assert(!mm_midi_stream_prepare_seek(&prefix, &source, 1900, &skip) && skip == -1);
    assert(!mm_midi_stream_lock(&prefix, &bytes) && bytes == 0 && !prefix.storage.buffers[0].data);
    f->fail = 0;
    assert(mm_midi_stream_prepare_seek(&prefix, &source, 1900, &skip) && skip == 1901 * 12);
    data = mm_midi_stream_lock(&prefix, &bytes);
    assert(data && bytes == f->output_count * 12 && !memcmp(data, f->output, bytes));
    assert(mm_midi_stream_unlock(&prefix) == MM_STORAGE_OK);
    assert(mm_midi_stream_unlock(&source) == MM_STORAGE_OK);
    assert(mm_midi_stream_dispose(&prefix) == MM_STORAGE_OK);
    assert(mm_midi_stream_dispose(&source) == MM_STORAGE_OK);
}
static void stream(void)
{
    StreamFixture f;
    MMMidiStreamBuildServices s;
    MMU32 duration;
    MMI32 skip, i;
    memset(&f, 0, sizeof(f));
    s.context = &f;
    s.next = stream_next;
    s.append = stream_append;
    assert(mm_midi_stream_build(&s, &duration) && duration == 2010 && f.count == 2012);
    assert(f.events[0].eventWord == 0x010f4240UL && f.events[2011].eventWord == 0x02000000UL);
    f.batches = 0;
    assert(mm_midi_stream_seek(f.events, f.count, 1900, &f, stream_prepend, &skip));
    assert(skip == 1901 * 12 && f.output_count == 1901 && f.batches == 20);
    assert(f.output[0].eventWord == 0x010f4240UL && f.output[1900].eventWord == f.events[1900].eventWord);
    for (i = 0; i < f.output_count; ++i)
        assert(f.output[i].streamId == 0 && f.output[i].deltaTicks == 0);
    stream_owner(&f);
    f.output_count = f.batches = 0;
    f.fail = 2;
    assert(!mm_midi_stream_seek(f.events, f.count, 1900, &f, stream_prepend, &skip) && f.batches == 2);
    assert(!mm_midi_stream_seek(f.events, f.count, 2011, &f, stream_prepend, &skip));
    memset(&f, 0, sizeof(f));
    f.fail = 2;
    assert(!mm_midi_stream_build(&s, &duration) && f.batches == 2);
}
int main(void)
{
    arithmetic();
    mixer();
    movie_mixer();
    movie_audio_cache();
    midi();
    queue();
    audioman_queue();
    midi_queue();
    audioman_device();
    dispatch();
    stream();
    puts("C89 audio runtime: clocks, PCM mixing, MIDI scheduling, queue policy and fault cleanup passed.");
    return 0;
}
