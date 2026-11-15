#include "mm/preview_metadata.h"
#include "mm/scene_data.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ONE 65536L
typedef struct Track
{
    MMActorRoutePoint route[2];
    MMActorEvent events[8];
    unsigned char payload[8][64];
    MMI32 sizes[8], count, routes;
    int many;
} Track;
typedef struct Timeline
{
    MMPreviewActor actor;
    Track live, copy;
    MMPreviewTimelineServices services;
    MMPreviewMarker markers[16];
    MMPreviewSpan spans[16];
    unsigned marker_count, span_count;
    int calls, fail_at, failure, fault, owners, released;
} Timeline;
static int tl_step(Timeline *h)
{
    ++h->calls;
    if (h->calls == h->fail_at)
    {
        if (h->failure < 0)
            h->fault = h->failure;
        return h->failure;
    }
    return 1;
}
static MMU32 route_width(void *p)
{
    (void)p;
    return 16;
}
static MMI32 route_count(void *p)
{
    return ((Track *)p)->routes;
}
static void point(void *p, MMI32 i, MMActorRoutePoint *out)
{
    Track *t = (Track *)p;
    assert(i >= 0 && i < t->routes);
    *out = t->route[i];
}
static MMU32 event_width(void *p)
{
    (void)p;
    return 20;
}
static MMI32 event_count(void *p)
{
    return ((Track *)p)->count;
}
static void event(void *p, MMI32 i, MMActorEvent *out)
{
    Track *t = (Track *)p;
    assert(i >= 0 && i < t->count);
    if (!t->many)
        *out = t->events[i];
    else
    {
        memset(out, 0, sizeof(*out));
        out->nfrm = 1;
        out->aet = i == 0 ? MM_ACTOR_ADD : i == t->count - 1 ? MM_ACTOR_SOUND : MM_ACTOR_SIZE;
    }
}
static MMI32 size(void *p, MMI32 i)
{
    Track *t = (Track *)p;
    return t->many ? (i == 0              ? (MMI32)sizeof(MMActorEntrance)
                      : i == t->count - 1 ? (MMI32)sizeof(MMActorSound)
                                          : 4)
                   : t->sizes[i];
}
static void payload(void *p, MMI32 i, void *out)
{
    Track *t = (Track *)p;
    if (!t->many)
        memcpy(out, t->payload[i], (size_t)t->sizes[i]);
    else if (i == 0)
        memset(out, 0, sizeof(MMActorEntrance));
    else
    {
        MMI32 one = ONE;
        memcpy(out, &one, 4);
    }
}
static void put(void *p, MMI32 i, const MMActorEvent *e)
{
    Track *t = (Track *)p;
    if (!t->many)
        t->events[i] = *e;
}
static int resolve(void *p, MMI32 action, MMI32 cel, MMI32 *out)
{
    Timeline *h = (Timeline *)p;
    (void)action;
    (void)cel;
    *out = ONE;
    return tl_step(h) > 0;
}
static void reset_timeline(void *p)
{
    Timeline *h = (Timeline *)p;
    h->marker_count = h->span_count = 0;
}
static int clone(void *p, void **owner, void **route, void **events)
{
    Timeline *h = (Timeline *)p;
    int r = tl_step(h);
    if (r >= 0)
    {
        h->copy = h->live;
        *owner = *route = *events = &h->copy;
        ++h->owners;
    }
    return r;
}
static void release_track(void *p, void *owner)
{
    Timeline *h = (Timeline *)p;
    assert(owner == &h->copy && h->owners == 1);
    --h->owners;
    ++h->released;
}
static int tl_fault(void *p)
{
    return ((Timeline *)p)->fault;
}
static int marker_emit(void *p, const MMPreviewMarker *m)
{
    Timeline *h = (Timeline *)p;
    int r = tl_step(h);
    if (r > 0)
    {
        if (h->marker_count < 16)
            h->markers[h->marker_count] = *m;
        ++h->marker_count;
    }
    return r;
}
static int span_emit(void *p, const MMPreviewSpan *span)
{
    Timeline *h = (Timeline *)p;
    int r = tl_step(h);
    if (r > 0)
    {
        assert(h->span_count < 16);
        h->spans[h->span_count++] = *span;
    }
    return r;
}
static void extend(void *p, MMWide end)
{
    Timeline *h = (Timeline *)p;
    assert(h->span_count);
    h->spans[h->span_count - 1].end = end;
}
static void effect(void *p, const MMActorEffectCommand *c, MMActorEffectReply *r)
{
    (void)p;
    assert(c->operation == MM_EFFECT_CEL_COUNT || c->operation == MM_EFFECT_ACTION_FLAGS);
    r->value = 4;
    r->flags = 0;
}
static int apply(void *p, MMActorState *state, void *owner, MMI32 index)
{
    Timeline *h = (Timeline *)p;
    MMActorEffectServices effects;
    MMActorPoint origin;
    int r = tl_step(h);
    if (r <= 0)
        return r;
    memset(&effects, 0, sizeof(effects));
    memset(&origin, 0, sizeof(origin));
    effects.context = h;
    effects.run = effect;
    effects.write_event = put;
    return mm_actor_effect_apply(state, owner, owner, index, &origin, 0, 1, &h->services.timeline.motion, &effects);
}
static void tl_setup(Timeline *h)
{
    MMPreviewTimelineServices *s = &h->services;
    MMActorEntrance add;
    MMI32 action[2] = {0, 0}, stride = -ONE, frozen = 1;
    memset(h, 0, sizeof(*h));
    memset(&add, 0, sizeof(add));
    h->actor.header.first_frame = 1;
    h->actor.header.last_frame = 5;
    h->actor.state.frame = 99;
    h->live.routes = 2;
    h->live.route[0].dwr = 4 * ONE;
    h->live.route[1].xyz.dxr = 4 * ONE;
    h->live.count = 4;
    h->live.events[0].aet = MM_ACTOR_ADD;
    h->live.events[0].nfrm = 1;
    h->live.events[1].aet = MM_ACTOR_ACTION;
    h->live.events[1].nfrm = 999;
    h->live.events[2].aet = MM_ACTOR_STEP;
    h->live.events[2].nfrm = 999;
    h->live.events[3].aet = MM_ACTOR_FREEZE;
    h->live.events[3].rtel.dwrOffset = ONE;
    h->live.events[3].nfrm = 999;
    h->live.sizes[0] = sizeof(add);
    h->live.sizes[1] = sizeof(action);
    h->live.sizes[2] = h->live.sizes[3] = 4;
    memcpy(h->live.payload[0], &add, sizeof(add));
    memcpy(h->live.payload[1], action, sizeof(action));
    memcpy(h->live.payload[2], &stride, 4);
    memcpy(h->live.payload[3], &frozen, 4);
    s->context = h;
    s->timeline.context = h;
    s->timeline.resolve = resolve;
    s->timeline.motion.route_width = route_width;
    s->timeline.motion.route_count = route_count;
    s->timeline.motion.route_point = point;
    s->timeline.motion.event_width = event_width;
    s->timeline.motion.event_count = event_count;
    s->timeline.motion.event = event;
    s->timeline.motion.event_size = size;
    s->timeline.motion.payload = payload;
    s->timeline.put_event = put;
    s->reset = reset_timeline;
    s->clone = clone;
    s->release = release_track;
    s->apply = apply;
    s->marker = marker_emit;
    s->span = span_emit;
    s->extend = extend;
    s->fault = tl_fault;
}
static void timeline_tests(void)
{
    Timeline h;
    Track original;
    MMPreviewActor actor;
    int i, n, f, r;
    tl_setup(&h);
    original = h.live;
    actor = h.actor;
    assert(mm_preview_actor_timeline(&h.actor, 1, 5, &h.services) == 1 && !h.owners && h.released == 1);
    assert(h.marker_count == 4 && h.span_count == 2 && h.spans[0].first.low == 1 && h.spans[0].end.low == 2 &&
           h.spans[1].first.low == 2 && h.spans[1].end.low == 6 && h.spans[1].frozen);
    assert(h.markers[1].kind == MM_PREVIEW_MARK_ACTION && h.markers[1].frame == 1 &&
           h.markers[3].kind == MM_PREVIEW_MARK_FREEZE && h.markers[3].frame == 2);
    assert(!memcmp(&original, &h.live, sizeof(original)) && !memcmp(&actor, &h.actor, sizeof(actor)));
    n = h.calls;
    for (f = -2; f <= 0; ++f)
        for (i = 1; i <= n; ++i)
        {
            tl_setup(&h);
            original = h.live;
            actor = h.actor;
            h.fail_at = i;
            h.failure = f;
            r = mm_preview_actor_timeline(&h.actor, 1, 5, &h.services);
            assert(!h.owners && !memcmp(&original, &h.live, sizeof(original)) &&
                   !memcmp(&actor, &h.actor, sizeof(actor)));
            if (f < 0)
                assert(r == f);
        }
    tl_setup(&h);
    h.actor.inactive = 1;
    assert(mm_preview_actor_timeline(&h.actor, 1, 5, &h.services) == 1 && !h.calls && !h.released);
    tl_setup(&h);
    assert(mm_preview_actor_timeline(&h.actor, 1, 0, &h.services) == 1 && !h.calls);
    tl_setup(&h);
    assert(mm_preview_actor_timeline(&h.actor, 1, 100002, &h.services) == 0 && !h.calls);
    tl_setup(&h);
    assert(mm_preview_actor_timeline(&h.actor, 3, 5, &h.services) == 1 && !h.marker_count && h.span_count == 1 &&
           h.spans[0].first.low == 3);
    tl_setup(&h);
    h.actor.header.first_frame = 2147483646;
    h.live.events[0].nfrm = 2147483646;
    h.live.count = 3;
    h.live.route[0].dwr = 0;
    h.live.routes = 1;
    memset(h.live.payload[2], 0, 4);
    assert(mm_preview_actor_timeline(&h.actor, 2147483646, 2147483647, &h.services) == 1 && h.span_count == 1 &&
           h.spans[0].end.low == 0x80000000U && h.spans[0].end.high == 0);
    tl_setup(&h);
    h.live.many = 1;
    h.live.count = 100001;
    h.live.routes = 1;
    h.live.route[0].dwr = 0;
    assert(mm_preview_actor_timeline(&h.actor, 1, 1, &h.services) == 0 && h.marker_count == 100000 && !h.owners);
}
typedef struct Group
{
    MMU32 count;
    MMI32 kind[4], frame[4];
    MMU32 words[4][14];
    MMActorSound sounds[4];
    int live;
} Group;
typedef struct Presentation
{
    Group groups[3];
    MMPreviewPresentationServices services;
    MMPreviewCue cues[8];
    MMI32 transition, pause_kind, pause_duration;
    int count, captions, pauses, groups_live, calls, fail_at, failure, releases[4], released;
} Presentation;
static int pr_step(Presentation *h)
{
    return ++h->calls == h->fail_at ? h->failure : 1;
}
static void reset(void *p)
{
    Presentation *h = (Presentation *)p;
    h->count = h->captions = h->pauses = 0;
    h->transition = 0;
}
static void transition(void *p, MMI32 value)
{
    ((Presentation *)p)->transition = value;
}
static int scene_header(void *p, MMU32 scene, MMI32 *out)
{
    Presentation *h = (Presentation *)p;
    int r = pr_step(h);
    assert(scene == 9);
    if (r > 0)
        *out = 3;
    return r;
}
static int group(void *p, MMU32 number, int kind, MMPreviewCueGroup *out)
{
    Presentation *h = (Presentation *)p;
    Group *g = h->groups + kind;
    int r = pr_step(h);
    assert(number == (kind == MM_PREVIEW_CUE_ACTOR ? 10u : 9u) && !g->live);
    if (r >= 0)
    {
        g->live = 1;
        ++h->groups_live;
        out->handle = g;
        out->count = g->count;
        out->kind = kind;
    }
    return r;
}
static void group_release(void *p, MMPreviewCueGroup *g)
{
    Presentation *h = (Presentation *)p;
    Group *group = (Group *)g->handle;
    assert(group->live);
    group->live = 0;
    --h->groups_live;
    h->releases[h->released++] = g->kind;
    g->handle = 0;
}
static void scene_event(void *p, const MMPreviewCueGroup *g, MMU32 row, MMI32 *kind, MMI32 *frame)
{
    Group *data = (Group *)g->handle;
    (void)p;
    assert(row < data->count);
    *kind = data->kind[row];
    *frame = data->frame[row];
}
static MMU32 word(void *p, const MMPreviewCueGroup *g, MMU32 row, MMU32 index)
{
    (void)p;
    assert(index < 14);
    return ((Group *)g->handle)->words[row][index];
}
static int child(void *p, MMU32 scene, MMU32 slot, int actor, MMU32 *out)
{
    Presentation *h = (Presentation *)p;
    int r = pr_step(h);
    assert(scene == 9);
    if (r > 0)
        *out = actor ? 10 + slot : 12;
    return r;
}
static int actor_header(void *p, MMU32 number, MMActorHeader *out)
{
    Presentation *h = (Presentation *)p;
    int r = pr_step(h);
    assert(number == 10 || number == 11);
    if (r > 0)
    {
        memset(out, 0, sizeof(*out));
        out->first_frame = number == 11 ? 2147483647 : 1;
        out->actor_id = 42;
    }
    return r;
}
static void sound(void *p, const MMPreviewCueGroup *g, MMU32 row, MMActorSound *out)
{
    (void)p;
    *out = ((Group *)g->handle)->sounds[row];
}
static int caption(void *p, MMU32 number)
{
    Presentation *h = (Presentation *)p;
    int r = pr_step(h);
    assert(number == 12);
    if (r > 0)
        ++h->captions;
    return r;
}
static int pause_emit(void *p, MMI32 frame, MMI32 kind, MMI32 duration)
{
    Presentation *h = (Presentation *)p;
    int r = pr_step(h);
    assert(frame == 2);
    if (r > 0)
    {
        ++h->pauses;
        h->pause_kind = kind;
        h->pause_duration = duration;
    }
    return r;
}
static int cue(void *p, const MMPreviewCue *value)
{
    Presentation *h = (Presentation *)p;
    int r = pr_step(h);
    if (r > 0)
    {
        assert(h->count < 8);
        h->cues[h->count++] = *value;
    }
    return r;
}
static void pr_setup(Presentation *h)
{
    Group *g;
    MMPreviewPresentationServices *s = &h->services;
    memset(h, 0, sizeof(*h));
    g = h->groups + MM_PREVIEW_CUE_STARTS;
    g->count = 3;
    g->kind[0] = g->kind[1] = MM_SCENE_ADD_ACTOR;
    g->words[1][0] = 1;
    g->kind[2] = MM_SCENE_ADD_TEXT;
    g = h->groups + MM_PREVIEW_CUE_FRAMES;
    g->count = 4;
    g->kind[0] = MM_SCENE_PLAY_SOUND;
    g->words[0][0] = ONE;
    g->words[0][1] = 3;
    g->words[0][3] = 1;
    g->words[0][4] = 5;
    g->words[0][7] = 22;
    g->words[0][8] = 50;
    g->kind[1] = MM_SCENE_PAUSE;
    g->frame[1] = 2;
    g->words[1][0] = 1;
    g->words[1][1] = 20;
    g->kind[2] = MM_SCENE_PLAY_SOUND;
    g->frame[2] = 3;
    g->words[2][0] = 1234;
    g->words[2][1] = 2;
    g->words[2][2] = 1;
    g->words[2][3] = 2;
    g->words[2][4] = 6;
    g->words[2][5] = 2;
    g->words[2][7] = 22;
    g->words[2][8] = 51;
    g->words[2][9] = 7;
    g->words[2][10] = 3;
    g->words[2][12] = 22;
    g->words[2][13] = 52;
    g->kind[3] = MM_SCENE_CHANGE_CAMERA;
    g = h->groups + MM_PREVIEW_CUE_ACTOR;
    g->count = 2;
    g->kind[0] = g->kind[1] = MM_ACTOR_SOUND;
    g->frame[0] = 3;
    g->frame[1] = 7;
    g->sounds[0].tag.type = 22;
    g->sounds[0].tag.number = 60;
    g->sounds[0].cel = -1;
    g->sounds[0].queue = 2;
    g->sounds[0].no_sound = 2;
    g->sounds[1].tag.type = 22;
    g->sounds[1].tag.number = 61;
    g->sounds[1].cel = 3;
    g->sounds[1].no_sound = 1;
    s->context = h;
    s->reset = reset;
    s->transition = transition;
    s->header = scene_header;
    s->group = group;
    s->release = group_release;
    s->event = scene_event;
    s->word = word;
    s->child = child;
    s->actor_header = actor_header;
    s->actor_sound = sound;
    s->caption = caption;
    s->pause = pause_emit;
    s->cue = cue;
}
static void presentation_tests(void)
{
    Presentation h;
    int sheet, captions, n, i, f, r;
    for (sheet = 0; sheet <= 1; ++sheet)
        for (captions = 0; captions <= 1; ++captions)
        {
            pr_setup(&h);
            assert(mm_preview_presentation(9, sheet, captions, &h.services) == 1);
            assert(!h.groups_live && h.transition == 3 && h.pauses == 1 && h.pause_kind == 1 &&
                   h.pause_duration == 20 && h.captions == (!sheet || captions) && h.count == (sheet ? 5 : 3));
            n = h.calls;
            if (sheet)
                assert(h.cues[0].actor_sound && h.cues[0].actor == 42 && h.cues[0].queue && !h.cues[0].silent &&
                       !h.cues[0].motion && h.cues[1].motion && h.cues[1].silent && h.cues[1].motion_cel == 3 &&
                       h.releases[0] == MM_PREVIEW_CUE_ACTOR);
            assert(h.cues[h.count - 1].queue && h.cues[h.count - 1].number == 52 && h.cues[h.count - 1].source == 3);
            for (f = -2; f <= 0; ++f)
                for (i = 1; i <= n; ++i)
                {
                    pr_setup(&h);
                    h.fail_at = i;
                    h.failure = f;
                    r = mm_preview_presentation(9, sheet, captions, &h.services);
                    assert(r == f && !h.groups_live);
                }
        }
    pr_setup(&h);
    h.groups[0].words[1][0] = 4;
    assert(mm_preview_presentation(9, 0, 0, &h.services) == 0 && h.count == 1 && h.captions == 1 && !h.groups_live);
    pr_setup(&h);
    h.groups[0].words[1][1] = 0xffffffffU;
    assert(mm_preview_presentation(9, 1, 0, &h.services) == 0 && h.count == 3 && !h.groups_live);
}
typedef struct Sort
{
    MMPreviewCue cues[64];
    int calls, fail, live;
} Sort;
static void get(void *p, MMU32 i, MMPreviewCue *out)
{
    *out = ((Sort *)p)->cues[i];
}
static void store(void *p, MMU32 i, const MMPreviewCue *cue)
{
    ((Sort *)p)->cues[i] = *cue;
}
static void *resize(void *p, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Sort *s = (Sort *)p;
    void *out;
    (void)old_bytes;
    (void)priority;
    ++s->calls;
    if (s->fail && s->calls >= s->fail)
        return 0;
    out = realloc(old, bytes);
    if (out && !old)
        ++s->live;
    return out;
}
static void free_buffer(void *p, void *buffer, MMU32 bytes)
{
    Sort *s = (Sort *)p;
    (void)bytes;
    assert(s->live > 0);
    --s->live;
    free(buffer);
}
static void sort_tests(void)
{
    Sort s;
    MMPreviewCue expected[64];
    MMPreviewCueOrder order;
    MMStorageAllocator a;
    int i, f, n;
    memset(&order, 0, sizeof(order));
    a.context = &s;
    a.resize = resize;
    a.release = free_buffer;
    order.context = &s;
    order.allocator = &a;
    order.get = get;
    order.put = store;
    for (n = 0; n <= 64; ++n)
        for (f = 0; f <= 2; ++f)
        {
            order.count = (MMU32)n;
            memset(&s, 0, sizeof(s));
            s.fail = f;
            for (i = 0; i < 64; ++i)
            {
                s.cues[i].frame = (i * 37 % 13) - 6;
                s.cues[i].actor_sound = i % 2;
                s.cues[i].number = (MMU32)i;
            }
            s.cues[0].frame = (-2147483647 - 1);
            s.cues[1].frame = 2147483647;
            mm_preview_cues_sort(77, 9, &order);
            assert(!s.live);
            for (i = 0; i < n; ++i)
            {
                MMPreviewCue *c = s.cues + i;
                assert(c->scene_type == 77 && c->scene_number == 9);
                if (i)
                {
                    MMPreviewCue *p = c - 1;
                    assert(p->frame < c->frame ||
                           (p->frame == c->frame && (p->actor_sound < c->actor_sound ||
                                                     (p->actor_sound == c->actor_sound && p->number < c->number))));
                }
            }
            if (!f)
                memcpy(expected, s.cues, sizeof(expected));
            else
                assert(!memcmp(expected, s.cues, sizeof(expected)));
        }
}
int main(void)
{
    timeline_tests();
    presentation_tests();
    sort_tests();
    puts("Preview metadata: private replay, spans/budgets, cue traversal/order, partial output and exception cleanup "
         "passed.");
    return 0;
}
