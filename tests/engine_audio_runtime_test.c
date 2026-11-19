#include "mm/audio_mixer.h"
#include "mm/midi_render.h"
#include "mm/sound_queue.h"
#include "mm/sound_manager.h"
#include "mm/midi_mix.h"
#include "mm/movie_audio.h"
#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Fixture
{
    int fail, live, allocations, ready, messages, silences, resets, lock, releases, changed, operation, first;
    MMU32 last;
    MMSoundEntry entries[12];
    MMI32 count, current;
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
static void mixer(void)
{
    Fixture f;
    MMAudioAllocator a;
    MMAudioMixer m;
    MMAudioCounter id, other;
    float samples[] = {0.25f, -0.5f, 0.75f, -0.25f}, out[10];
    MMAudioVoice v[2];
    int allocations;
    memset(&f, 0, sizeof(f));
    a.context = &f;
    a.allocate = allocate;
    a.release = release;
    mm_audio_mixer_init(&m, &a);
    f.fail = 1;
    id = mm_audio_mixer_add(&m);
    assert(!id.low && !id.high);
    f.fail = 0;
    id = mm_audio_mixer_add(&m);
    assert(id.low == 2);
    memset(v, 0, sizeof(v));
    v[0].id = 1;
    v[0].plays = 2;
    v[0].sample = samples;
    v[0].stereo = samples;
    v[0].frames = 2;
    v[0].volume = 1;
    assert(mm_audio_mixer_sync(&m, id, v, 1));
    allocations = f.allocations;
    mm_audio_mixer_render(&m, out, 3);
    assert(out[0] == .25f && out[2] == .75f && out[4] == .25f && f.allocations == allocations);
    f.fail = 1;
    assert(!mm_audio_mixer_sync(&m, id, v, 1));
    f.fail = 0;
    mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == .75f && mm_audio_mixer_done(&m, id, 1));
    assert(mm_audio_mixer_sync(&m, id, v, 1));
    mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == 0);
    v[1] = v[0];
    assert(!mm_audio_mixer_sync(&m, id, v, 2));
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
    other = mm_audio_mixer_add(&m);
    assert(!other.low && !other.high);
    mm_audio_mixer_dispose(&m);
    assert(f.live == 0);
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
static MMI32 count(void *p)
{
    return ((Fixture *)p)->count;
}
static MMI32 current(void *p)
{
    return ((Fixture *)p)->current;
}
static void set_current(void *p, MMI32 n)
{
    ((Fixture *)p)->current = n;
}
static void get(void *p, MMI32 i, MMSoundEntry *out)
{
    *out = ((Fixture *)p)->entries[i];
}
static void put(void *p, MMI32 i, const MMSoundEntry *in)
{
    ((Fixture *)p)->entries[i] = *in;
}
static int add(void *p, const MMSoundEntry *in, MMI32 *index)
{
    Fixture *f = (Fixture *)p;
    if (f->fail)
        return 0;
    *index = f->count;
    f->entries[f->count++] = *in;
    return 1;
}
static void erase(void *p, MMI32 i)
{
    Fixture *f = (Fixture *)p;
    --f->count;
    memmove(f->entries + i, f->entries + i + 1, (f->count - i) * sizeof(*f->entries));
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
    s.enter = enter;
    s.leave = leave;
    s.flush = flush;
    s.count = count;
    s.current = current;
    s.set_current = set_current;
    s.get = get;
    s.put = put;
    s.add = add;
    s.erase = erase;
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
    s = queue_services(&f);
    e.id = 1;
    e.sound_class = 2;
    e.priority = 99;
    mm_sound_queue_enqueue(&s, &e);
    e.id = 2;
    mm_sound_queue_enqueue(&s, &e);
    assert(f.count == 2 && !f.lock && f.changed == 2 && f.first == 1);
    assert(mm_sound_queue_priority(&s) == (-2147483647 - 1));
    mm_sound_queue_edit(&s, MM_SOUND_PAUSE, 1, 2, -1);
    assert(f.first == 0 && f.entries[1].pause == 1);
    mm_sound_queue_edit(&s, MM_SOUND_PAUSE, 0, 1, -1);
    assert(f.entries[0].pause == 2);
    mm_sound_queue_edit(&s, MM_SOUND_RESUME, 1, -1, -1);
    assert(f.entries[0].pause == 1 && !f.entries[1].pause);
    assert(mm_sound_queue_playing(&s, 0, 1, -1));
    mm_sound_queue_edit(&s, MM_SOUND_STOP, 0, 2, -1);
    assert(!mm_sound_queue_playing(&s, 0, 2, -1));
    mm_sound_queue_edit(&s, MM_SOUND_RESUME, 0, 2, -1);
    assert(f.entries[1].pause == -1);
    f.current = 1;
    mm_sound_queue_flush(&s);
    assert(f.count == 1 && !f.current && f.releases == 1);
    f.fail = 1;
    mm_sound_queue_enqueue(&s, &e);
    assert(f.releases == 2 && f.count == 1);
    mm_sound_queue_release(&s);
    assert(f.releases == 3 && !f.lock);
}
typedef struct Dispatch
{
    MMSoundEndpoint entries[8];
    int count, calls, retained, released, fail, created, busy[8], last_op, last_first, next_id, clear_on_release;
} Dispatch;
static MMI32 endpoint_count(void *p)
{
    return ((Dispatch *)p)->count;
}
static void endpoint_get(void *p, MMI32 i, MMSoundEndpoint *e)
{
    *e = ((Dispatch *)p)->entries[i];
}
static void endpoint_put(void *p, MMI32 i, const MMSoundEndpoint *e)
{
    ((Dispatch *)p)->entries[i] = *e;
}
static int endpoint_insert(void *p, MMI32 i, const MMSoundEndpoint *e)
{
    Dispatch *f = (Dispatch *)p;
    if (f->fail)
        return 0;
    memmove(f->entries + i + 1, f->entries + i, (f->count - i) * sizeof(*e));
    f->entries[i] = *e;
    ++f->count;
    return 1;
}
static void endpoint_erase(void *p, MMI32 i)
{
    Dispatch *f = (Dispatch *)p;
    --f->count;
    memmove(f->entries + i, f->entries + i + 1, (f->count - i) * sizeof(*f->entries));
}
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
        f->count = 0;
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
    MMSoundManagerServices s;
    MMSoundControl c;
    MMSoundEndpoint e;
    MMSoundPlay p;
    MMI32 index;
    memset(&f, 0, sizeof(f));
    memset(&s, 0, sizeof(s));
    memset(&p, 0, sizeof(p));
    s.context = &f;
    s.count = endpoint_count;
    s.get = endpoint_get;
    s.put = endpoint_put;
    s.insert = endpoint_insert;
    s.erase = endpoint_erase;
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
    assert(mm_sound_manager_add(&s, &c, 10, f.busy + 1) && f.count == 1 && f.released == 2);
    assert(mm_sound_manager_find(&s, 10, &e, &index) && !index && e.object == f.busy + 1);
    p.type = 10;
    p.number = 123;
    assert(mm_sound_manager_play(&s, &p, -1, 1) == 123 && !f.next_id);
    p.sound_class = 2;
    assert(mm_sound_manager_play(&s, &p, -1, 1) == 1 && f.next_id == 1);
    mm_sound_manager_remove(&s, 10);
    assert(f.count == 0 && f.released == 3);
    assert(mm_sound_ensure_queue(&s, 10, -2, -1, &e, &index) && f.count == 1 && f.created == 1);
    assert(mm_sound_ensure_queue(&s, 10, -2, -1, &e, &index) && f.created == 1);
    assert(mm_sound_ensure_queue(&s, 20, -2, -1, &e, &index) && f.count == 1 && e.tag == 20);
    f.busy[1] = 1;
    assert(mm_sound_ensure_queue(&s, 30, -2, -1, &e, &index) && f.created == 2 && f.count == 2);
    f.fail = 1;
    f.busy[2] = 1;
    assert(!mm_sound_ensure_queue(&s, 40, -2, -1, &e, &index) && f.released == 4);
    f.fail = 0;
    f.busy[1] = f.busy[2] = 0;
    mm_sound_queues_flush(&s, -1);
    assert(f.count == 1 && f.released == 5);
    mm_sound_activate(&s, &c, 0, 1);
    assert(!c.active && f.last_op == MM_SOUND_SUSPEND && f.last_first);
    mm_sound_suspend(&s, &c, 0, 1);
    assert(c.suspended == 1 && f.last_first);
    assert(mm_sound_next_id(2147483647) == (-2147483647 - 1));
    f.count = 2;
    f.clear_on_release = 1;
    index = f.released;
    mm_sound_endpoints_release(&s, 0);
    assert(f.released == index + 1);
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
    midi();
    queue();
    dispatch();
    stream();
    puts("C89 audio runtime: clocks, PCM mixing, MIDI scheduling, queue policy and fault cleanup passed.");
    return 0;
}
