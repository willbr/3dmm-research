#include "three_mm/audio_mixer.h"
#include "three_mm/midi_render.h"
#include "three_mm/sound_queue.h"
#include "three_mm/sound_manager.h"
#include "three_mm/midi_mix.h"
#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Fixture
{
    int fail, live, allocations, ready, messages, silences, resets, lock, releases, changed, operation, first;
    ThreeMMU32 last;
    ThreeMMSoundEntry entries[12];
    ThreeMMI32 count, current;
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
static void message(void *p, ThreeMMU32 m)
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
    ThreeMMAudioCounter a, b, q, r;
    a = three_mm_audio_count_multiply(0xffffffffUL, 0xffffffffUL);
    assert(a.high == 0xfffffffeUL && a.low == 1);
    q = three_mm_audio_count_divide(a, three_mm_audio_counter(0xffffffffUL, 0), &r);
    assert(q.low == 0xffffffffUL && q.high == 0 && !r.low && !r.high);
    a = three_mm_audio_counter(0xffffffffUL, 0xffffffffUL);
    b = three_mm_audio_counter(1, 0x80000000UL);
    q = three_mm_audio_count_divide(a, b, &r);
    assert(q.low == 1 && q.high == 0 && r.high == 0x7fffffffUL && r.low == 0xfffffffeUL);
    a = three_mm_audio_count_add(a, three_mm_audio_counter(1, 0));
    assert(!a.low && !a.high);
    a = three_mm_audio_count_sub(a, three_mm_audio_counter(1, 0));
    assert(a.low == 0xffffffffUL && a.high == 0xffffffffUL);
    assert(three_mm_audio_count_to_size(three_mm_audio_count_size((size_t)-1)) == (size_t)-1);
    assert(three_mm_sound_volume_scale(0xffffffffUL, 65536) == 0xffffffffUL);
    assert(three_mm_sound_volume_scale(0xffffffffUL, 131072) == 0xffffffffUL);
    assert(three_mm_sound_volume_scale(0xffffffffUL, 32768) == 0x7fff7fffUL);
}
static void mixer(void)
{
    Fixture f;
    ThreeMMAudioAllocator a;
    ThreeMMAudioMixer m;
    ThreeMMAudioCounter id, other;
    float samples[] = {0.25f, -0.5f, 0.75f, -0.25f}, out[10];
    ThreeMMAudioVoice v[2];
    int allocations;
    memset(&f, 0, sizeof(f));
    a.context = &f;
    a.allocate = allocate;
    a.release = release;
    three_mm_audio_mixer_init(&m, &a);
    f.fail = 1;
    id = three_mm_audio_mixer_add(&m);
    assert(!id.low && !id.high);
    f.fail = 0;
    id = three_mm_audio_mixer_add(&m);
    assert(id.low == 2);
    memset(v, 0, sizeof(v));
    v[0].id = 1;
    v[0].plays = 2;
    v[0].sample = samples;
    v[0].stereo = samples;
    v[0].frames = 2;
    v[0].volume = 1;
    assert(three_mm_audio_mixer_sync(&m, id, v, 1));
    allocations = f.allocations;
    three_mm_audio_mixer_render(&m, out, 3);
    assert(out[0] == .25f && out[2] == .75f && out[4] == .25f && f.allocations == allocations);
    f.fail = 1;
    assert(!three_mm_audio_mixer_sync(&m, id, v, 1));
    f.fail = 0;
    three_mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == .75f && three_mm_audio_mixer_done(&m, id, 1));
    assert(three_mm_audio_mixer_sync(&m, id, v, 1));
    three_mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == 0);
    v[1] = v[0];
    assert(!three_mm_audio_mixer_sync(&m, id, v, 2));
    v[0].id = 2;
    v[0].offset_ms = 0xffffffffUL;
    assert(three_mm_audio_mixer_sync(&m, id, v, 1));
    three_mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == 0 && three_mm_audio_mixer_done(&m, id, 2));
    v[0].id = 3;
    v[0].offset_ms = 0;
    v[0].plays = 0;
    three_mm_audio_mixer_begin(&m);
    three_mm_audio_mixer_begin(&m);
    assert(three_mm_audio_mixer_sync(&m, id, v, 1));
    three_mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == 0);
    three_mm_audio_mixer_end(&m);
    three_mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == 0);
    three_mm_audio_mixer_end(&m);
    three_mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == .25f);
    v[0].volume = -1;
    assert(!three_mm_audio_mixer_sync(&m, id, v, 1));
    v[0].volume = 1;
    three_mm_audio_mixer_volume(&m, -1);
    assert(m.volume == 1);
    three_mm_audio_mixer_suspend(&m, 1);
    three_mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == 0);
    three_mm_audio_mixer_suspend(&m, 0);
    three_mm_audio_mixer_render(&m, out, 1);
    assert(out[0] == .75f);
    m.next = three_mm_audio_counter(0xffffffffUL, 0xffffffffUL);
    other = three_mm_audio_mixer_add(&m);
    assert(other.high == 0xffffffffUL);
    other = three_mm_audio_mixer_add(&m);
    assert(!other.low && !other.high);
    three_mm_audio_mixer_dispose(&m);
    assert(f.live == 0);
}
static void midi(void)
{
    Fixture f;
    ThreeMMAudioAllocator a;
    ThreeMMMidiRenderServices s;
    ThreeMMMidiRenderer r;
    ThreeMMMidiStreamEvent events[] = {{1, 0, 0x00643c90UL}, {1, 0, 0x00003c80UL}};
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
    three_mm_midi_render_init(&r, &s, &a);
    assert(!three_mm_midi_render_queue(&r, events, sizeof(events), 0, 1, 42) && f.live == 0);
    f.ready = 1;
    f.fail = 1;
    assert(!three_mm_midi_render_queue(&r, events, sizeof(events), 0, 1, 42));
    f.fail = 0;
    assert(!three_mm_midi_render_queue(&r, events, sizeof(events), 1, 1, 42));
    events[0].streamId = 1;
    assert(!three_mm_midi_render_queue(&r, events, sizeof(events), 0, 1, 42));
    events[0].streamId = 0;
    assert(three_mm_midi_render_queue(&r, events, sizeof(events), 0, 2, 42));
    assert(!three_mm_midi_render_reset(&r));
    before = f.allocations;
    assert(!three_mm_midi_render(&r, out, 48) && f.messages == 1 && f.last == 0x00643c90UL);
    assert(!three_mm_midi_render(&r, out, 48) && f.messages == 2);
    assert(!three_mm_midi_render(&r, out, 48) && f.messages == 3);
    assert(three_mm_midi_render(&r, out, 48) && f.messages == 4 && f.allocations == before);
    assert(r.notes.low == 2 && r.frames.low == 192 && r.completed.low == 1);
    assert(three_mm_midi_render_pop(&r, &data, &tag) && data == events && tag == 42 && f.live == 0);
    assert(three_mm_midi_render_reset(&r) && f.resets == 1);
    events[0].deltaTicks = events[1].deltaTicks = 0;
    assert(!three_mm_midi_render_queue(&r, events, sizeof(events), 0, 0, 0));
    assert(three_mm_midi_render_queue(&r, events, sizeof(events), 0, 1, 7));
    assert(three_mm_midi_render(&r, NULL, 0));
    assert(three_mm_midi_render_pop(&r, &data, &tag) && tag == 7);
    events[0].deltaTicks = 0xffffffffUL;
    events[1].deltaTicks = 1;
    assert(three_mm_midi_render_queue(&r, events, sizeof(events), 0, 1, 9));
    assert(!three_mm_midi_render(&r, out, 1) && r.wait_frames.high > 0);
    three_mm_midi_render_stop(&r);
    assert(f.silences == 1 && !r.head);
    three_mm_midi_render_dispose(&r);
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
static ThreeMMI32 count(void *p)
{
    return ((Fixture *)p)->count;
}
static ThreeMMI32 current(void *p)
{
    return ((Fixture *)p)->current;
}
static void set_current(void *p, ThreeMMI32 n)
{
    ((Fixture *)p)->current = n;
}
static void get(void *p, ThreeMMI32 i, ThreeMMSoundEntry *out)
{
    *out = ((Fixture *)p)->entries[i];
}
static void put(void *p, ThreeMMI32 i, const ThreeMMSoundEntry *in)
{
    ((Fixture *)p)->entries[i] = *in;
}
static int add(void *p, const ThreeMMSoundEntry *in, ThreeMMI32 *index)
{
    Fixture *f = (Fixture *)p;
    if (f->fail)
        return 0;
    *index = f->count;
    f->entries[f->count++] = *in;
    return 1;
}
static void erase(void *p, ThreeMMI32 i)
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
static void changed(void *p, int op, ThreeMMI32 first)
{
    Fixture *f = (Fixture *)p;
    ++f->changed;
    f->operation = op;
    f->first = first;
}
static void flush(void *p);
static ThreeMMSoundQueueServices queue_services(Fixture *f)
{
    ThreeMMSoundQueueServices s;
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
    ThreeMMSoundQueueServices s = queue_services((Fixture *)p);
    three_mm_sound_queue_flush(&s);
}
static void queue(void)
{
    Fixture f;
    ThreeMMSoundQueueServices s;
    ThreeMMSoundEntry e;
    memset(&f, 0, sizeof(f));
    memset(&e, 0, sizeof(e));
    s = queue_services(&f);
    e.id = 1;
    e.sound_class = 2;
    e.priority = 99;
    three_mm_sound_queue_enqueue(&s, &e);
    e.id = 2;
    three_mm_sound_queue_enqueue(&s, &e);
    assert(f.count == 2 && !f.lock && f.changed == 2 && f.first == 1);
    assert(three_mm_sound_queue_priority(&s) == (-2147483647 - 1));
    three_mm_sound_queue_edit(&s, THREE_MM_SOUND_PAUSE, 1, 2, -1);
    assert(f.first == 0 && f.entries[1].pause == 1);
    three_mm_sound_queue_edit(&s, THREE_MM_SOUND_PAUSE, 0, 1, -1);
    assert(f.entries[0].pause == 2);
    three_mm_sound_queue_edit(&s, THREE_MM_SOUND_RESUME, 1, -1, -1);
    assert(f.entries[0].pause == 1 && !f.entries[1].pause);
    assert(three_mm_sound_queue_playing(&s, 0, 1, -1));
    three_mm_sound_queue_edit(&s, THREE_MM_SOUND_STOP, 0, 2, -1);
    assert(!three_mm_sound_queue_playing(&s, 0, 2, -1));
    three_mm_sound_queue_edit(&s, THREE_MM_SOUND_RESUME, 0, 2, -1);
    assert(f.entries[1].pause == -1);
    f.current = 1;
    three_mm_sound_queue_flush(&s);
    assert(f.count == 1 && !f.current && f.releases == 1);
    f.fail = 1;
    three_mm_sound_queue_enqueue(&s, &e);
    assert(f.releases == 2 && f.count == 1);
    three_mm_sound_queue_release(&s);
    assert(f.releases == 3 && !f.lock);
}
typedef struct Dispatch
{
    ThreeMMSoundEndpoint entries[8];
    int count, calls, retained, released, fail, created, busy[8], last_op, last_first, next_id, clear_on_release;
} Dispatch;
static ThreeMMI32 endpoint_count(void *p)
{
    return ((Dispatch *)p)->count;
}
static void endpoint_get(void *p, ThreeMMI32 i, ThreeMMSoundEndpoint *e)
{
    *e = ((Dispatch *)p)->entries[i];
}
static void endpoint_put(void *p, ThreeMMI32 i, const ThreeMMSoundEndpoint *e)
{
    ((Dispatch *)p)->entries[i] = *e;
}
static int endpoint_insert(void *p, ThreeMMI32 i, const ThreeMMSoundEndpoint *e)
{
    Dispatch *f = (Dispatch *)p;
    if (f->fail)
        return 0;
    memmove(f->entries + i + 1, f->entries + i, (f->count - i) * sizeof(*e));
    f->entries[i] = *e;
    ++f->count;
    return 1;
}
static void endpoint_erase(void *p, ThreeMMI32 i)
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
static ThreeMMI32 endpoint_call(void *p, void *o, int op, ThreeMMI32 first, ThreeMMI32 second)
{
    Dispatch *f = (Dispatch *)p;
    (void)second;
    ++f->calls;
    f->last_op = op;
    f->last_first = first;
    if (op == THREE_MM_SOUND_ALL_PLAYING)
        return *(int *)o;
    if (op == THREE_MM_SOUND_GET_VOLUME)
        return *(int *)o;
    return 0;
}
static ThreeMMI32 endpoint_play(void *p, void *o, const ThreeMMSoundPlay *v)
{
    (void)p;
    (void)o;
    return v->number;
}
static ThreeMMI32 endpoint_id(void *p)
{
    return ++((Dispatch *)p)->next_id;
}
static void dispatch(void)
{
    Dispatch f;
    ThreeMMSoundManagerServices s;
    ThreeMMSoundControl c;
    ThreeMMSoundEndpoint e;
    ThreeMMSoundPlay p;
    ThreeMMI32 index;
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
    assert(!three_mm_sound_manager_add(&s, &c, 10, f.busy) && f.retained == 1 && f.released == 1);
    f.fail = 0;
    assert(three_mm_sound_manager_add(&s, &c, 10, f.busy));
    assert(f.calls == 3);
    assert(three_mm_sound_manager_add(&s, &c, 10, f.busy + 1) && f.count == 1 && f.released == 2);
    assert(three_mm_sound_manager_find(&s, 10, &e, &index) && !index && e.object == f.busy + 1);
    p.type = 10;
    p.number = 123;
    assert(three_mm_sound_manager_play(&s, &p, -1, 1) == 123 && !f.next_id);
    p.sound_class = 2;
    assert(three_mm_sound_manager_play(&s, &p, -1, 1) == 1 && f.next_id == 1);
    three_mm_sound_manager_remove(&s, 10);
    assert(f.count == 0 && f.released == 3);
    assert(three_mm_sound_ensure_queue(&s, 10, -2, -1, &e, &index) && f.count == 1 && f.created == 1);
    assert(three_mm_sound_ensure_queue(&s, 10, -2, -1, &e, &index) && f.created == 1);
    assert(three_mm_sound_ensure_queue(&s, 20, -2, -1, &e, &index) && f.count == 1 && e.tag == 20);
    f.busy[1] = 1;
    assert(three_mm_sound_ensure_queue(&s, 30, -2, -1, &e, &index) && f.created == 2 && f.count == 2);
    f.fail = 1;
    f.busy[2] = 1;
    assert(!three_mm_sound_ensure_queue(&s, 40, -2, -1, &e, &index) && f.released == 4);
    f.fail = 0;
    f.busy[1] = f.busy[2] = 0;
    three_mm_sound_queues_flush(&s, -1);
    assert(f.count == 1 && f.released == 5);
    three_mm_sound_activate(&s, &c, 0, 1);
    assert(!c.active && f.last_op == THREE_MM_SOUND_SUSPEND && f.last_first);
    three_mm_sound_suspend(&s, &c, 0, 1);
    assert(c.suspended == 1 && f.last_first);
    assert(three_mm_sound_next_id(2147483647) == (-2147483647 - 1));
    f.count = 2;
    f.clear_on_release = 1;
    index = f.released;
    three_mm_sound_endpoints_release(&s, 0);
    assert(f.released == index + 1);
}
typedef struct StreamFixture
{
    ThreeMMMidiStreamEvent events[2200], output[2200];
    ThreeMMI32 count, at, output_count, batches, fail;
} StreamFixture;
static int stream_next(void *p, ThreeMMMidiEvent *e)
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
static int stream_append(void *p, const ThreeMMMidiStreamEvent *e, ThreeMMI32 n)
{
    StreamFixture *f = (StreamFixture *)p;
    ++f->batches;
    if (f->fail == f->batches)
        return 0;
    memcpy(f->events + f->count, e, n * sizeof(*e));
    f->count += n;
    return 1;
}
static int stream_prepend(void *p, const ThreeMMMidiStreamEvent *e, ThreeMMI32 n)
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
    ThreeMMMidiStreamBuildServices s;
    ThreeMMU32 duration;
    ThreeMMI32 skip, i;
    memset(&f, 0, sizeof(f));
    s.context = &f;
    s.next = stream_next;
    s.append = stream_append;
    assert(three_mm_midi_stream_build(&s, &duration) && duration == 2010 && f.count == 2012);
    assert(f.events[0].eventWord == 0x010f4240UL && f.events[2011].eventWord == 0x02000000UL);
    f.batches = 0;
    assert(three_mm_midi_stream_seek(f.events, f.count, 1900, &f, stream_prepend, &skip));
    assert(skip == 1901 * 12 && f.output_count == 1901 && f.batches == 20);
    assert(f.output[0].eventWord == 0x010f4240UL && f.output[1900].eventWord == f.events[1900].eventWord);
    for (i = 0; i < f.output_count; ++i)
        assert(f.output[i].streamId == 0 && f.output[i].deltaTicks == 0);
    f.output_count = f.batches = 0;
    f.fail = 2;
    assert(!three_mm_midi_stream_seek(f.events, f.count, 1900, &f, stream_prepend, &skip) && f.batches == 2);
    assert(!three_mm_midi_stream_seek(f.events, f.count, 2011, &f, stream_prepend, &skip));
    memset(&f, 0, sizeof(f));
    f.fail = 2;
    assert(!three_mm_midi_stream_build(&s, &duration) && f.batches == 2);
}
int main(void)
{
    arithmetic();
    mixer();
    midi();
    queue();
    dispatch();
    stream();
    puts("C89 audio runtime: clocks, PCM mixing, MIDI scheduling, queue policy and fault cleanup passed.");
    return 0;
}
