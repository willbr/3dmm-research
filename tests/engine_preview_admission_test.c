#include "mm/preview_admission.h"
#include "mm/scene_data.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Host
{
    MMPreviewLive live;
    MMPreviewAdmission published;
    int calls, fail_at, failure, clears, groups, owned, releases, stats;
    int body_missing, empty_actions, bad_cels, unhealthy, missing_child, group_result, header_result;
    int offstage, names[4], appended, rotations, poses, saved, syncs, seeks, backgrounds;
    MMU32 loaded[4], background_number;
    int token[8], allocations, allocation_fail;
    unsigned char event_data[16];
} Host;
static int step(Host *h)
{
    ++h->calls;
    return h->fail_at == h->calls ? h->failure : 1;
}
static void *resize(void *v, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Host *h = (Host *)v;
    (void)old_bytes;
    (void)priority;
    ++h->allocations;
    if (h->allocation_fail && h->allocations >= h->allocation_fail)
        return 0;
    return realloc(old, bytes);
}
static void release_storage(void *v, void *p, MMU32 bytes)
{
    (void)v;
    (void)bytes;
    free(p);
}
static void actor_release(void *v, void *a)
{
    Host *h = (Host *)v;
    assert(a && h->owned > 0);
    --h->owned;
    ++h->releases;
}
static void clear_asset(void *v, int preserve)
{
    Host *h = (Host *)v;
    MMPreviewLiveServices s;
    assert(preserve == 0 || preserve == 1);
    memset(&s, 0, sizeof(s));
    s.context = h;
    s.actor_release = actor_release;
    mm_preview_live_clear(&h->live, &s);
    memset(&h->published, 0, sizeof(h->published));
    ++h->clears;
}
static void publish(void *v, const MMPreviewAdmission *p)
{
    ((Host *)v)->published = *p;
}
static void begin(void *v)
{
    (void)v;
}
static int healthy(void *v)
{
    return !((Host *)v)->unhealthy;
}
static int fetch(void *v, MMU32 type, MMU32 number, void **out)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(type == 77 && number == 9);
    if (r > 0)
        *out = h->token;
    return r;
}
static int bind_template(void *v, void *t)
{
    Host *h = (Host *)v;
    assert(t == h->token);
    return step(h);
}
static int body_create(void *v, void *t, void **out)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(t == h->token);
    if (r > 0)
        *out = h->body_missing ? 0 : h->token + 1;
    return r;
}
static int prop(void *v, void *t)
{
    (void)v;
    (void)t;
    return 1;
}
static MMI32 actions(void *v, void *t)
{
    (void)t;
    return ((Host *)v)->empty_actions ? 0 : 4;
}
static int cels(void *v, void *t, MMU32 i, MMI32 *out)
{
    Host *h = (Host *)v;
    (void)t;
    *out = i == 2 ? 0 : (MMI32)i + 2;
    if ((int)i == h->bad_cels)
        return 0;
    return step(h);
}
static int child(void *v, MMU32 type, MMU32 number, MMU32 slot, int actor, MMPreviewChunk *out)
{
    Host *h = (Host *)v;
    (void)type;
    assert(number == 9);
    if (actor && h->missing_child)
        return 0;
    if (!actor && slot == 1)
        return 0;
    memset(out, 0, sizeof(*out));
    out->type = 22;
    out->number = actor ? slot + 90 : 50;
    return 1;
}
static MMU32 chunks(void *v)
{
    (void)v;
    return 4;
}
static void chunk(void *v, MMU32 i, MMPreviewChunk *out)
{
    (void)v;
    memset(out, 0, sizeof(*out));
    out->type = 22;
    out->number = i ? 50 : 51;
    out->named = i != 1;
}
static int action_add(void *v, MMU32 i, MMU32 n, MMI32 name)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(n == i + 2);
    if (r > 0)
    {
        h->names[i] = name;
        ++h->appended;
    }
    return r;
}
static MMU32 action_cels(void *v, MMU32 i)
{
    (void)v;
    return i + 2;
}
static int pose(void *v, void *t, void *b, MMU32 action, MMU32 cel)
{
    Host *h = (Host *)v;
    assert(t == h->token && b == h->token + 1);
    assert(action < 2 && cel < action + 2);
    ++h->poses;
    return step(h);
}
static void rest(void *v, void *t, unsigned short *a)
{
    (void)v;
    (void)t;
    a[0] = 11;
    a[1] = 12;
    a[2] = 13;
}
static void rotate(void *v, MMActorMatrix *m, int axis, unsigned short a, int post)
{
    Host *h = (Host *)v;
    assert(axis == h->rotations++ && a == axis + 11 && post == (axis != 0));
    if (!axis)
        memset(m, 0, sizeof(*m));
    m->m[axis][axis] = a;
}
static void locate(void *v, void *b, const MMActorMatrix *m)
{
    Host *h = (Host *)v;
    assert(b == h->token + 1 && m->m[0][0] == 11 && m->m[1][1] == 12 && m->m[2][2] == 13);
}
static int background(void *v, const MMPreviewAdmissionTag *t, int virtual_scene)
{
    Host *h = (Host *)v;
    (void)virtual_scene;
    ++h->backgrounds;
    h->background_number = t ? t->number : 9;
    return step(h);
}
static int scene_header(void *v, MMU32 n, MMMovieSessionScene *out)
{
    Host *h = (Host *)v;
    int result = step(h);
    assert(n == 9);
    out->number = n;
    out->first_frame = 1;
    out->last_frame = 20;
    out->transition = 0;
    return result < 0 ? result : result == 1 ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}
