#include "mm/preview_actor.h"
#include "mm/preview_camera.h"
#include "mm/actor_persistence.h"
#include "mm/template_create.h"
#include "mm/text_resource.h"
#include "engine_resource_block_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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
    int fail_alloc, alloc_live, draft_bodies, costume_saved, costume_restored, material_overrides;
    int material_refs, material_failure;
    MMBodyOwner *action_draft;
    MMStorageAllocator allocator;
    int token[4];
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
static int effect_select_cel(void *v, MMI32 action, MMI32 cel)
{
    Host *h = (Host *)v;
    (void)action;
    (void)cel;
    ++h->poses;
    return 1;
}
static int effect_cel_count(void *v, MMI32 action, MMI32 *count)
{
    (void)v;
    (void)action;
    *count = 4;
    return 1;
}
static int effect_action_flags(void *v, MMI32 action, MMU32 *flags)
{
    (void)v;
    (void)action;
    *flags = 0;
    return 1;
}
static int effect_default_costume(void *v)
{
    (void)v;
    return 1;
}
static void put_owned(void *events, MMI32 i, const MMActorEvent *value)
{
    assert(mm_actor_events_set_fixed((MMActorEventTable *)events, i, value));
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
    s.select_cel = effect_select_cel;
    s.cel_count = effect_cel_count;
    s.action_flags = effect_action_flags;
    s.default_costume = effect_default_costume;
    s.write_event = h->services.tags ? put_owned : put;
    origin.dxr = (MMI32)h->actor.header.translation[0];
    origin.dyr = (MMI32)h->actor.header.translation[1];
    origin.dzr = (MMI32)h->actor.header.translation[2];
    return mm_actor_effect_apply(&h->actor.state, h->actor.route, h->actor.events, i, &origin,
                                 1, 0, &h->services.timeline.motion, &s);
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
static void *resize_track(void *p, void *block, MMU32 old, MMU32 bytes, int priority)
{
    Host *h = (Host *)p;
    void *result;
    int fresh = !block;
    (void)old;
    (void)priority;
    if (h->fail_alloc)
        return NULL;
    result = realloc(block, bytes);
    if (result && fresh)
        ++h->alloc_live;
    return result;
}

static void free_track(void *p, void *block, MMU32 bytes)
{
    (void)bytes;
    --((Host *)p)->alloc_live;
    free(block);
}

static void setup_owned(Host *h)
{
    static const MMTagServices tags = {0};
    setup(h);
    h->allocator.context = h;
    h->allocator.resize = resize_track;
    h->allocator.release = free_track;
    h->services.tags = &tags;
    h->services.track_allocator = &h->allocator;
    h->services.timeline.motion = mm_actor_track_motion_services();
    h->services.timeline.put_event = put_owned;
}

static void dispose(Host *h)
{
    mm_preview_actor_dispose(&h->actor, &h->services);
    assert(!h->templates && !h->bodies && !h->alloc_live);
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
    setup_owned(&h);
    assert(mm_preview_actor_create(&h.actor, &d, &h.services));
    n = h.calls;
    {
        MMActorTrack t = mm_preview_actor_track(&h.actor);
        MMActorEvent e;
        assert(mm_actor_path_count(t.route) == 1 && mm_actor_events_count(t.events) == 3);
        assert(mm_actor_events_get_fixed(t.events, 0, &e) && e.aet == MM_ACTOR_ADD);
        assert(mm_actor_events_get_fixed(t.events, 1, &e) && e.aet == MM_ACTOR_ACTION);
        assert(mm_actor_events_get_fixed(t.events, 2, &e) && e.aet == MM_ACTOR_STEP);
        assert(h.actor.original[0] == 2 * ONE);
    }
    dispose(&h);
    for (failure = -2; failure <= 0; ++failure)
        for (i = 1; i <= n; ++i)
        {
            setup_owned(&h);
            h.fail_at = i;
            h.failure = failure;
            assert(mm_preview_actor_create(&h.actor, &d, &h.services) == failure);
            dispose(&h);
        }
    setup_owned(&h);
    h.fail_alloc = 1;
    assert(!mm_preview_actor_create(&h.actor, &d, &h.services));
    assert(h.templates && h.bodies && !h.actor.route && !h.actor.events);
    dispose(&h);
    setup_owned(&h);
    h.prop = 0;
    assert(mm_preview_actor_create(&h.actor, &d, &h.services) && h.templates && h.bodies);
    dispose(&h);
    setup_owned(&h);
    h.text = 1;
    assert(mm_preview_actor_create(&h.actor, &d, &h.services) && h.templates && h.bodies);
    dispose(&h);
}
static void playback(void)
{
    Host h;
    MMActorState before;
    MMPreviewActorShape shape;
    MMPreviewOrbit orbit = {0};
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
    orbit.framed = 1;
    orbit.object = 10;
    assert(!mm_preview_actor_translate(&h.actor, bad, &orbit, 10, &h.services) && !orbit.anchor[0] &&
           !memcmp(&before, &h.actor.state, sizeof(before)));
    assert(mm_preview_actor_translate(&h.actor, delta, &orbit, 10, &h.services) &&
           h.actor.header.translation[0] == 3 * ONE &&
           orbit.anchor[0] == 1 && orbit.anchor[1] == 2 && orbit.anchor[2] == 3);
    assert(mm_preview_actor_offset(&h.actor, zero, &orbit, 10, &h.services) &&
           h.actor.header.translation[0] == 2 * ONE && !orbit.anchor[0] && !orbit.anchor[1] && !orbit.anchor[2]);
    h.actor.original[0] = (MMI32)0x7fffffff;
    h.actor.header.translation[0] = 0x7fffffff;
    assert(mm_preview_actor_offset(&h.actor, zero, NULL, 0, &h.services));
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
    assert(mm_preview_actor_set_shape(&h.actor, &shape, NULL, 0, NULL, &h.services) == MM_PREVIEW_SHAPE_OK &&
           h.actor.shape.scale[0] == 2);
    assert(mm_preview_actor_translate(&h.actor, delta, &orbit, 9, &h.services) && !orbit.anchor[0]);
    assert(h.point.dxr == mm_fixed_add(h.actor.state.routeTranslation.dxr, h.actor.state.position.dxr));
    h.fail_at = h.calls + 1;
    h.failure = -1;
    before = h.actor.state;
    assert(mm_preview_actor_position(&h.actor, 1, &h.services) == -1 && h.actor.state.onStage == before.onStage);
    dispose(&h);
}
static int fetch_material(void *context, MMI32 source, MMU32 number, void **out)
{
    Host *h = (Host *)context;
    if (source != 4 || number > 1)
        return 0;
    *out = h->token + 2 + number;
    ++h->material_refs;
    return 1;
}

static void release_material(void *context, void *material)
{
    Host *h = (Host *)context;
    assert(h->material_refs > 0 && (material == h->token + 2 || material == h->token + 3));
    --h->material_refs;
}

static int material_override(void *context, void *body, void *material)
{
    Host *h = (Host *)context;
    assert(body == h->actor.body && (material == h->token + 2 || material == h->token + 3));
    if (h->material_failure)
        return -2;
    ++h->material_overrides;
    return 1;
}

static void replace_events(void)
{
    Host h;
    MMActorState expected;
    int failure;
    setup(&h);
    assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
    h.services.fetch_material = fetch_material;
    h.services.release_material = release_material;
    h.services.apply_material = material_override;
    assert(!mm_preview_actor_set_material(&h.actor, 4, 0, &h.services));
    h.text = 1;
    assert(mm_preview_actor_set_material(&h.actor, 4, 0, &h.services));
    assert(h.actor.material_override == h.token + 2 && h.material_refs == 1);
    assert(!mm_preview_actor_set_material(&h.actor, 5, 1, &h.services));
    h.material_failure = 1;
    assert(mm_preview_actor_set_material(&h.actor, 4, 1, &h.services) == -2);
    assert(h.actor.material_override == h.token + 2 && h.material_refs == 1);
    h.material_failure = 0;
    assert(mm_preview_actor_set_material(&h.actor, 4, 1, &h.services));
    assert(h.actor.material_override == h.token + 3 && h.material_refs == 1);
    assert(!mm_preview_actor_seek(&h.actor, 100002, &h.services) && h.material_overrides == 2);
    assert(mm_preview_actor_seek(&h.actor, 2, &h.services));
    assert(h.material_overrides == 3);
    expected = h.actor.state;
    h.actor.state.action = 99;
    h.actor.state.nextEvent = h.count;
    assert(mm_preview_actor_replay_track(&h.actor, 2, &h.services));
    assert(h.material_overrides == 3);
    assert(h.actor.state.frame == expected.frame && h.actor.state.action == expected.action);
    assert(h.actor.state.nextEvent == expected.nextEvent && h.actor.state.onStage == expected.onStage);
    assert(h.resets && h.clears && h.motions && h.actor.events == &h);
    dispose(&h);
    assert(!h.material_refs);
    for (failure = -2; failure <= 0; ++failure)
    {
        setup(&h);
        assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
        h.fail_at = h.calls + 1; h.failure = failure;
        assert(mm_preview_actor_replay_track(&h.actor, 2, &h.services) == failure);
        dispose(&h);
    }
}

static int fetch_replacement(void *p, const MMActorHeader *header, void **out)
{
    Host *h = (Host *)p;
    int result = step(h);
    assert(header->template_type == MM_TMPL && header->source_id == 8 && header->template_number == 20);
    if (result > 0)
    {
        *out = h->token + 2;
        ++h->templates;
    }
    return result;
}

static int bind_replacement(void *p, void *object)
{
    Host *h = (Host *)p;
    assert(object == h->token + 2 && h->actor.tmpl == h->token);
    return step(h);
}

static int duplicate_body(void *p, void *body, void **out)
{
    Host *h = (Host *)p;
    int result = step(h);
    assert(body == h->token + 1 && h->actor.body == body);
    if (result > 0)
    {
        *out = h->token + 3;
        ++h->bodies;
    }
    return result;
}

static int adjust_body(void *p, void *object, void *body)
{
    Host *h = (Host *)p;
    assert(object == h->token + 2 && body == h->token + 3 && h->actor.body == h->token + 1);
    return step(h);
}

static void locate_replacement(void *p, void *body, const MMActorBodyPose *pose)
{
    Host *h = (Host *)p;
    assert(body == h->token + 3 && h->actor.body == body && h->actor.header.source_id == 8);
    assert(pose->translation.dxr == h->actor.state.routeTranslation.dxr + h->actor.state.position.dxr);
    step(h);
}

static int pose_replacement(void *p, void *object, void *body, MMI32 action, MMI32 cel)
{
    Host *h = (Host *)p;
    assert(object == h->token + 2 && body == h->token + 3 && action == 0 && cel >= 0);
    return step(h);
}

static void release_replacement_body(void *p, void *body)
{
    Host *h = (Host *)p;
    assert(body == h->token + 1 || body == h->token + 3);
    --h->bodies;
    h->releases[h->released++] = body == h->token + 1 ? 1 : 3;
}

static void release_replacement_template(void *p, void *object)
{
    Host *h = (Host *)p;
    assert(object == h->token || object == h->token + 2);
    --h->templates;
    h->releases[h->released++] = object == h->token ? 2 : 4;
}

static void replace_template(void)
{
    Host h;
    MMPreviewActor before;
    int fail_at, result;
    for (fail_at = 0; fail_at <= 7; ++fail_at)
    {
        setup(&h);
        h.text = 1;
        assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
        assert(mm_preview_actor_seek(&h.actor, 1, &h.services) && h.actor.state.onStage);
        h.services.fetch = fetch_replacement;
        h.services.bind = bind_replacement;
        h.services.duplicate_body = duplicate_body;
        h.services.adjust_text_body = adjust_body;
        h.services.locate = locate_replacement;
        h.services.pose = pose_replacement;
        h.services.release_body = release_replacement_body;
        h.services.release_template = release_replacement_template;
        h.calls = 0;
        h.failure = fail_at == 7 ? 0 : -2;
        h.fail_at = fail_at == 7 ? 6 : fail_at;
        before = h.actor;
        assert(mm_preview_actor_replace_template(&h.actor, before.header.source_id,
                                                before.header.template_number, &h.services) == 1 && !h.calls);
        result = mm_preview_actor_replace_template(&h.actor, 8, 20, &h.services);
        assert(h.templates == 1 && h.bodies == 1);
        if (fail_at)
        {
            assert(result == h.failure && h.calls == h.fail_at);
            assert(!memcmp(&h.actor, &before, sizeof(before)));
            if (fail_at > 1)
                assert(h.releases[h.released - 1] == 4);
        }
        else
        {
            assert(result == 1 && h.calls == 6 && h.actor.body == h.token + 3 && h.actor.tmpl == h.token + 2);
            assert(h.actor.header.source_id == 8 && h.actor.header.template_number == 20);
            assert(h.released == 2 && h.releases[0] == 1 && h.releases[1] == 2);
        }
        mm_preview_actor_dispose(&h.actor, &h.services);
        assert(!h.templates && !h.bodies);
    }
}

static int draft_body(void *p, void *body, void **out)
{
    Host *h = (Host *)p;
    int result = step(h);
    assert(body == h->token + 1);
    if (result > 0)
    {
        ++h->draft_bodies;
        *out = h->token + 3;
    }
    return result;
}

static void release_draft_body(void *p, void *body)
{
    Host *h = (Host *)p;
    if (body == h->token + 3)
    {
        assert(h->draft_bodies == 1);
        --h->draft_bodies;
    }
    else
        release_body(p, body);
}

static void track_draft(void)
{
    Host h;
    MMPreviewActorDefinition d = definition();
    MMPreviewActor original;
    MMPreviewActorDraft draft = {0};
    MMActorSounds motion;
    MMActorEvent event = {0};
    MMActorSound sound = {0};
    MMWide clock = {(MMU32)-1, 1};
    int blocks;
    setup_owned(&h);
    assert(mm_preview_actor_create(&h.actor, &d, &h.services));
    h.services.duplicate_body = draft_body;
    h.services.release_body = release_draft_body;
    assert(mm_actor_sounds_init(&motion, NULL));
    assert(mm_actor_sounds_insert(&motion, &event, &sound));
    h.actor.draw_clock = &clock;
    mm_preview_actor_shown(&h.actor);
    assert(h.actor.draw_order.low == 0 && h.actor.draw_order.high == 2);
    original = h.actor;
    blocks = h.alloc_live;

    h.fail_alloc = 1;
    assert(!mm_preview_actor_begin_draft(&h.actor, &motion, &draft, &h.services));
    assert(!draft.active && !h.draft_bodies && h.alloc_live == blocks);
    h.fail_alloc = 0;
    h.fail_at = h.calls + 1;
    h.failure = -2;
    assert(mm_preview_actor_begin_draft(&h.actor, &motion, &draft, &h.services) == -2);
    assert(!draft.active && !h.draft_bodies && h.alloc_live == blocks);
    assert(!memcmp(&h.actor, &original, sizeof(original)) && motion.count == 1);

    assert(mm_preview_actor_begin_draft(&h.actor, &motion, &draft, &h.services) == 1);
    assert(draft.active && h.draft_bodies == 1 && h.actor.body != original.body);
    assert(h.actor.route != original.route && h.actor.events != original.events);
    assert(mm_actor_events_delete(mm_preview_actor_track(&h.actor).events, 1));
    h.actor.state.frame = 33;
    mm_preview_actor_shown(&h.actor);
    assert(h.actor.draw_order.low == 1 && clock.low == 1);
    mm_actor_sounds_clear(&motion);
    mm_preview_actor_end_draft(&h.actor, &motion, &draft, &h.services);
    assert(!draft.active && !h.draft_bodies && h.alloc_live == blocks);
    assert(!memcmp(&h.actor, &original, sizeof(original)) && motion.count == 1);
    assert(clock.low == 0 && clock.high == 2);
    assert(mm_actor_events_count(mm_preview_actor_track(&h.actor).events) == 3);
    mm_actor_sounds_dispose(&motion);
    dispose(&h);
}

typedef struct TrackSource
{
    Host *host;
    MMResourceCache cache;
    ResourceTestBlock block;
    unsigned char bytes[512];
    MMU32 length;
    int missing, fail_read, packed, fail_decode, refs, decodes;
} TrackSource;

static int resolve_track_source(void *p, MMI32 source, MMU32 type, MMU32 number, MMResourceCache **out)
{
    TrackSource *track = p;
    int result = step(track->host);
    assert(source == 9 && type == MM_ACTOR_EVENTS_CHUNK && number == 0);
    if (result > 0)
        *out = &track->cache;
    return result;
}

static int locate_track_source(void *p, MMU32 type, MMU32 number, MMBlockFile *file, int *packed)
{
    TrackSource *track = p;
    assert(type == MM_ACTOR_EVENTS_CHUNK && number == 0);
    if (track->missing)
        return 0;
    file->file = track;
    file->offset = 0;
    file->length = track->length;
    *packed = track->packed;
    return 1;
}

static void retain_track_file(void *p, void *file)
{
    TrackSource *track = p;
    assert(track == file && !track->refs);
    ++track->refs;
}

static void release_track_file(void *p, void *file)
{
    TrackSource *track = p;
    assert(track == file && track->refs == 1);
    --track->refs;
}

static int read_track_source(void *p, void *file, MMU32 offset, void *out, size_t bytes)
{
    TrackSource *track = p;
    assert(track == file && track->refs == 1 && offset <= track->length && bytes <= track->length - offset);
    if (track->fail_read)
        return 0;
    memcpy(out, track->bytes + offset, bytes);
    return 1;
}

static int write_track_fixture(void *p, MMU32 offset, const void *data, size_t bytes)
{
    TrackSource *track = p;
    assert(offset <= track->length && bytes <= track->length - offset);
    memcpy(track->bytes + offset, data, bytes);
    return 1;
}

static int reject_track_write(void *p, void *file, MMU32 offset, const void *data, size_t bytes)
{
    (void)p; (void)file; (void)offset; (void)data; (void)bytes;
    return 0;
}

static int decode_track_source(void *p, void **handle)
{
    TrackSource *track = p;
    assert(track->packed && *handle);
    ++track->decodes;
    return !track->fail_decode;
}

static void setup_track_source(TrackSource *source, Host *h)
{
    MMActorEventFactory factory = {NULL, 0};
    MMActorEventServices events = mm_actor_events_services(&factory);
    MMActorEventTable *wire = mm_actor_write_events(mm_preview_actor_track(&h->actor).events, &events);
    MMStorageWriter writer;
    assert(wire);
    memset(source, 0, sizeof(*source));
    source->host = h;
    source->length = mm_actor_events_wire_size(wire);
    assert(source->length <= sizeof(source->bytes));
    writer.context = source;
    writer.length = source->length;
    writer.write_at = write_track_fixture;
    assert(mm_actor_events_write(wire, 0, 0, &writer));
    mm_actor_events_release(wire);
    resource_block_init(&source->block, source, read_track_source, reject_track_write);
    source->block.files.retain = retain_track_file;
    source->block.files.release = release_track_file;
    source->cache.file = source;
    source->cache.services.handles = source->block.owner.handles;
    source->cache.services.block_files = &source->block.files;
    source->cache.services.locate = locate_track_source;
}

static int save_track_costume(void *p)
{
    Host *h = ((TrackSource *)p)->host;
    ++h->costume_saved;
    return step(h);
}

static void restore_track_costume(void *p) { ++((TrackSource *)p)->host->costume_restored; }

static void replace_owned_track(void)
{
    Host h;
    TrackSource source;
    MMPreviewActorDefinition d = definition();
    MMPreviewActor original;
    MMPreviewEventReplacement edit = {
        NULL, resolve_track_source, NULL, decode_track_source, save_track_costume, restore_track_costume
    };
    int mode, blocks, expected;
    edit.context = edit.decode_context = &source;
    for (mode = 0; mode <= 8; ++mode)
    {
        setup_owned(&h);
        assert(mm_preview_actor_create(&h.actor, &d, &h.services));
        assert(mm_preview_actor_seek(&h.actor, 1, &h.services));
        setup_track_source(&source, &h);
        original = h.actor;
        blocks = h.alloc_live;
        h.fail_at = mode >= 1 && mode <= 3 ? h.calls + mode : 0;
        h.failure = -2;
        source.missing = mode == 4;
        source.fail_read = mode == 5;
        if (mode == 6)
            memset(source.bytes, 0, source.length);
        source.packed = mode == 7 || mode == 8;
        source.fail_decode = mode == 8;
        expected = mode == 0 || mode == 7 ? 1 : h.fail_at ? -2 : 0;
        assert(mm_preview_actor_replace_track(&h.actor, 0, &edit, &h.services));
        assert(mm_preview_actor_replace_track(&h.actor, 9, &edit, &h.services) == expected);
        assert(!source.refs && source.decodes == source.packed);
        if (expected <= 0)
        {
            assert(!memcmp(&h.actor, &original, sizeof(original)) && h.alloc_live == blocks);
            assert(h.costume_saved == (mode == 2 || mode == 3) && h.costume_restored == (mode == 3));
        }
        else
        {
            assert(h.actor.event_source == 9 && h.actor.state.frame == original.state.frame);
            assert(h.actor.route != original.route && h.actor.events != original.events);
            assert(h.costume_saved == 1 && !h.costume_restored && h.alloc_live == blocks);
        }
        dispose(&h);
    }
}

static void locate_action_preview(void *p, void *body, const MMActorBodyPose *pose)
{
    Host *h = (Host *)p;
    assert(body == h->action_draft && h->actor.body == body);
    assert(!pose->translation.dxr && !pose->translation.dyr && !pose->translation.dzr);
}

static int pose_action_preview(void *p, void *tmpl, void *body, MMI32 action, MMI32 cel)
{
    Host *h = (Host *)p;
    assert(tmpl == h->token && body == h->action_draft && action == 0 && cel >= 0);
    h->token[3] = cel;
    return step(h);
}

static MMBodyState *action_body_state(void *p, void *body)
{
    (void)p;
    return mm_body_owner_state((MMBodyOwner *)body);
}

static void action_body_free(void *p, void **handle)
{
    (void)p;
    assert(!*handle);
}

static int duplicate_action_body(void *p, void *body, void **out)
{
    Host *h = (Host *)p;
    int result = step(h);
    assert(body == h->actor.body && body != h->action_draft);
    if (result > 0)
    {
        mm_body_owner_retain(h->action_draft);
        *out = h->action_draft;
        ++h->draft_bodies;
    }
    return result;
}

static void release_action_body(void *p, void *body)
{
    Host *h = (Host *)p;
    assert(body == h->action_draft && h->actor.body != body && h->draft_bodies == 1);
    mm_body_owner_release((MMBodyOwner *)body);
    --h->draft_bodies;
}

static void *action_geometry_at(void *p, void *actors, MMI32 index)
{
    assert(!actors && index == 1);
    return p;
}

static int action_geometry_type(void *p, void *actor)
{
    assert(p == actor);
    return ((Host *)p)->point.dxr;
}

static void action_geometry_set_type(void *p, void *actor, int type)
{
    assert(p == actor);
    ((Host *)p)->point.dxr = type;
}

static void action_preview_bounds(void *p, void *root, MMBodyBounds *bounds)
{
    Host *h = (Host *)p;
    int i;
    assert(!root && !h->point.dxr);
    for (i = 0; i < 3; ++i)
        bounds->min[i] = bounds->max[i] = h->token[3];
}

static int action_preview_render(void *p, const float corners[24], int crop)
{
    Host *h = (Host *)p;
    assert(!crop && h->actor.body == h->action_draft && h->token[3] == 7 && h->point.dxr == 1);
    assert(corners[0] == 0 && corners[21] == scalar_float(2147483646L));
    return !h->token[2];
}

static void action_preview(void)
{
    Host h;
    MMBodyCreateServices create = {0};
    MMBodyGeometryServices geometry = {0};
    MMBodyPartBinding binding = {0};
    MMPreviewIsolatedView view = {NULL, NULL, NULL, action_preview_render};
    MMPreviewActor before;
    MMBodyOwner *original;
    float clip[24];
    int mode, valid, calls;
    create.state = action_body_state;
    create.free_memory = action_body_free;
    geometry.context = &h;
    geometry.actor_at = action_geometry_at;
    geometry.actor_type = action_geometry_type;
    geometry.set_actor_type = action_geometry_set_type;
    geometry.local_bounds = action_preview_bounds;
    mm_matrix_identity(&binding.matrix);
    view.geometry = &geometry;
    view.context = &h;
    for (mode = 0; mode < 3; ++mode)
    {
        setup(&h);
        assert(mm_preview_actor_load(&h.actor, 8, &h.chunks, 0, &h.services));
        original = mm_body_owner_create(NULL, &create);
        h.action_draft = mm_body_owner_create(NULL, &create);
        assert(original && h.action_draft);
        mm_body_owner_state(original)->shape.hidden = 1;
        mm_body_owner_state(h.action_draft)->shape.hidden = 1;
        mm_body_owner_state(h.action_draft)->shape.bindings = &binding;
        h.actor.body = original;
        h.actor.state.onStage = 1;
        h.point.dxr = 1;
        h.services.duplicate_body = duplicate_action_body;
        h.services.release_body = release_action_body;
        h.services.locate = locate_action_preview;
        h.services.pose = pose_action_preview;
        h.fail_at = mode == 1 ? h.calls + 3 : 0;
        h.token[2] = mode == 2;
        before = h.actor;
        valid = 0;
        assert(mm_preview_actor_action_render(&h.actor, 0, 7, 2147483647L, clip, &valid, &h.services, &view) ==
               (mode == 0 ? MM_PREVIEW_ACTION_OK : mode == 1 ? MM_PREVIEW_ACTION_FRAME : MM_PREVIEW_ACTION_FAILED));
        assert(!memcmp(&h.actor, &before, sizeof(before)) && !h.draft_bodies && valid == (mode != 1));
        if (mode == 0)
        {
            calls = h.calls;
            assert(mm_preview_actor_action_render(&h.actor, 0, 7, 2147483647L, clip, &valid, &h.services, &view) ==
                   MM_PREVIEW_ACTION_OK);
            assert(h.calls == calls + 2 && !h.draft_bodies);
        }
        assert(mm_body_owner_references(h.action_draft) == 1 && h.point.dxr == 1);
        mm_body_owner_release(h.action_draft);
        mm_body_owner_release(original);
        h.actor.body = h.token + 1;
        h.services.release_body = release_body;
        mm_preview_actor_dispose(&h.actor, &h.services);
    }
}

static int catalogue_cels(MMTemplateOwner *owner, MMI32 action, void *count)
{
    assert(owner && (action == 0 || action == 1));
    *(MMI32 *)count = action ? -1 : 7;
    return 1;
}

static void catalogue_views(void)
{
    MMPreviewActor actor;
    MMPreviewActorInfo info;
    MMPreviewActorText text;
    const MMTemplateOwnerHost host = {NULL, 1, NULL};
    MMTemplateOwner *owner = mm_template_owner_create(&host, 1);
    MMStringValue name;
    MMTemplateState *state;
    MMTextDescriptor *descriptor;
    assert(owner);
    mm_preview_actor_init(&actor);
    assert(!mm_preview_actor_action_count(&actor) && !mm_preview_actor_text(&actor, &text));
    actor.number = 81;
    actor.header.template_type = MM_TDT;
    actor.header.template_number = 9;
    actor.header.actor_id = 5;
    actor.header.source_id = 6;
    actor.header.first_frame = 3;
    actor.header.last_frame = 12;
    actor.state.onStage = 1;
    actor.state.action = 1;
    actor.state.cel = 4;
    actor.state.routeTranslation.dxr = 2147483647L;
    actor.state.position.dxr = 1;
    actor.tmpl = owner;
    state = mm_template_owner_state(owner);
    state->flags = MM_TEMPLATE_PROP | MM_TEMPLATE_TEXT;
    state->action_count = 2;
    descriptor = mm_template_owner_descriptor(owner);
    descriptor->shape = 4;
    descriptor->font.sid = 6;
    descriptor->font.number = 22;
    name = mm_template_owner_name(owner);
    mm_string_value_set(&name, "Set", 3);
    mm_preview_actor_info(&actor, &info);
    assert(info.number == 81 && info.template_type == MM_TDT && info.template_number == 9 &&
           info.actor == 5 && info.source == 6 && info.first == 3 && info.last == 12 &&
           info.has_lifetime && info.visible && info.prop && info.text && info.action == 1 && info.cel == 4);
    assert(info.position.dxr == (-2147483647L - 1) && !info.position.dyr && !info.position.dzr);
    actor.inactive = 1;
    mm_preview_actor_info(&actor, &info);
    assert(!info.has_lifetime && !info.visible && !info.first && !info.last);
    assert(mm_preview_actor_text(&actor, &text) && text.name.buffer == name.buffer &&
           text.shape == 4 && text.font_source == 6 && text.font_number == 22);
    assert(mm_preview_actor_action_count(&actor) == 2 && mm_preview_actor_action_cels(&actor, 0, catalogue_cels) == 7);
    assert(!mm_preview_actor_action_cels(&actor, -1, catalogue_cels) &&
           !mm_preview_actor_action_cels(&actor, 1, catalogue_cels) &&
           !mm_preview_actor_action_cels(&actor, 2, catalogue_cels));
    mm_template_owner_release(owner);
}

int main(void)
{
    catalogue_views();
    loading();
    playback();
    replace_events();
    replace_template();
    track_draft();
    replace_owned_track();
    action_preview();
    puts("Preview actor: load/create prefixes, lifetime, resource ownership, replay, exceptions, pose and placement "
         "passed.");
    return 0;
}
