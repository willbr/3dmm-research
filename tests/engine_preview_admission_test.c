#include "mm/preview_admission.h"
#include "mm/body_create.h"
#include "mm/model_resource.h"
#include "mm/preview_actor.h"
#include "mm/scene_data.h"
#include "mm/background_resource.h"
#include "mm/template_create.h"
#include "mm/template_runtime.h"
#include "mm/text_resource.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Host
{
    MMPreviewLive live;
    MMPreviewAdmission published;
    MMStorageAllocator asset_storage;
    MMBodyCreateServices body_services;
    MMTemplateOwnerHost template_host;
    MMModelOwnerHost model_host;
    MMModelServices models;
    int assets, partial_asset;
    int calls, fail_at, failure, clears, groups, owned, releases, stats;
    int body_missing, empty_actions, bad_cels, unhealthy, missing_child, group_result, header_result;
    int offstage, names[4], appended, rests, poses, saved, syncs, seeks, backgrounds;
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
static void *allocate_asset(void *v, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Host *h = (Host *)v;
    void *p = malloc(bytes);
    (void)old_bytes;
    (void)priority;
    assert(!old);
    if (p)
        ++h->assets;
    return p;
}
static void release_asset(void *v, void *p, MMU32 bytes)
{
    assert(((Host *)v)->assets > 0);
    --((Host *)v)->assets;
    release_storage(v, p, bytes);
}
static MMBodyState *body_state(void *v, void *body)
{
    (void)v;
    return mm_body_owner_state((MMBodyOwner *)body);
}
static void empty_body_storage(void *v, void **p)
{
    (void)v;
    assert(!*p);
}
static MMBodyOwner *new_body(Host *h)
{
    MMBodyOwner *body = mm_body_owner_create(&h->asset_storage, &h->body_services);
    assert(body);
    mm_body_owner_state(body)->shape.hidden = 1;
    return body;
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
    assert(p->live->status.asset_type == p->type && p->live->status.asset_number == p->number);
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
static int fetch(void *v, MMU32 type, MMU32 number, MMTemplateOwner **out)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(type == MM_TMPL && number == 9);
    if (r > 0 || h->partial_asset)
    {
        *out = mm_template_owner_create(&h->template_host, 0);
        assert(*out);
    }
    return r;
}
static int fetch_model(void *v, MMU32 type, MMU32 number, MMModelOwner **out)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(type == MM_TEMPLATE_BMDL && number == 9);
    if (r > 0)
    {
        *out = mm_model_owner_create(&h->model_host);
        assert(*out);
    }
    return r;
}
static int bind_template(void *v, void *t)
{
    Host *h = (Host *)v;
    assert(t && t == h->live.assets.tmpl);
    return step(h);
}
static int body_create(void *v, MMTemplateOwner *t, MMBodyOwner **out)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(t && t == h->live.assets.tmpl);
    if (r > 0)
        *out = h->body_missing ? NULL : new_body(h);
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
static int action_entry(void *v, MMU32 i, MMChunkEntry *out)
{
    (void)v;
    assert(i == 0);
    memset(out, 0, sizeof(*out));
    out->type = MM_TMPL;
    out->number = 9;
    out->child_count = 1;
    return 1;
}
static int action_link(void *v, MMU32 parent, MMU32 i, MMChunkChild *out)
{
    (void)v;
    assert(parent == 0 && i == 0);
    out->type = MM_ACTN;
    out->number = 50;
    out->slot = 0;
    return 1;
}
static int scene_child(void *v, MMU32 number, MMU32 slot, MMU32 type, MMU32 *out)
{
    Host *h = (Host *)v;
    assert(number == 9 && type == MM_SCENE_ACTOR_CHUNK);
    if (h->missing_child)
        return 0;
    *out = slot + 90;
    return 1;
}
static int publish_action(void *v, MMU32 i, const MMPreviewLiveAction *action)
{
    Host *h = (Host *)v;
    int r = step(h);
    assert(action == mm_preview_live_action(&h->live, i) && action->cels == i + 2);
    if (r > 0)
    {
        h->names[i] = action->has_chunk ? (MMI32)action->chunk_number : -1;
        ++h->appended;
    }
    return r;
}
static int pose(void *v, void *t, void *b, MMU32 action, MMU32 cel)
{
    Host *h = (Host *)v;
    assert(t && b && t == h->live.assets.tmpl && b == h->live.assets.body);
    assert(action < 2 && cel < action + 2);
    ++h->poses;
    return step(h);
}
static void rest_pose(void *v, MMTemplateOwner *t, MMBodyOwner *b)
{
    Host *h = (Host *)v;
    assert(t == h->live.assets.tmpl && b == h->live.assets.body);
    ++h->rests;
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
static void setup(Host *h, MMPreviewAdmission *p, MMPreviewAdmissionServices *s, MMStorageAllocator *a, MMU32 type)
{
    memset(h, 0, sizeof(*h));
    memset(p, 0, sizeof(*p));
    memset(s, 0, sizeof(*s));
    h->bad_cels = -1;
    a->context = h;
    a->resize = resize;
    a->release = release_storage;
    assert(mm_preview_live_init(&h->live, a));
    h->asset_storage.context = h;
    h->asset_storage.resize = allocate_asset;
    h->asset_storage.release = release_asset;
    h->body_services.state = body_state;
    h->body_services.free_memory = empty_body_storage;
    h->template_host.allocator = &h->asset_storage;
    h->template_host.name_unit = 1;
    h->model_host.objects = &h->asset_storage;
    h->model_host.renderer = &h->models;
    p->live = &h->live;
    p->type = type;
    p->number = 9;
    p->cache = 1;
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
    s->action_chunks.context = h;
    s->action_chunks.count = 1;
    s->action_chunks.entry = action_entry;
    s->action_chunks.child = action_link;
    s->publish_action = publish_action;
    s->pose = pose;
    s->rest_pose = rest_pose;
    s->model_fetch = fetch_model;
    s->background = background;
    s->scene.context = h;
    s->scene.header = scene_header;
    s->scene.child = scene_child;
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
    assert(!h->groups && !h->owned && !h->assets);
}
static void admission_tests(void)
{
    Host h;
    MMPreviewAdmission p;
    MMPreviewAdmissionServices s;
    MMStorageAllocator a;
    static const MMU32 types[] = {MM_TMPL, MM_TEMPLATE_BMDL, MM_BACKGROUND, MM_SCENE_CHUNK, MM_ACTOR_CHUNK};
    size_t kind;
    int n, i, failure, r, allocations, actor_call = 0, sync_call = 0, seek_call = 0;
    for (failure = -1; failure >= -2; --failure)
    {
        setup(&h, &p, &s, &a, MM_TMPL);
        h.fail_at = 1;
        h.failure = failure;
        h.partial_asset = 1;
        r = mm_preview_admit(&p, 0, &s);
        assert(r == (failure == -1 ? MM_PREVIEW_ADMIT_MEMORY : MM_PREVIEW_ADMIT_EXCEPTION));
        assert(h.assets == (failure == -2) && !!h.live.assets.tmpl == (failure == -2));
        dispose(&h);
    }
    setup(&h, &p, &s, &a, MM_TMPL);
    h.allocation_fail = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_MEMORY);
    assert(!h.live.actions.count && !h.appended && !h.assets && h.clears == 2);
    dispose(&h);
    for (kind = 0; kind < sizeof(types) / sizeof(types[0]); ++kind)
    {
        setup(&h, &p, &s, &a, types[kind]);
        assert(mm_preview_admit(&p, 1, &s) == MM_PREVIEW_ADMIT_OK && h.live.status.ready && h.stats == 1);
        n = h.calls;
        if (types[kind] == MM_TMPL)
            assert(h.live.actions.count == 2 && h.appended == 2 && h.names[0] == 50 && h.names[1] == -1 &&
                   h.rests == 1 && h.live.status.prop);
        if (types[kind] == MM_SCENE_CHUNK)
        {
            assert(h.owned == 2 && h.loaded[0] == 100 && h.loaded[1] == 101 && h.background_number == 6 &&
                   h.live.initial_camera == 7 && h.live.cameras.count == 2 && h.live.original_actors == 2 &&
                   h.syncs == 1);
            /* header, starts, background, frames, two loads, sync, seek */
            actor_call = n - 2;
            sync_call = n - 1;
            seek_call = n;
        }
        if (types[kind] == MM_ACTOR_CHUNK)
            assert(h.live.status.first == 2 && h.live.status.last == 19 && h.loaded[0] == 9);
        dispose(&h);
        for (failure = -2; failure <= 0; ++failure)
            for (i = 1; i <= n; ++i)
            {
                setup(&h, &p, &s, &a, types[kind]);
                h.fail_at = i;
                h.failure = failure;
                r = mm_preview_admit(&p, 0, &s);
                assert(!h.groups);
                if (failure < 0)
                    assert(r == (failure == -1 ? MM_PREVIEW_ADMIT_MEMORY : MM_PREVIEW_ADMIT_EXCEPTION));
                if (failure == -1)
                    assert(!h.live.status.ready && !h.owned && h.clears == 2 && !h.live.actions.count);
                if (failure == -2 && types[kind] == MM_TMPL)
                    assert(h.live.actions.count == (MMU32)h.appended);
                dispose(&h);
            }
    }
    setup(&h, &p, &s, &a, MM_TMPL);
    h.body_missing = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_DEPENDENCIES && p.mark_chunk && !h.poses && h.appended == 2);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_TMPL);
    h.bad_cels = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_OK && h.live.actions.count == 1);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_TMPL);
    h.empty_actions = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_DEPENDENCIES && !h.live.status.ready);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_TMPL);
    h.unhealthy = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_DEPENDENCIES && p.mark_chunk);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_SCENE_CHUNK);
    h.missing_child = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_ACTOR_CHILD && p.detail == 10 && p.mark_chunk &&
           h.clears == 1);
    dispose(&h);
    for (i = 2; i <= 3; ++i)
    {
        setup(&h, &p, &s, &a, MM_SCENE_CHUNK);
        h.group_result = i;
        assert(mm_preview_admit(&p, 0, &s) == (i == 2 ? MM_PREVIEW_ADMIT_START_READ : MM_PREVIEW_ADMIT_START_MISSING));
        dispose(&h);
    }
    setup(&h, &p, &s, &a, MM_ACTOR_CHUNK);
    h.offstage = 1;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_OFFSTAGE && !h.owned);
    dispose(&h);
    setup(&h, &p, &s, &a, 0x12345678);
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_UNSUPPORTED);
    p.cache = 0;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_CACHE);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_SCENE_CHUNK);
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_OK);
    allocations = h.allocations;
    dispose(&h);
    for (i = 1; i <= allocations; ++i)
    {
        setup(&h, &p, &s, &a, MM_SCENE_CHUNK);
        h.allocation_fail = i;
        assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_MEMORY && !h.groups && h.clears == 2 && !h.owned);
        dispose(&h);
    }
    setup(&h, &p, &s, &a, MM_SCENE_CHUNK);
    h.fail_at = actor_call;
    h.failure = 0;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_ACTOR_LOAD && h.saved == 1 && h.clears == 2 &&
           h.releases == 2 && !p.mark_chunk);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_SCENE_CHUNK);
    h.fail_at = sync_call;
    h.failure = 0;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_EXTERNAL && h.clears == 1 && h.owned == 2 &&
           h.live.original_actors == 2 && !h.live.status.ready);
    dispose(&h);
    setup(&h, &p, &s, &a, MM_SCENE_CHUNK);
    h.fail_at = seek_call;
    h.failure = 0;
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_DEPENDENCIES && h.clears == 2 && !h.owned && p.mark_chunk);
    dispose(&h);
    for (i = 0; i <= 2; ++i)
    {
        if (i == 1)
            continue;
        setup(&h, &p, &s, &a, MM_SCENE_CHUNK);
        h.fail_at = 4;
        h.failure = i;
        assert(mm_preview_admit(&p, 0, &s) == (i ? MM_PREVIEW_ADMIT_FRAME_READ : MM_PREVIEW_ADMIT_FRAME_MISSING) &&
               h.backgrounds == 1 && h.clears == 1);
        dispose(&h);
    }
    setup(&h, &p, &s, &a, MM_SCENE_CHUNK);
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
    setup(&h, &p, &s, &a, MM_TMPL);
    assert(mm_preview_admit(&p, 0, &s) == MM_PREVIEW_ADMIT_OK);
    assert(mm_preview_admission_pose(&p, 2, 0, &s) == MM_PREVIEW_ADMIT_POSE_RANGE && h.live.status.ready);
    assert(mm_preview_admission_pose(&p, 1, 3, &s) == MM_PREVIEW_ADMIT_POSE_RANGE && h.live.status.ready);
    assert(mm_preview_admission_pose(&p, 1, 2, &s) == MM_PREVIEW_ADMIT_OK && h.stats == 2);
    h.fail_at = h.calls + 1;
    h.failure = -1;
    assert(mm_preview_admission_pose(&p, 0, 0, &s) == MM_PREVIEW_ADMIT_MEMORY && h.live.status.ready);
    h.fail_at = h.calls + 1;
    h.failure = 0;
    assert(mm_preview_admission_pose(&p, 0, 0, &s) == MM_PREVIEW_ADMIT_POSE && !h.live.status.ready);
    dispose(&h);
}
typedef struct Budget
{
    MMChunkEntry chunks[10];
    int headers, bad_header;
    MMU32 decoded;
} Budget;
static int budget_chunk(void *v, MMU32 i, MMChunkEntry *out)
{
    *out = ((Budget *)v)->chunks[i];
    return 1;
}
static int budget_header(void *v, const MMChunkEntry *c, unsigned char *out)
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
    MMChunkEntry failed = {0};
    MMU32 i;
    failed.number = 99;
    memset(&b, 0, sizeof(b));
    s.context = &b;
    s.chunks.context = &b;
    s.chunks.entry = budget_chunk;
    s.chunks.child = NULL;
    s.header = budget_header;
    s.chunks.count = 8;
    for (i = 0; i < 10; ++i)
    {
        b.chunks[i].number = i;
        b.chunks[i].bytes = 64u * 1024 * 1024;
    }
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_OK && failed.number == 99);
    s.chunks.count = 9;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_LIMIT && failed.number == 8);
    s.chunks.count = 1;
    ++b.chunks[0].bytes;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_LIMIT && failed.number == 0);
    b.chunks[0].flags = MM_CHUNK_PACKED;
    b.bad_header = 1;
    failed.number = 99;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_HEADER && failed.number == 99 && b.headers == 1);
    b.bad_header = 0;
    b.chunks[0].bytes = 8;
    b.decoded = 0xffffffffu;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_LIMIT);
    b.decoded = 64u * 1024 * 1024;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_OK);
    b.chunks[0].bytes = 7;
    b.headers = 0;
    assert(mm_preview_budget(&s, &failed) == MM_PREVIEW_BUDGET_HEADER && !b.headers);
}
static const MMPreviewActor *actor_view(const void *actor)
{
    return (const MMPreviewActor *)actor;
}
static MMU32 model_faces(const void *model)
{
    assert(model);
    return 7;
}
static void *allocate_model(void *v, MMI32 vertices, MMI32 faces)
{
    assert(vertices == 0 && faces == 0);
    return v;
}
static void identify_model(void *v, void *handle, void *owner)
{
    assert(v == handle && owner);
}
static void copy_model(void *v, void *handle, const void *vertices, const void *faces, MMI32 n, MMI32 f)
{
    assert(v == handle && !vertices && !faces && !n && !f);
}
static void model_effect(void *v, void *handle)
{
    assert(v == handle);
}
static void stats_tests(void)
{
    Host h;
    MMPreviewAdmission admission;
    MMPreviewAdmissionServices services;
    MMStorageAllocator allocator;
    MMPreviewActor actors[3];
    MMBodyPartBinding bindings[5];
    MMBodyShape *shape;
    int i;
    setup(&h, &admission, &services, &allocator, MM_TMPL);
    h.live.assets.body = new_body(&h);
    shape = &mm_body_owner_state(h.live.assets.body)->shape;
    memset(bindings, 0, sizeof(bindings));
    bindings[0].model_handle = bindings[1].model_handle = &h;
    bindings[2].model_handle = bindings[4].model_handle = &h;
    shape->bindings = bindings;
    shape->part_count = 3;
    h.live.actor_view = actor_view;
    h.live.model_faces = model_faces;
    for (i = 0; i < 3; ++i)
    {
        mm_preview_actor_init(&actors[i]);
        actors[i].body = h.live.assets.body;
        actors[i].state.onStage = i != 1;
        assert(mm_preview_live_adopt(&h.live, &actors[i]) == MM_PREVIEW_LIVE_OK);
        ++h.owned;
    }
    mm_preview_live_refresh_geometry(&h.live);
    assert(h.live.status.parts == 6 && h.live.status.triangles == 42);
    h.models.context = &h;
    h.models.allocate_model = allocate_model;
    h.models.identify = identify_model;
    h.models.copy_input = copy_model;
    h.models.add = h.models.remove = h.models.free_model = model_effect;
    h.live.assets.model = mm_model_new(0, NULL, 0, NULL, &h.model_host);
    assert(h.live.assets.model);
    mm_preview_live_refresh_geometry(&h.live);
    assert(h.live.status.parts == 1 && h.live.status.triangles == 7);
    dispose(&h);
    assert(h.live.actor_view == actor_view && h.live.model_faces == model_faces &&
           !h.live.status.parts && !h.live.status.triangles);
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