static int actor_header(void *v, MMU32 n, MMI32 *first, MMI32 *last)
{
    Host *h = (Host *)v;
    assert(n == 9);
    *first = h->offstage ? 2147483647 : 1;
    *last = 20;
    return h->header_result ? h->header_result : step(h);
}
static int group_read(void *v, MMU32 n, int start, MMMovieSceneGroup *out)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(n == 9 && !h->groups);
    if (h->group_result)
        r = h->group_result == 3 ? 0 : h->group_result;
    if (r == 1)
    {
        out->handle = h->token + (start ? 2 : 3);
        out->count = start ? 7 : 3;
        ++h->groups;
    }
    return r < 0 ? r : r == 1 ? MM_MOVIE_OK : r == 0 ? MM_MOVIE_ASSET_NOT_FOUND : MM_MOVIE_BAD_FILE;
}
static void group_release(void *v, MMMovieSceneGroup *g)
{
    Host *h = (Host *)v;
    assert(h->groups == 1 && g->handle);
    --h->groups;
    g->handle = 0;
}
static void event(void *v, const MMMovieSceneGroup *g, MMU32 i, MMMovieSceneEvent *out)
{
    Host *h = (Host *)v;
    static const int kinds[] = {MM_SCENE_ADD_ACTOR,      MM_SCENE_ADD_ACTOR,     MM_SCENE_SET_BACKGROUND,
                                MM_SCENE_SET_BACKGROUND, MM_SCENE_CHANGE_CAMERA, MM_SCENE_CHANGE_CAMERA,
                                MM_SCENE_PLAY_SOUND};
    out->kind = g->handle == h->token + 2 ? kinds[i] : i == 1 ? MM_SCENE_PAUSE : MM_SCENE_CHANGE_CAMERA;
    out->frame = (MMI32)i + 4;
    out->big_endian = 0;
    out->data = h->event_data;
    memset(out->data, 0, sizeof(h->event_data));
    out->bytes = 4;
    out->data[0] = (unsigned char)(g->handle == h->token + 2 && i < 2 ? i + 10 : i + 2);
    if (out->kind == MM_SCENE_SET_BACKGROUND)
    {
        out->bytes = 16;
        out->data[0] = 3;
        out->data[8] = 44;
        out->data[12] = (unsigned char)(i + 3);
    }
}
static int actor_load(void *v, MMU32 number, void **out)
{
    Host *h = (Host *)v;
    int r = step(h);
    /* A typed provider can return a partially loaded actor on ordinary failure. */
    if (r >= 0)
    {
        h->loaded[h->owned] = number;
        *out = h->token + 4 + h->owned++;
    }
    return r;
}
static void actor_range(void *v, void *a, MMI32 *first, MMI32 *last)
{
    (void)v;
    (void)a;
    *first = 2;
    *last = 19;
}
static int sync_actors(void *v)
{
    Host *h = (Host *)v;
    ++h->syncs;
    return step(h);
}
static int seek(void *v, MMI32 f)
{
    Host *h = (Host *)v;
    ++h->seeks;
    h->live.status.frame = f;
    return step(h);
}
static int save_error(void *v)
{
    ++((Host *)v)->saved;
    return step((Host *)v);
}
static void stats(void *v)
{
    ++((Host *)v)->stats;
}
static void setup(Host *h, MMPreviewAdmission *p, MMPreviewAdmissionServices *s, MMStorageAllocator *a, int kind)
{
    memset(h, 0, sizeof(*h));
    memset(p, 0, sizeof(*p));
    memset(s, 0, sizeof(*s));
    h->bad_cels = -1;
    a->context = h;
    a->resize = resize;
    a->release = release_storage;
    assert(mm_preview_live_init(&h->live, a));
    p->live = &h->live;
    p->type = 77;
    p->number = 9;
    p->cache = 1;
    p->kind = kind;
    s->context = h;
    s->allocator = a;
    s->clear = clear_asset;
    s->publish = publish;
    s->begin = begin;
    s->healthy = healthy;
    s->template_fetch = fetch;
    s->template_bind = bind_template;
    s->body_create = body_create;
    s->prop = prop;
    s->actions = actions;
    s->cels = cels;
    s->child = child;
    s->chunks = chunks;
    s->chunk = chunk;
    s->action_add = action_add;
    s->action_cels = action_cels;
    s->pose = pose;
    s->rest = rest;
    s->rotate = rotate;
    s->locate = locate;
    s->model_fetch = fetch;
    s->background = background;
    s->scene.context = h;
    s->scene.header = scene_header;
    s->scene.group = group_read;
    s->scene.close = group_release;
    s->scene.event = event;
    s->actor_header = actor_header;
    s->actor_load = actor_load;
    s->actor_release = actor_release;
    s->actor_range = actor_range;
    s->sync = sync_actors;
    s->seek = seek;
    s->save_actor_error = save_error;
    s->stats = stats;
}
static void dispose(Host *h)
{
    MMPreviewLiveServices s;
    memset(&s, 0, sizeof(s));
    s.context = h;
    s.actor_release = actor_release;
    mm_preview_live_dispose(&h->live, &s);
    assert(!h->groups && !h->owned);
}
static void admission_tests(void)
{
    Host h;
    MMPreviewAdmission p;
    MMPreviewAdmissionServices s;
    MMStorageAllocator a;
    int kind, n, i, failure, r, allocations, actor_call = 0, sync_call = 0, seek_call = 0;
    for (kind = MM_PREVIEW_TEMPLATE; kind <= MM_PREVIEW_ACTOR; ++kind)
    {
        setup(&h, &p, &s, &a, kind);
        assert(mm_preview_admit(&p, 1, &s) == MM_PREVIEW_ADMIT_OK && p.ready && h.stats == 1);
        n = h.calls;
        if (kind == MM_PREVIEW_TEMPLATE)
            assert(h.appended == 2 && h.names[0] == 2 && h.names[1] == -1 && h.rotations == 3 && p.prop);
        if (kind == MM_PREVIEW_SCENE)
        {
            assert(h.owned == 2 && h.loaded[0] == 100 && h.loaded[1] == 101 && h.background_number == 6 &&
                   h.live.initial_camera == 7 && h.live.cameras.count == 2 && h.live.original_actors == 2 &&
                   h.syncs == 1);
            /* header, starts, background, frames, two loads, sync, seek */
            actor_call = n - 2;
            sync_call = n - 1;
            seek_call = n;
        }
        if (kind == MM_PREVIEW_ACTOR)
            assert(h.live.status.first == 2 && h.live.status.last == 19 && h.loaded[0] == 9);
        dispose(&h);
        for (failure = -2; failure <= 0; ++failure)
            for (i = 1; i <= n; ++i)
            {
                setup(&h, &p, &s, &a, kind);
                h.fail_at = i;
                h.failure = failure;
                r = mm_preview_admit(&p, 0, &s);
                assert(!h.groups);
                if (failure < 0)
                    assert(r == (failure == -1 ? MM_PREVIEW_ADMIT_MEMORY : MM_PREVIEW_ADMIT_EXCEPTION));
                if (failure == -1)
                    assert(!p.ready && !h.owned && h.clears == 2);
                dispose(&h);
            }
    }
    setup(&h, &p, &s, &a, MM_PREVIEW_TEMPLATE);
    h.body_missing = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_DEPENDENCIES && p.mark_chunk && !h.poses && h.appended == 2);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_PREVIEW_TEMPLATE);
    h.bad_cels = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_OK && p.action_count == 1);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_PREVIEW_TEMPLATE);
    h.empty_actions = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_DEPENDENCIES && !p.ready);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_PREVIEW_TEMPLATE);
    h.unhealthy = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_DEPENDENCIES && p.mark_chunk);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_PREVIEW_SCENE);
    h.missing_child = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_ACTOR_CHILD && p.detail == 10 && p.mark_chunk &&
           h.clears == 1);
    dispose(&h);
    for (i = 2; i <= 3; ++i)
    {
        setup(&h, &p, &s, &a, MM_PREVIEW_SCENE);
        h.group_result = i;
        assert(mm_preview_admit(&p, 0, &s) == (i == 2 ? MM_PREVIEW_ADMIT_START_READ : MM_PREVIEW_ADMIT_START_MISSING));
        dispose(&h);
    }
    setup(&h, &p, &s, &a, MM_PREVIEW_ACTOR);
    h.offstage = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_OFFSTAGE && !h.owned);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_PREVIEW_UNKNOWN);
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_UNSUPPORTED);
    p.cache = 0;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_CACHE);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_PREVIEW_SCENE);
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_OK);
    allocations = h.allocations;
    dispose(&h);
    for (i = 1; i <= allocations; ++i)
    {
        setup(&h, &p, &s, &a, MM_PREVIEW_SCENE);
        h.allocation_fail = i;
        assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_MEMORY && !h.groups && h.clears == 2 && !h.owned);
        dispose(&h);
    }
    setup(&h, &p, &s, &a, MM_PREVIEW_SCENE);
    h.fail_at = actor_call;
    h.failure = 0;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_ACTOR_LOAD && h.saved == 1 && h.clears == 2 &&
           h.releases == 2 && !p.mark_chunk);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_PREVIEW_SCENE);
    h.fail_at = sync_call;
    h.failure = 0;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_EXTERNAL && h.clears == 1 && h.owned == 2 &&
           h.live.original_actors == 2 && !p.ready);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_PREVIEW_SCENE);
    h.fail_at = seek_call;
    h.failure = 0;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_DEPENDENCIES && h.clears == 2 && !h.owned && p.mark_chunk);
    dispose(&h);
    for (i = 0; i <= 2; ++i)
    {
        if (i == 1)
            continue;
        setup(&h, &p, &s, &a, MM_PREVIEW_SCENE);
        h.fail_at = 4;
        h.failure = i;
        assert(mm_preview_admit(&p, 0, &s) == (i ? MM_PREVIEW_ADMIT_FRAME_READ : MM_PREVIEW_ADMIT_FRAME_MISSING) &&
               h.backgrounds == 1 && h.clears == 1);
        dispose(&h);
    }
    setup(&h, &p, &s, &a, MM_PREVIEW_SCENE);
    {
        MMPreviewLiveScene d;
        d.number = 9;
        d.source = 2;
        d.background = 42;
        assert(mm_storage_ensure(&h.live.scenes.storage, sizeof(d), 0, 0) == MM_STORAGE_OK);
        memcpy(h.live.scenes.storage.buffers[0].data, &d, sizeof(d));
        h.live.scenes.count = 1;
        p.cache = 0;
        h.unhealthy = 1; /* Virtual selection retains the nested seek's diagnostic policy. */
        assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_OK && h.background_number == 42 &&
               h.live.virtual_scene && h.live.status.frame == 1 && h.syncs == 1 && h.seeks == 1);
    }
    dispose(&h);
    setup(&h, &p, &s, &a, MM_PREVIEW_TEMPLATE);
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_OK);
    assert(mm_preview_admission_pose(&p, 2, 0, &s) == MM_PREVIEW_ADMIT_POSE_RANGE && p.ready);
    assert(mm_preview_admission_pose(&p, 1, 3, &s) == MM_PREVIEW_ADMIT_POSE_RANGE && p.ready);
    assert(mm_preview_admission_pose(&p, 1, 2, &s) == MM_PREVIEW_ADMIT_OK && h.stats == 2);
    h.fail_at = h.calls + 1;
    h.failure = -1;
    assert(mm_preview_admission_pose(&p, 0, 0, &s) == MM_PREVIEW_ADMIT_MEMORY && p.ready);
    h.fail_at = h.calls + 1;
    h.failure = 0;
    assert(mm_preview_admission_pose(&p, 0, 0, &s) == MM_PREVIEW_ADMIT_POSE && !p.ready);
    dispose(&h);
}
typedef struct Budget
{
    MMPreviewChunk chunks[10];
    int headers, bad_header;
    MMU32 decoded;
} Budget;
static void budget_chunk(void *v, MMU32 i, MMPreviewChunk *out)
{
    *out = ((Budget *)v)->chunks[i];
}
static int budget_header(void *v, const MMPreviewChunk *c, unsigned char *out)
{
    Budget *b = (Budget *)v;
    MMU32 n = b->decoded;
    (void)c;
    ++b->headers;
    memset(out, 0, 8);
    out[4] = (unsigned char)(n >> 24);
    out[5] = (unsigned char)(n >> 16);
    out[6] = (unsigned char)(n >> 8);
    out[7] = (unsigned char)n;
    return !b->bad_header;
}
static void budget_tests(void)
{
    Budget b;
    MMPreviewBudgetServices s;
    MMU32 i, failed = 99;
    memset(&b, 0, sizeof(b));
    s.context = &b;
    s.chunk = budget_chunk;
    s.header = budget_header;
    s.count = 8;
    for (i = 0; i < 10; ++i)
        b.chunks[i].stored = 64u * 1024 * 1024;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_OK && failed == 99);
    s.count = 9;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_LIMIT && failed == 8);
    s.count = 1;
    ++b.chunks[0].stored;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_LIMIT && failed == 0);
    b.chunks[0].packed = 1;
    b.bad_header = 1;
    failed = 99;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_HEADER && failed == 99 && b.headers == 1);
    b.bad_header = 0;
    b.chunks[0].stored = 8;
    b.decoded = 0xffffffffu;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_LIMIT);
    b.decoded = 64u * 1024 * 1024;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_OK);
    b.chunks[0].stored = 7;
    b.headers = 0;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_HEADER && !b.headers);
}
static int visible(void *v, MMU32 i)
{
    (void)v;
    return i != 1;
}
static void *actor_body(void *v, MMU32 i)
{
    (void)i;
    return v;
}
static MMI32 body_parts(void *v, void *b)
{
    (void)v;
    (void)b;
    return 5;
}
static void *part_model(void *v, void *b, MMI32 i)
{
    (void)b;
    return i == 1 ? 0 : v;
}
static MMU32 faces(void *v, void *m)
{
    (void)v;
    (void)m;
    return 7;
}
static void stats_tests(void)
{
    MMPreviewStatsServices s;
    MMU32 p, t;
    memset(&s, 0, sizeof(s));
    s.context = &s;
    s.body = &s;
    s.actors = 3;
    s.visible = visible;
    s.actor_body = actor_body;
    s.body_parts = body_parts;
    s.part_model = part_model;
    s.faces = faces;
    mm_preview_stats(&s, &p, &t);
    assert(p == 6 && t == 42);
    s.model = &s;
    mm_preview_stats(&s, &p, &t);
    assert(p == 1 && t == 7);
}
int main(void)
{
    budget_tests();
    admission_tests();
    stats_tests();
    puts(
        "Preview admission: budgets, selection prefixes, events, ownership, exception cleanup, pose and stats passed.");
    return 0;
}
