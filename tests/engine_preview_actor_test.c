#include "mm/preview_actor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define ONE 65536L
typedef struct Host
{
    MMPreviewActor actor;
    MMPreviewActorServices services;
    MMChunkAccess chunks;
    MMActorHeader header;
    MMActorRoutePoint route[4];
    MMActorEvent events[8];
    unsigned char payload[8][64];
    MMI32 sizes[8], routes, count;
    int calls, fail_at, failure, fault, releases[4], released, templates, bodies;
    int prop, text, child_calls, fetches, poses, locations, clears, motions, resets, applies;
    int token[2];
    MMActorPoint point;
    MMActorMatrix matrix;
} Host;
static int step(Host *h)
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
static MMU32 route_width(void *v)
{
    (void)v;
    return 16;
}
static MMI32 route_count(void *v)
{
    return ((Host *)v)->routes;
}
static void point(void *v, MMI32 i, MMActorRoutePoint *out)
{
    Host *h = (Host *)v;
    assert(i >= 0 && i < h->routes);
    *out = h->route[i];
}
static MMU32 event_width(void *v)
{
    (void)v;
    return 20;
}
static MMI32 event_count(void *v)
{
    return ((Host *)v)->count;
}
static void event(void *v, MMI32 i, MMActorEvent *out)
{
    Host *h = (Host *)v;
    assert(i >= 0 && i < h->count);
    *out = h->events[i];
}
static MMI32 size(void *v, MMI32 i)
{
    return ((Host *)v)->sizes[i];
}
static void payload(void *v, MMI32 i, void *out)
{
    Host *h = (Host *)v;
    memcpy(out, h->payload[i], (size_t)h->sizes[i]);
}
static void put(void *v, MMI32 i, const MMActorEvent *e)
{
    ((Host *)v)->events[i] = *e;
}
static int resolve(void *v, MMI32 action, MMI32 cel, MMI32 *out)
{
    Host *h = (Host *)v;
    (void)action;
    (void)cel;
    *out = ONE;
    return step(h) > 0;
}
static int fault(void *v)
{
    return ((Host *)v)->fault;
}
static int header(void *v, MMU32 number, MMActorHeader *out)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(number == 8);
    if (r > 0)
        *out = h->header;
    return r;
}
static int table(void *v, MMU32 number, void **out)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(number == 8);
    if (r > 0)
        *out = h;
    return r;
}
static int chunk(void *v, MMU32 index, MMChunkEntry *out)
{
    (void)v;
    assert(index == 0);
    memset(out, 0, sizeof(*out));
    out->type = 0x41435452UL;
    out->number = 8;
    out->child_count = 1;
    return 1;
}
static int child(void *v, MMU32 parent, MMU32 index, MMChunkChild *out)
{
    Host *h = (Host *)v;
    assert(parent == 0 && index == 0);
    ++h->child_calls;
    out->type = 0x544d504cUL;
    out->number = 17;
    out->slot = 0;
    return 1;
}
static int fetch(void *v, const MMActorHeader *header, void **out)
{
    Host *h = (Host *)v;
    int r = step(h);
    ++h->fetches;
    assert(header->template_type == 22);
    if (r > 0)
    {
        *out = h->token;
        ++h->templates;
    }
    return r;
}
static int bind(void *v, void *t)
{
    Host *h = (Host *)v;
    assert(t == h->token);
    return step(h);
}
static int body(void *v, void *t, void **out)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(t == h->token);
    if (r > 0)
    {
        *out = h->token + 1;
        ++h->bodies;
    }
    return r;
}
static int is_prop(void *v, void *t)
{
    (void)t;
    return ((Host *)v)->prop;
}
static int is_text(void *v, void *t)
{
    (void)t;
    return ((Host *)v)->text;
}
static void release_body(void *v, void *b)
{
    Host *h = (Host *)v;
    assert(b == h->token + 1 && h->bodies == 1);
    --h->bodies;
    h->releases[h->released++] = 1;
}
static void release_template(void *v, void *t)
{
    Host *h = (Host *)v;
    assert(t == h->token && h->templates == 1);
    --h->templates;
    h->releases[h->released++] = 2;
}
static int create_track(void *v, void **r, void **e)
{
    Host *h = (Host *)v;
    int result = step(h);
    if (result > 0)
    {
        h->routes = h->count = 0;
        *r = *e = h;
    }
    return result;
}
static int insert_route(void *v, void *g, MMI32 i, const MMActorRoutePoint *p)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(g == h && i == h->routes);
    if (r > 0)
        h->route[h->routes++] = *p;
    return r;
}
static int insert_event(void *v, void *g, MMI32 i, MMU32 n, const void *p, const MMActorEvent *e)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(g == h && i == h->count && n <= 64);
    if (r > 0)
    {
        h->events[i] = *e;
        h->sizes[i] = (MMI32)n;
        memcpy(h->payload[i], p, n);
        ++h->count;
    }
    return r;
}
static void rest(void *v, void *t, unsigned short *a)
{
    (void)v;
    (void)t;
    a[0] = 0;
    a[1] = 0;
    a[2] = 0;
}
static MMI32 scalar(float v)
{
    return (MMI32)(v * 65536.f);
}
static float scalar_float(MMI32 v)
{
    return (float)v / 65536.f;
}
static void locate(void *v, void *b, const MMActorBodyPose *pose)
{
    Host *h = (Host *)v;
    assert(b == h->token + 1);
    ++h->locations;
    h->point = pose->translation;
    h->matrix = pose->orientation;
}
static int pose(void *v, void *t, void *b, MMI32 action, MMI32 cel)
{
    Host *h = (Host *)v;
    assert(t == h->token && b == h->token + 1 && action == 0 && cel >= 0);
    ++h->poses;
    return step(h);
}
static void effect(void *v, const MMActorEffectCommand *c, MMActorEffectReply *r)
{
    Host *h = (Host *)v;
    if (c->operation == MM_EFFECT_CEL_COUNT)
        r->value = 4;
    if (c->operation == MM_EFFECT_ACTION_FLAGS)
        r->flags = 0;
    if (c->operation == MM_EFFECT_SET_ACTION_CEL)
        ++h->poses;
}
static int apply(void *v, MMI32 i)
{
    Host *h = (Host *)v;
    MMActorEffectServices s;
    MMActorPoint origin;
    int r = step(h);
    ++h->applies;
    if (r <= 0)
        return r;
    memset(&s, 0, sizeof(s));
    s.context = h;
    s.run = effect;
    s.write_event = put;
    origin.dxr = (MMI32)h->actor.header.translation[0];
    origin.dyr = (MMI32)h->actor.header.translation[1];
    origin.dzr = (MMI32)h->actor.header.translation[2];
    return mm_actor_effect_apply(&h->actor.state, h, h, i, &origin, 1, 0, &h->services.timeline.motion, &s);
}
static void clear_cues(void *v)
{
    ++((Host *)v)->clears;
}
static void clear_motion(void *v)
{
    ++((Host *)v)->motions;
}
static int costume(void *v, void *t, void *b)
{
    Host *h = (Host *)v;
    (void)t;
    (void)b;
    ++h->resets;
    return step(h);
}
static int motion(void *v)
{
    return step((Host *)v);
}
static void setup(Host *h)
{
    MMPreviewActorServices *s = &h->services;
    MMActorEntrance add;
    MMI32 action[2] = {0, 0}, stride = -ONE;
    memset(h, 0, sizeof(*h));
    mm_preview_actor_init(&h->actor);
    h->chunks.context = h;
    h->chunks.count = 1;
    h->chunks.entry = chunk;
    h->chunks.child = child;
    h->prop = 1;
    h->header.first_frame = 1;
    h->header.last_frame = 5;
    h->header.template_type = 22;
    h->header.template_number = 10;
    h->header.translation[0] = 2 * ONE;
    h->routes = 2;
    h->route[0].dwr = 4 * ONE;
    h->route[1].xyz.dxr = 4 * ONE;
    h->count = 3;
    memset(&add, 0, sizeof(add));
    h->events[0].aet = MM_ACTOR_ADD;
    h->events[1].aet = MM_ACTOR_ACTION;
    h->events[2].aet = MM_ACTOR_STEP;
    h->events[0].nfrm = h->events[1].nfrm = h->events[2].nfrm = 1;
    h->sizes[0] = sizeof(add);
    h->sizes[1] = sizeof(action);
    h->sizes[2] = sizeof(stride);
    memcpy(h->payload[0], &add, sizeof(add));
    memcpy(h->payload[1], action, sizeof(action));
    memcpy(h->payload[2], &stride, sizeof(stride));
    s->context = h;
    s->timeline.context = h;
    s->timeline.resolve = resolve;
    s->timeline.put_event = put;
    s->timeline.motion.route_width = route_width;
    s->timeline.motion.route_count = route_count;
    s->timeline.motion.route_point = point;
    s->timeline.motion.event_width = event_width;
    s->timeline.motion.event_count = event_count;
    s->timeline.motion.event = event;
    s->timeline.motion.event_size = size;
    s->timeline.motion.payload = payload;
    s->fault = fault;
    s->header = header;
    s->route = table;
    s->events = table;
    s->fetch = fetch;
    s->bind = bind;
    s->body_create = body;
    s->prop = is_prop;
    s->text = is_text;
    s->release_body = release_body;
    s->release_template = release_template;
    s->track_create = create_track;
    s->route_insert = insert_route;
    s->event_insert = insert_event;
    s->rest = rest;
    s->scalar = scalar;
    s->scalar_float = scalar_float;
    s->locate = locate;
    s->pose = pose;
    s->apply = apply;
    s->clear_cues = clear_cues;
    s->clear_motion = clear_motion;
    s->default_costume = costume;
    s->motion_cues = motion;
}
static void dispose(Host *h)
{
    mm_preview_actor_dispose(&h->actor, &h->services);
    assert(!h->templates && !h->bodies);
    if (h->released == 2)
        assert(h->releases[0] == 1 && h->releases[1] == 2);
    mm_preview_actor_dispose(&h->actor, &h->services);
}
static MMPreviewActorDefinition definition(void)
{
    MMPreviewActorDefinition d;
    memset(&d, 0, sizeof(d));
    d.header.first_frame = 1;
    d.header.last_frame = 5;
    d.header.template_type = 22;
    d.header.template_number = 10;
    d.point[0] = 2;
    return d;
}
static void loading(void)
{
    Host h;
    MMPreviewActorDefinition d = definition();
    int n, i, failure;
    setup(&h);
    assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services) == 1);
    assert(h.actor.header.template_number == 17 && h.child_calls > 0 && h.actor.original[0] == 2 * ONE);
    n = h.calls;
    dispose(&h);
    for (failure = -2; failure <= 0; ++failure)
        for (i = 1; i <= n; ++i)
        {
            setup(&h);
            h.fail_at = i;
            h.failure = failure;
            assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services) == failure);
            dispose(&h);
        }
    setup(&h);
    h.header.first_frame = 2147483647;
    h.events[0].rtel.irpt = -9;
    assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services) && h.actor.inactive && !h.fetches && !h.actor.tmpl);
    dispose(&h);
    setup(&h);
    h.header.source_id = 2;
    assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services) && !h.child_calls && h.actor.header.template_number == 10);
    dispose(&h);
    setup(&h);
    h.routes = 0;
    assert(!mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services) && h.bodies == 1);
    dispose(&h);
    setup(&h);
    h.events[1].rtel.irpt = 2;
    assert(!mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services) && h.bodies == 1);
    dispose(&h);
    setup(&h);
    h.header.last_frame = 0;
    assert(!mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
    dispose(&h);
    setup(&h);
    h.header.last_frame = 100002;
    assert(!mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
    dispose(&h);
    setup(&h);
    h.header.last_frame = 100001;
    assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
    dispose(&h);
    setup(&h);
    h.header.last_frame = 2147483647;
    assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services) && h.actor.header.last_frame == 5);
    n = h.calls;
    dispose(&h);
    for (failure = -2; failure < 0; ++failure)
        for (i = 7; i <= n; ++i)
        {
            setup(&h);
            h.header.last_frame = 2147483647;
            h.fail_at = i;
            h.failure = failure;
            assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services) == failure &&
                   h.actor.header.last_frame == 2147483647);
            dispose(&h);
        }
    setup(&h);
    assert(mm_preview_actor_create(&h.actor, &d, &h.services));
    n = h.calls;
    assert(h.count == 3 && h.routes == 1 && h.events[0].aet == MM_ACTOR_ADD && h.events[1].aet == MM_ACTOR_ACTION &&
           h.events[2].aet == MM_ACTOR_STEP && h.actor.original[0] == 2 * ONE);
    dispose(&h);
    for (failure = -2; failure <= 0; ++failure)
        for (i = 1; i <= n; ++i)
        {
            setup(&h);
            h.fail_at = i;
            h.failure = failure;
            assert(mm_preview_actor_create(&h.actor, &d, &h.services) == failure);
            dispose(&h);
        }
    setup(&h);
    h.prop = 0;
    assert(mm_preview_actor_create(&h.actor, &d, &h.services) && h.templates && h.bodies && h.count == 3);
    dispose(&h);
    setup(&h);
    h.text = 1;
    assert(mm_preview_actor_create(&h.actor, &d, &h.services) && h.templates && h.bodies && h.count == 3);
    dispose(&h);
}
static void playback(void)
{
    Host h;
    MMActorState before;
    MMPreviewActorShape shape;
    float left[9], right[9];
    float delta[3] = {1, 2, 3}, bad[3] = {1, 2, 40000}, zero[3] = {0, 0, 0};
    int n, i, failure, r;
    setup(&h);
    assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
    h.calls = 0;
    assert(mm_preview_actor_seek(&h.actor, 3, &h.services) && h.actor.state.frame == 3 && h.actor.state.onStage);
    n = h.calls;
    assert(h.point.dxr == 4 * ONE && h.locations && h.actor.orientation_reset == 0);
    assert(mm_preview_actor_seek(&h.actor, 2, &h.services) && h.actor.state.frame == 2);
    assert(mm_preview_actor_seek(&h.actor, 1, &h.services) && h.actor.state.frame == 1);
    assert(mm_preview_actor_seek(&h.actor, 0, &h.services) && !h.actor.state.onStage);
    dispose(&h);
    for (failure = -2; failure <= 0; ++failure)
        for (i = 1; i <= n; ++i)
        {
            setup(&h);
            assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
            h.calls = 0;
            h.fail_at = i;
            h.failure = failure;
            r = mm_preview_actor_seek(&h.actor, 3, &h.services);
            if (failure < 0)
                assert(r == failure);
            dispose(&h);
        }
    setup(&h);
    assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
    assert(!mm_preview_actor_seek(&h.actor, 100002, &h.services) && h.actor.state.frame == 2147483647);
    h.actor.inactive = 1;
    assert(mm_preview_actor_seek(&h.actor, 100002, &h.services));
    h.actor.inactive = 0;
    before = h.actor.state;
    assert(!mm_preview_actor_translate(&h.actor, bad, &h.services) && !memcmp(&before, &h.actor.state, sizeof(before)));
    assert(mm_preview_actor_translate(&h.actor, delta, &h.services) && h.actor.header.translation[0] == 3 * ONE);
    assert(mm_preview_actor_offset(&h.actor, zero, &h.services) && h.actor.header.translation[0] == 2 * ONE);
    h.actor.original[0] = (MMI32)0x7fffffff;
    h.actor.header.translation[0] = 0x7fffffff;
    assert(mm_preview_actor_offset(&h.actor, zero, &h.services));
    h.actor.original[0] = 2 * ONE;
    h.actor.header.translation[0] = 2 * ONE;
    assert(mm_preview_actor_seek(&h.actor, 2, &h.services));
    h.actor.state.useCurrentRotation = 1;
    h.actor.orientation_reset = 2;
    assert(mm_preview_actor_position(&h.actor, 0, &h.services) && h.actor.orientation.useCurrentRotation);
    h.actor.orientation_reset = 1;
    assert(mm_preview_actor_position(&h.actor, 0, &h.services) && !h.actor.orientation.useCurrentRotation);
    mm_preview_actor_shape(&h.actor, &shape, left, right, &h.services);
    shape.scale[0] = 2;
    assert(mm_preview_actor_set_shape(&h.actor, &shape, &h.services) && h.actor.shape.scale[0] == 2);
    assert(mm_preview_actor_translate(&h.actor, delta, &h.services));
    assert(h.point.dxr == mm_fixed_add(h.actor.state.routeTranslation.dxr, h.actor.state.position.dxr));
    h.fail_at = h.calls + 1;
    h.failure = -1;
    before = h.actor.state;
    assert(mm_preview_actor_position(&h.actor, 1, &h.services) == -1 && h.actor.state.onStage == before.onStage);
    dispose(&h);
}
static void replace_events(void)
{
    Host h;
    MMActorState expected;
    int failure;
    setup(&h);
    assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
    assert(mm_preview_actor_seek(&h.actor, 2, &h.services));
    expected = h.actor.state;
    h.actor.state.action = 99;
    h.actor.state.nextEvent = h.count;
    assert(mm_preview_actor_replace_events(&h.actor, &h, 2, &h.services));
    assert(h.actor.state.frame == expected.frame && h.actor.state.action == expected.action);
    assert(h.actor.state.nextEvent == expected.nextEvent && h.actor.state.onStage == expected.onStage);
    assert(h.resets && h.clears && h.motions && h.actor.events == &h);
    dispose(&h);
    for (failure = -2; failure <= 0; ++failure)
    {
        setup(&h);
        assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
        h.fail_at = h.calls + 1; h.failure = failure;
        assert(mm_preview_actor_replace_events(&h.actor, &h, 2, &h.services) == failure);
        dispose(&h);
    }
}
int main(void)
{
    loading();
    playback();
    replace_events();
    puts("Preview actor: load/create prefixes, lifetime, resource ownership, replay, exceptions, pose and placement "
         "passed.");
    return 0;
}
