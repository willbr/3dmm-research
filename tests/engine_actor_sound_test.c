#include "three_mm/actor_sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok) { fprintf(stderr, "actor sound failed at %d\n", line); exit(1); }
}
#define CHECK(x) check((x), __LINE__)
enum { COUNT = 1, RESOLVE, WRITE, FETCH, PRIORITY, ENQUEUE, RELEASE, DIAGNOSTIC, CUE };
typedef struct Host
{
    int log[128], calls, refs, token, count, bad_count, missing, bad_resolve, bad_enqueue, bad_cue;
    int kind, low, queued, written;
    ThreeMMActorSound saved;
} Host;
static void log_call(Host *h, int op) { CHECK(h->calls < 128); h->log[h->calls++] = op; }
static int cel_count(void *p, ThreeMMI32 action, ThreeMMI32 *count)
{
    Host *h = (Host *)p;
    CHECK(action == 4); log_call(h, COUNT); *count = h->count; return !h->bad_count;
}
static int resolve(void *p, ThreeMMTag *tag, ThreeMMU32 child)
{
    Host *h = (Host *)p;
    int failed = (int)tag->number == h->bad_resolve;
    CHECK(child == 89 && tag->sid == 0); log_call(h, RESOLVE);
    tag->number += 1000; tag->sid = 7;
    return !failed;
}
static void write_event(void *p, ThreeMMI32 i, const ThreeMMActorSound *s)
{
    Host *h = (Host *)p; CHECK(i == 3); log_call(h, WRITE); ++h->written; h->saved = *s;
}
static void diagnostic(void *p) { log_call((Host *)p, DIAGNOSTIC); }
static void *fetch(void *p, const ThreeMMTag *tag)
{
    Host *h = (Host *)p; log_call(h, FETCH);
    if ((int)tag->number == h->missing) return NULL;
    ++h->refs; return &h->token;
}
static ThreeMMI32 priority(void *p, void *resource, int kind)
{
    Host *h = (Host *)p; CHECK(resource == &h->token && h->refs == 1);
    log_call(h, PRIORITY); h->kind = kind; return 100 + kind;
}
static int enqueue(void *p, void *resource, const ThreeMMActorSound *s, ThreeMMI32 pri, int low)
{
    Host *h = (Host *)p;
    CHECK(resource == &h->token && h->refs == 1 && pri == 100 + h->kind);
    CHECK(s->volume == 765 && s->queue == -1);
    log_call(h, ENQUEUE); h->low = low; ++h->queued;
    return (int)s->tag.number != h->bad_enqueue;
}
static void release(void *p, void *resource)
{
    Host *h = (Host *)p; CHECK(resource == &h->token && h->refs == 1); log_call(h, RELEASE); --h->refs;
}
static int cue(void *p, const ThreeMMActorSound *sound)
{
    Host *h = (Host *)p; log_call(h, CUE); ++h->queued; h->saved = *sound;
    return (int)sound->tag.number != h->bad_cue;
}
static ThreeMMActorSoundServices services(Host *h)
{
    ThreeMMActorSoundServices s;
    memset(h, 0, sizeof(*h)); h->count = 4; h->missing = h->bad_resolve = h->bad_enqueue = h->bad_cue = -1;
    memset(&s, 0, sizeof(s)); s.context = h; s.cel_count = cel_count; s.resolve = resolve;
    s.write_event = write_event; s.resolution_failed = diagnostic; s.fetch = fetch;
    s.priority = priority; s.enqueue = enqueue; s.release = release; s.cue = cue;
    return s;
}
static ThreeMMActorSound sound(int cel, int style, int number)
{
    ThreeMMActorSound s;
    memset(&s, 0, sizeof(s)); s.cel = cel; s.style = style; s.tag.sid = 2;
    s.tag.number = number; s.child = 89; s.volume = 765; s.queue = -1; return s;
}
static ThreeMMActorEvent event(void)
{
    ThreeMMActorEvent e;
    memset(&e, 0, sizeof(e)); e.aet = THREE_MM_ACTOR_SOUND; e.nfrm = 17; return e;
}
static void expect(Host *h, const int *ops, int n)
{
    CHECK(h->calls == n && memcmp(h->log, ops, (size_t)n * sizeof(int)) == 0 && h->refs == 0);
}
typedef struct Allocator { int fail, live; } Allocator;
static void *resize(void *context, void *old, ThreeMMU32 old_bytes, ThreeMMU32 bytes, int pri)
{
    Allocator *a = (Allocator *)context; void *out;
    (void)old_bytes; (void)pri;
    if (a->fail) return NULL;
    out = realloc(old, bytes); if (out && !old) ++a->live; return out;
}
static void free_block(void *context, void *p, ThreeMMU32 bytes)
{
    Allocator *a = (Allocator *)context; (void)bytes; CHECK(a->live > 0); --a->live; free(p);
}
static void storage_tests(void)
{
    ThreeMMActorSounds a, b;
    ThreeMMActorMotionSound record;
    ThreeMMActorSound s;
    ThreeMMActorEvent e = event();
    ThreeMMStorageAllocator allocator;
    Allocator memory = {0, 0};
    unsigned i;
    allocator.context = &memory; allocator.resize = resize; allocator.release = free_block;
    CHECK(three_mm_actor_sounds_init(&a, &allocator) && three_mm_actor_sounds_init(&b, &allocator));
    memory.fail = 1; s = sound(2, 1, 21);
    CHECK(!three_mm_actor_sounds_insert(&a, &e, &s) && a.count == 0 && memory.live == 0);
    memory.fail = 0;
    for (i = 0; i < 32; ++i)
    {
        s = sound(2, (int)i, (int)i); CHECK(three_mm_actor_sounds_insert(&a, &e, &s));
        if (!i) CHECK(a.storage.buffers[0].capacity == 2 * sizeof(ThreeMMActorMotionSound));
    }
    memory.fail = 1; s = sound(3, 4, 19);
    CHECK(!three_mm_actor_sounds_insert(&a, &e, &s) && a.count == 32);
    s = sound(2, 4, 91); e.nfrm = 101;
    CHECK(three_mm_actor_sounds_insert(&a, &e, &s) && a.count == 32);
    CHECK(three_mm_actor_sounds_get(&a, 4, &record) && record.event.nfrm == 101 && record.sound.tag.number == 91);
    CHECK(!three_mm_actor_sounds_get(&a, 32, &record) && !three_mm_actor_sounds_delete(&a, 32));
    CHECK(!three_mm_actor_sounds_clone(&b, &a) && b.count == 0);
    memory.fail = 0; s = sound(1, 0, 99); CHECK(three_mm_actor_sounds_insert(&b, &e, &s));
    memory.fail = 1;
    CHECK(!three_mm_actor_sounds_clone(&b, &a) && b.count == 1);
    CHECK(three_mm_actor_sounds_get(&b, 0, &record) && record.sound.tag.number == 99);
    memory.fail = 0;
    CHECK(three_mm_actor_sounds_clone(&b, &a) && b.count == 32 && three_mm_actor_sounds_clone(&b, &b));
    CHECK(three_mm_actor_sounds_delete(&a, 0) && a.count == 31);
    CHECK(three_mm_actor_sounds_get(&a, 0, &record) && record.sound.style == 1);
    CHECK(three_mm_actor_sounds_get(&b, 0, &record) && record.sound.style == 0);
    s = sound(4, 80, 77); CHECK(three_mm_actor_sounds_insert(&a, &e, &s));
    CHECK(three_mm_actor_sounds_put(&a, 0, &record));
    CHECK(three_mm_actor_sounds_insert(&a, &((ThreeMMActorMotionSound *)a.storage.buffers[0].data)[0].event,
                                      &((ThreeMMActorMotionSound *)a.storage.buffers[0].data)[0].sound));
    three_mm_actor_sounds_clear(&a); CHECK(a.count == 0 && a.storage.buffers[0].capacity > 0);
    three_mm_actor_sounds_swap(&a, &b); CHECK(a.count == 32 && b.count == 0);
    CHECK(!three_mm_actor_sounds_reserve(&a, 0xffffffffUL) && a.count == 32);
    three_mm_actor_sounds_dispose(&a); three_mm_actor_sounds_dispose(&b);
    CHECK(memory.live == 0);
}
static void ordinary_tests(void)
{
    ThreeMMActorSounds list;
    ThreeMMActorEvent e = event();
    ThreeMMActorSound s;
    ThreeMMActorSoundServices callbacks;
    Host h;
    int imported, loop, failure, ok;
    const int direct[] = {FETCH, PRIORITY, ENQUEUE, RELEASE};
    const int resolved[] = {RESOLVE, WRITE, FETCH, PRIORITY, ENQUEUE, RELEASE};
    const int bad_resolution[] = {RESOLVE, DIAGNOSTIC};
    CHECK(three_mm_actor_sounds_init(&list, NULL));
    for (imported = 0; imported < 2; ++imported)
        for (loop = -1; loop < 2; ++loop)
            for (failure = 0; failure < 4; ++failure)
            {
                callbacks = services(&h); s = sound(-1, 1, 21); s.loop = loop; s.tag.sid = imported ? 0 : 2;
                if (failure == 1) h.bad_resolve = 21;
                if (failure == 2) h.missing = imported ? 1021 : 21;
                if (failure == 3) h.bad_enqueue = imported ? 1021 : 21;
                ok = three_mm_actor_sound_event(&list, &e, &s, 3, &callbacks);
                CHECK(ok == (failure == 0 || (failure == 1 && !imported)) && list.count == 0 && s.tag.number == 21);
                if (imported && failure == 1) expect(&h, bad_resolution, 2);
                else if (failure == 2) expect(&h, imported ? resolved : direct, imported ? 3 : 1);
                else
                {
                    expect(&h, imported ? resolved : direct, imported ? 6 : 4);
                    CHECK(h.kind == (loop ? THREE_MM_ACTOR_SOUND_LOOP : THREE_MM_ACTOR_SOUND_ORDINARY) && !h.low);
                }
                CHECK(h.written == (imported && failure != 1));
                if (h.written) CHECK(h.saved.tag.sid == 7 && h.saved.tag.number == 1021);
            }
    callbacks = services(&h); callbacks.cues_only = 1;
    callbacks.resolve = NULL; callbacks.fetch = NULL; callbacks.enqueue = NULL;
    s = sound(-1, 1, 21); s.tag.sid = 0;
    CHECK(three_mm_actor_sound_event(&list, &e, &s, 3, &callbacks) && h.calls == 1 && h.log[0] == CUE && h.saved.tag.sid == 0);
    h.bad_cue = 21; CHECK(!three_mm_actor_sound_event(&list, &e, &s, 3, &callbacks));
    three_mm_actor_sounds_dispose(&list);
}
static void motion_tests(void)
{
    ThreeMMActorSounds list;
    ThreeMMActorEvent e = event();
    ThreeMMActorSound s;
    ThreeMMActorMotionSound record;
    ThreeMMActorSoundServices callbacks;
    Host h;
    int failure, imported, ok;
    const int success[] = {COUNT, FETCH, PRIORITY, ENQUEUE, RELEASE, FETCH, PRIORITY, ENQUEUE, RELEASE};
    const int missing[] = {COUNT, FETCH, FETCH, PRIORITY, ENQUEUE, RELEASE};
    const int resolving[] = {COUNT, RESOLVE, FETCH, PRIORITY, ENQUEUE, RELEASE, RESOLVE, FETCH, PRIORITY, ENQUEUE, RELEASE};
    for (imported = 0; imported < 2; ++imported)
        for (failure = 0; failure < 4; ++failure)
        {
            CHECK(three_mm_actor_sounds_init(&list, NULL)); callbacks = services(&h);
            s = sound(2, 1, 21); s.tag.sid = imported ? 0 : 2;
            CHECK(three_mm_actor_sound_event(&list, &e, &s, 3, &callbacks));
            s.style = 2; s.tag.number = 22;
            CHECK(three_mm_actor_sound_event(&list, &e, &s, 3, &callbacks) && h.calls == 0 && list.count == 2);
            if (failure == 1) h.missing = imported ? 1021 : 21;
            if (failure == 2) h.bad_resolve = 21;
            if (failure == 3) h.bad_enqueue = imported ? 1021 : 21;
            ok = three_mm_actor_sound_motion(&list, 4, 10, &callbacks);
            CHECK(ok == (failure == 0 || (failure == 2 && !imported)) && h.refs == 0);
            if (failure == 2 && imported)
            {
                const int failed[] = {COUNT, RESOLVE, DIAGNOSTIC}; expect(&h, failed, 3);
                CHECK(three_mm_actor_sounds_get(&list, 0, &record) && record.sound.tag.sid == 7 && record.sound.tag.number == 1021);
            }
            else if (failure == 3) expect(&h, imported ? resolving : success, imported ? 6 : 5);
            else if (failure == 1 && !imported) expect(&h, missing, 6);
            else if (failure == 1)
            {
                const int partial[] = {COUNT, RESOLVE, FETCH, RESOLVE, FETCH, PRIORITY, ENQUEUE, RELEASE};
                expect(&h, partial, 8);
            }
            else if (failure != 1) expect(&h, imported ? resolving : success, imported ? 11 : 9);
            CHECK(h.written == 0);
            if (failure == 1) CHECK(h.queued == 1);
            if (h.queued) CHECK(h.low && h.kind == THREE_MM_ACTOR_SOUND_MATCH);
            three_mm_actor_sounds_dispose(&list);
        }
    CHECK(three_mm_actor_sounds_init(&list, NULL)); callbacks = services(&h);
    CHECK(three_mm_actor_sound_motion(&list, 4, 10, &callbacks) && h.calls == 1 && h.log[0] == COUNT);
    callbacks = services(&h); h.count = 0; CHECK(!three_mm_actor_sound_motion(&list, 4, 10, &callbacks));
    callbacks = services(&h); h.bad_count = 1; CHECK(!three_mm_actor_sound_motion(&list, 4, 10, &callbacks));
    s = sound(-2, 1, 21); CHECK(three_mm_actor_sounds_insert(&list, &e, &s));
    callbacks = services(&h); callbacks.cues_only = 1;
    CHECK(three_mm_actor_sound_motion(&list, 4, -10, &callbacks) && h.calls == 2 && h.saved.cel == -2);
    callbacks = services(&h); callbacks.cues_only = 1; h.count = 3;
    CHECK(three_mm_actor_sound_motion(&list, 4, (-2147483647L - 1), &callbacks) && h.calls == 2);
    callbacks = services(&h); callbacks.cues_only = 1;
    CHECK(three_mm_actor_sound_motion(&list, 4, 10, &callbacks) && h.calls == 1);
    three_mm_actor_sounds_dispose(&list);
}
int main(void)
{
    storage_tests(); ordinary_tests(); motion_tests();
    puts("actor sound storage and policy passed"); return 0;
}
