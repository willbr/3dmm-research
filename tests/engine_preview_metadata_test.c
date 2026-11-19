#include "mm/preview_metadata.h"
#include "mm/movie_session.h"
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
#define ACTR 0x41435452UL
#define GGAE 0x47474145UL
#define GGFR 0x47474652UL
#define GGST 0x47475354UL
#define SCEN 0x5343454eUL
#define TBOX 0x54424f58UL

typedef struct PresentationChunk
{
    MMU32 size;
    unsigned char bytes[192];
} PresentationChunk;

typedef struct Presentation
{
    PresentationChunk chunks[7];
    unsigned char *pause_data, *second_actor_event;
    MMMovieSessionGraph graph;
    MMPreviewPresentationServices services;
    MMPreviewCue cues[8];
    MMI32 transition, pause_kind, pause_duration;
    int big, count, captions, pauses, calls, fail_at, failure, expected, missing_actor;
} Presentation;

enum { ACTOR, IDLE_ACTOR, ACTOR_EVENTS, FRAMES, STARTS, SCENE, CAPTION, CHUNKS };
static const MMU32 chunk_types[] = {ACTR, ACTR, GGAE, GGFR, GGST, SCEN, TBOX};
static const MMU32 chunk_numbers[] = {10, 11, 20, 21, 22, 9, 12};

static int pr_step(Presentation *h)
{
    if (++h->calls != h->fail_at)
        return 1;
    h->expected = h->failure;
    return h->failure;
}
static int graph_step(Presentation *h)
{
    int result = pr_step(h);
    if (result > 0)
        return MM_MOVIE_OK;
    if (result == -1)
        return MM_MOVIE_MEMORY;
    h->expected = 0;
    return MM_MOVIE_IO;
}
static int pr_chunk(void *p, MMU32 i, MMChunkEntry *entry)
{
    Presentation *h = (Presentation *)p;
    assert(i < CHUNKS);
    memset(entry, 0, sizeof(*entry));
    entry->type = chunk_types[i];
    entry->number = chunk_numbers[i];
    entry->bytes = h->chunks[i].size;
    entry->child_count = i == SCENE ? 5 : i == ACTOR ? 1 : 0;
    return 1;
}
static int pr_child(void *p, MMU32 parent, MMU32 slot, MMChunkChild *out)
{
    const MMChunkChild scene_children[] = {{ACTR, 10, 0}, {GGFR, 21, 0}, {TBOX, 12, 0},
                                           {ACTR, 11, 1}, {GGST, 22, 1}};
    Presentation *h = (Presentation *)p;
    if (parent == ACTOR)
    {
        const MMChunkChild events = {GGAE, 20, 0};
        assert(slot == 0);
        *out = events;
    }
    else
    {
        assert(parent == SCENE && slot < 5);
        *out = scene_children[slot];
        if (h->missing_actor && slot == 0)
            out->type = 0;
    }
    return 1;
}
static void graph_access(void *p, MMChunkAccess *out)
{
    MMChunkAccess access = {NULL, CHUNKS, pr_chunk, pr_child};
    access.context = p;
    *out = access;
}
static int graph_read(void *p, void *request, MMU32 position, const MMChunkEntry *entry, void *bytes, MMU32 size)
{
    Presentation *h = (Presentation *)p;
    int status = graph_step(h);
    assert(!request && position == SCENE && entry->type == SCEN && size == h->chunks[SCENE].size);
    if (status == MM_MOVIE_OK)
        memcpy(bytes, h->chunks[SCENE].bytes, size);
    return status;
}
static int graph_payload(void *p, const MMChunkEntry *entry, MMMovieReceiveBytes receive, void *out)
{
    Presentation *h = (Presentation *)p;
    MMMemoryStream stream;
    MMU32 i;
    int status = graph_step(h);
    if (status != MM_MOVIE_OK)
        return status;
    for (i = 0; i < CHUNKS; ++i)
        if (entry->type == chunk_types[i] && entry->number == chunk_numbers[i])
        {
            assert(mm_memory_stream_init(&stream, h->chunks[i].bytes, h->chunks[i].size));
            return receive(out, &stream.stream);
        }
    return MM_MOVIE_BAD_FILE;
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

static void word(unsigned char *out, MMU32 value, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        out[big ? 3 - i : i] = (unsigned char)(value >> (i * 8));
}
typedef struct WireEvent
{
    MMI32 kind, frame;
    MMU32 words, data[14];
} WireEvent;

static void write_group(PresentationChunk *chunk, MMU32 width, int big, MMU32 count, const WireEvent *events)
{
    MMCollectionHeader header = {0};
    MMU32 i, j, at = 0, row_size;
    unsigned char *rows = chunk->bytes + 20, *locations;
    header.kind = MM_COLLECTION_GROUP;
    header.big_endian = big;
    header.system = 0x0303;
    header.width = width;
    header.count = count;
    header.free_count = -1;
    for (i = 0; i < count; ++i)
        header.bytes += width + events[i].words * 4;
    locations = rows + header.bytes;
    chunk->size = 20 + header.bytes + count * 8;
    assert(chunk->size <= sizeof(chunk->bytes));
    assert(mm_collection_header_write(&header, chunk->bytes, sizeof(chunk->bytes)) == MM_STORAGE_OK);
    for (i = 0; i < count; ++i)
    {
        word(rows + at, (MMU32)(width == 8 ? events[i].frame : events[i].kind), big);
        word(rows + at + 4, (MMU32)(width == 8 ? events[i].kind : events[i].frame), big);
        for (j = 0; j < events[i].words; ++j)
            word(rows + at + width + 4 * j, events[i].data[j], big);
        row_size = width + events[i].words * 4;
        word(locations + 8 * i, at, big);
        word(locations + 8 * i + 4, row_size, big);
        at += row_size;
    }
}
static void pr_setup(Presentation *h, int big)
{
    const WireEvent starts[] = {{MM_SCENE_ADD_ACTOR, 1, 1, {0}}, {MM_SCENE_ADD_ACTOR, 1, 1, {1}},
                                 {MM_SCENE_ADD_TEXT, 1, 1, {0}}};
    const WireEvent frames[] = {{MM_SCENE_PLAY_SOUND, 1, 9, {ONE, 3, 0, 1, 5, 0, 0, 22, 50}},
                                 {MM_SCENE_PAUSE, 2, 2, {1, 20}},
                                 {MM_SCENE_PLAY_SOUND, 3, 14, {1234, 2, 1, 2, 6, 2, 0, 22, 51, 7, 3, 0, 22, 52}},
                                 {MM_SCENE_CHANGE_CAMERA, 4, 1, {0}}};
    const WireEvent actor_events[] = {{MM_ACTOR_SOUND, 3, 11, {0, 2, 0, 0xffffffffU, 0, 2, 1, 3, 0, 22, 60}},
                                      {MM_ACTOR_SOUND, 7, 11, {0, 0, 0, 3, 0, 1, 2, 4, 0, 22, 61}}};
    const MMMovieSessionGraph graph = {NULL, graph_access, graph_read, graph_payload, NULL};
    const MMPreviewPresentationServices services = {NULL, reset, transition, caption, pause_emit, cue};
    MMSceneState scene;
    unsigned char *header;
    memset(h, 0, sizeof(*h));
    h->big = big;
    h->graph = graph;
    h->graph.context = h;
    h->services = services;
    h->services.context = h;
    assert(mm_scene_init(&scene, 1));
    scene.firstFrame = 1;
    scene.lastFrame = 7;
    scene.transition = 3;
    assert(mm_scene_encode_header(&scene, 0x0303, big, h->chunks[SCENE].bytes));
    h->chunks[SCENE].size = 16;
    header = h->chunks[ACTOR].bytes;
    header[big ? 1 : 0] = 1;
    word(header + 16, 42, big);
    word(header + 20, 1, big);
    h->chunks[ACTOR].size = 44;
    header = h->chunks[IDLE_ACTOR].bytes;
    header[big ? 1 : 0] = 1;
    word(header + 20, 2147483647UL, big);
    h->chunks[IDLE_ACTOR].size = 40;
    write_group(h->chunks + STARTS, 8, big, 3, starts);
    write_group(h->chunks + FRAMES, 8, big, 4, frames);
    write_group(h->chunks + ACTOR_EVENTS, 20, big, 2, actor_events);
    h->pause_data = h->chunks[FRAMES].bytes + 20 + 44 + 8;
    h->second_actor_event = h->chunks[ACTOR_EVENTS].bytes + 20 + 64;
}

static int presentation(Presentation *h, int sheet, int captions)
{
    return mm_preview_presentation(&h->graph, 9, sheet, captions, &h->services);
}
static void presentation_tests(void)
{
    Presentation h;
    int sheet, captions, n, i, f, r, big;
    for (big = 0; big <= 1; ++big)
        for (sheet = 0; sheet <= 1; ++sheet)
            for (captions = 0; captions <= 1; ++captions)
            {
                pr_setup(&h, big);
                assert(presentation(&h, sheet, captions) == 1);
                assert(h.transition == 3 && h.pauses == 1 && h.pause_kind == 1 &&
                       h.pause_duration == 20 && h.captions == (!sheet || captions) && h.count == (sheet ? 5 : 3));
                n = h.calls;
                if (sheet)
                    assert(h.cues[0].actor_sound && h.cues[0].actor == 42 && h.cues[0].queue && !h.cues[0].silent &&
                           !h.cues[0].motion && h.cues[0].frame == 3 && h.cues[0].source == 3 &&
                           h.cues[1].motion && h.cues[1].silent && h.cues[1].motion_cel == 3);
                assert(h.cues[h.count - 1].queue && h.cues[h.count - 1].number == 52 && h.cues[h.count - 1].source == 3);
                for (f = -2; f <= 0; ++f)
                    for (i = 1; i <= n; ++i)
                    {
                        pr_setup(&h, big);
                        h.fail_at = i;
                        h.failure = f;
                        r = presentation(&h, sheet, captions);
                        assert(h.calls == i && r == h.expected);
                    }
            }
    pr_setup(&h, 0);
    word(h.pause_data, 4, h.big);
    assert(presentation(&h, 0, 0) == 0 && h.count == 1 && h.captions == 1);
    pr_setup(&h, 1);
    word(h.pause_data + 4, 0xffffffffU, h.big);
    assert(presentation(&h, 1, 0) == 0 && h.count == 3);
    pr_setup(&h, 1);
    word(h.second_actor_event, 99, h.big);
    assert(presentation(&h, 1, 0) == 0 && h.count == 0);
    assert(presentation(&h, 0, 0) == 1 && h.count == 3);
    pr_setup(&h, 0);
    --h.chunks[FRAMES].size;
    assert(presentation(&h, 1, 1) == 0 && h.count == 2 && h.captions == 1);
    pr_setup(&h, 1);
    h.missing_actor = 1;
    assert(presentation(&h, 1, 0) == 0 && h.count == 0);
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
