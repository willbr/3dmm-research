#include "mm/preview_live.h"
#include "mm/preview_objects.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MIN_FRAME (-2147483647 - 1)
typedef struct Actor
{
    MMU32 number;
    MMI32 frame;
    int live, id, template_number;
} Actor;
typedef struct Host
{
    MMPreviewLive live;
    MMPreviewLiveServices services;
    MMPreviewLiveDefinitions definitions;
    MMPreviewLiveScene scenes[4];
    MMPreviewLiveProp props[8];
    Actor actors[64];
    int reject_camera_once, forced_camera;
    int attempts, fail_at, memory, created, released[64], releases, actor_live, clock;
    int new_fail, create_fail, create_error, creates, seek_fail, seek_error, seeks;
    int camera_result, camera_ready, cameras, published, stats, begins, health, unhealthy;
    MMU32 selected;
    MMI32 published_frame;
} Host;
static void *resize(void *v, void *old, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Host *h = (Host *)v;
    void *p;
    (void)old_bytes;
    (void)priority;
    if (++h->attempts >= h->fail_at && h->fail_at)
        return 0;
    p = realloc(old, bytes);
    if (p && !old)
        ++h->memory;
    return p;
}
static void release_memory(void *v, void *p, MMU32 bytes)
{
    Host *h = (Host *)v;
    (void)bytes;
    assert(h->memory > 0);
    --h->memory;
    free(p);
}
static void *actor_new(void *v)
{
    Host *h = (Host *)v;
    Actor *a;
    if (h->new_fail && h->created + 1 == h->new_fail)
        return 0;
    assert(h->created < 64);
    a = h->actors + h->created;
    a->id = h->created++;
    a->live = 1;
    ++h->actor_live;
    return a;
}
static int actor_create(void *v, void *p, const MMPreviewLiveProp *d)
{
    Host *h = (Host *)v;
    Actor *a = (Actor *)p;
    assert(a->live);
    ++h->creates;
    a->number = d->number;
    a->template_number = (int)d->template_number;
    return h->creates == h->create_fail ? h->create_error : 1;
}
static int actor_seek(void *v, void *p, MMI32 frame)
{
    Host *h = (Host *)v;
    Actor *a = (Actor *)p;
    assert(a->live);
    ++h->seeks;
    if (h->seeks == h->seek_fail)
        return h->seek_error;
    a->frame = frame;
    ++h->clock;
    return 1;
}
static MMU32 actor_number(void *v, void *p)
{
    (void)v;
    return ((Actor *)p)->number;
}
static void actor_release(void *v, void *p)
{
    Host *h = (Host *)v;
    Actor *a = (Actor *)p;
    assert(a->live && h->actor_live > 0 && h->releases < 64);
    h->released[h->releases++] = a->id;
    a->live = 0;
    --h->actor_live;
}
static void scene(void *v, MMU32 i, MMPreviewLiveScene *out)
{
    *out = ((Host *)v)->scenes[i];
}
static void prop(void *v, MMU32 i, MMPreviewLiveProp *out)
{
    *out = ((Host *)v)->props[i];
}
static void begin_seek(void *v)
{
    ++((Host *)v)->begins;
}
static int healthy(void *v)
{
    Host *h = (Host *)v;
    return ++h->health != h->unhealthy;
}
static void publish(void *v, const MMPreviewLive *p)
{
    Host *h = (Host *)v;
    assert(p == &h->live);
    ++h->published;
    h->published_frame = p->status.frame;
}
static void stats(void *v)
{
    Host *h = (Host *)v;
    assert(h->published && h->published_frame == h->live.status.frame);
    assert(h->live.publication.phase == MM_MOVIE_FRAME_COMMITTED && h->live.publication.frame == h->published_frame);
    ++h->stats;
}
static int camera(void *v, MMU32 index)
{
    Host *h = (Host *)v;
    if (h->live.status.current_camera == h->live.status.camera_count)
        ++h->forced_camera;
    if (++h->cameras == h->reject_camera_once)
    {
        h->live.status.ready = 0;
        return 0;
    }
    h->selected = index;
    h->live.status.ready = h->camera_ready;
    return h->camera_result;
}
static void clear_asset(void *v)
{
    Host *h = (Host *)v;
    assert(!h->actor_live && !h->live.actors.count && !h->live.status.ready);
    /* The native resource adapter may call clear again; it is idempotent. */
    mm_preview_live_clear(&h->live, &h->services);
}
static void init(Host *h)
{
    MMStorageAllocator a;
    MMPreviewLiveServices *s;
    Actor *actor;
    int i;
    memset(h, 0, sizeof(*h));
    a.context = h;
    a.resize = resize;
    a.release = release_memory;
    assert(mm_preview_live_init(&h->live, &a));
    s = &h->services;
    s->context = h;
    s->actor_new = actor_new;
    s->actor_create = actor_create;
    s->actor_seek = actor_seek;
    s->actor_number = actor_number;
    s->actor_release = actor_release;
    s->camera = camera;
    s->begin_seek = begin_seek;
    s->healthy = healthy;
    s->publish = publish;
    s->stats = stats;
    s->clear_asset = clear_asset;
    h->camera_ready = h->camera_result = 1;
    h->live.status.ready = h->live.status.scene = h->live.status.timeline = h->live.status.background = 1;
    h->live.status.asset_number = 7;
    h->live.status.camera_count = 4;
    h->live.status.first = h->live.original_first = 10;
    h->live.status.last = h->live.original_last = 20;
    h->live.status.frame = 15;
    h->live.initial_camera = 1;
    for (i = 0; i < 2; ++i)
    {
        actor = (Actor *)actor_new(h);
        actor->number = (MMU32)(100 + i);
        actor->frame = 15;
        assert(mm_preview_live_adopt(&h->live, actor) == 0);
    }
    assert(mm_preview_live_camera_add(&h->live, 10, 1) == 0);
    assert(mm_preview_live_camera_add(&h->live, 10, 2) == 0);
    assert(mm_preview_live_camera_add(&h->live, 15, 3) == 0);
    assert(mm_preview_live_seal(&h->live) == 0);
    h->live.status.reported_actors = 2;
    h->definitions.context = h;
    h->definitions.scene = scene;
    h->definitions.prop = prop;
    h->definitions.scene_count = 1;
    h->definitions.prop_count = 3;
    h->scenes[0].number = 7;
    h->scenes[0].background = 8;
    h->scenes[0].source = 2;
    h->props[0].scene = h->props[2].scene = 7;
    h->props[1].scene = 8;
    h->props[0].number = 300;
    h->props[2].number = 400;
    h->props[1].number = 999;
    for (i = 0; i < 3; ++i)
        h->props[i].template_number = 1;
}
static void dispose(Host *h)
{
    mm_preview_live_dispose(&h->live, &h->services);
    assert(!h->memory && !h->actor_live);
}
static int definitions(Host *h)
{
    return mm_preview_live_definitions(&h->live, &h->definitions, &h->services);
}
static MMPreviewLiveCamera *cues(Host *h)
{
    return (MMPreviewLiveCamera *)h->live.cameras.storage.buffers[0].data;
}
static void reconciliation_tests(void)
{
    Host h;
    void *a, *b;
    int i, n, base, result, clock;
    init(&h);
    base = h.attempts;
    assert(definitions(&h) == 0 && h.live.actors.count == 4 && h.live.status.reported_actors == 4);
    assert(h.creates == 2 && h.seeks == 2 && !h.health && !h.begins && !h.stats);
    a = mm_preview_live_actor(&h.live, 2);
    b = mm_preview_live_actor(&h.live, 3);
    assert(((Actor *)a)->number == 300 && ((Actor *)b)->number == 400);
    n = h.attempts - base;
    h.props[0].template_number = 99;
    h.props[2].template_number = 98;
    assert(definitions(&h) == 0 && h.creates == 2 && mm_preview_live_actor(&h.live, 2) == a);
    assert(((Actor *)a)->template_number == 1);
    h.props[0].number = 500;
    h.props[2].number = 600;
    h.create_fail = h.creates + 2;
    h.create_error = 0;
    h.scenes[0].number = 9;
    clock = h.clock;
    assert(definitions(&h) == MM_PREVIEW_LIVE_CREATE);
    assert(mm_preview_live_actor(&h.live, 2) == a && mm_preview_live_actor(&h.live, 3) == b);
    assert(h.live.props.count == 3 && mm_preview_live_scene(&h.live, 7) && !mm_preview_live_scene(&h.live, 9));
    assert(h.actor_live == 4 && h.clock == clock + 1 && h.releases == 2);
    assert(h.released[0] == 5 && h.released[1] == 4); /* current, then created prefix */
    h.create_fail = 0;
    assert(definitions(&h) == 0 && h.released[2] == 2 && h.released[3] == 3);
    assert(((Actor *)mm_preview_live_actor(&h.live, 2))->number == 500);
    h.live.virtual_scene = 1;
    h.definitions.scene_count = h.definitions.prop_count = 0;
    assert(definitions(&h) == 0 && !h.actor_live && !h.live.status.ready && !h.live.virtual_scene);
    dispose(&h);
    /* Allocation failure preserves actors and the completed assignment prefix.
     * In particular, scenes may publish before prop allocation fails. */
    for (i = 1; i <= n; ++i)
    {
        init(&h);
        h.fail_at = h.attempts + i;
        result = definitions(&h);
        assert(result == MM_PREVIEW_LIVE_MEMORY && h.actor_live == 2 && h.live.actors.count == 2);
        assert(h.live.props.count == 0 || h.live.props.count == 3);
        if (i == 2)
            assert(h.live.scenes.count == 1 && h.live.props.count == 0);
        assert(h.live.status.reported_actors == 2);
        h.fail_at = 0;
        assert(definitions(&h) == 0 && h.live.actors.count == 4);
        dispose(&h);
    }
    for (i = -2; i <= 0; ++i)
    {
        init(&h);
        h.create_fail = 2;
        h.create_error = i;
        result = definitions(&h);
        assert(result == (i == -2   ? MM_PREVIEW_LIVE_EXCEPTION
                          : i == -1 ? MM_PREVIEW_LIVE_MEMORY
                                    : MM_PREVIEW_LIVE_CREATE));
        assert(h.live.status.ready && h.actor_live == 2);
        assert(h.live.props.count == (i == 0 ? 0U : 3U));
        assert(h.released[0] == 3 && h.released[1] == 2);
        dispose(&h);
    }
    init(&h);
    h.new_fail = h.created + 2;
    assert(definitions(&h) == MM_PREVIEW_LIVE_MEMORY && h.actor_live == 2 && h.releases == 1);
    dispose(&h);
    /* Adoption failure never transfers the caller's foreign object. */
    init(&h);
    while (h.live.actors.count < h.live.actors.storage.buffers[0].capacity / sizeof(void *))
        assert(mm_preview_live_adopt(&h.live, actor_new(&h)) == 0);
    a = actor_new(&h);
    h.fail_at = h.attempts + 1;
    assert(mm_preview_live_adopt(&h.live, a) == MM_PREVIEW_LIVE_MEMORY && ((Actor *)a)->live);
    actor_release(&h, a);
    dispose(&h);
    /* Reconciliation retains a prefix, rather than searching/reordering the tail. */
    init(&h);
    assert(definitions(&h) == 0);
    b = mm_preview_live_actor(&h.live, 3);
    h.props[0] = h.props[2];
    h.definitions.prop_count = 1;
    assert(definitions(&h) == 0 && h.creates == 3 && h.live.actors.count == 3);
    assert(mm_preview_live_actor(&h.live, 2) != b && h.released[0] == 2 && h.released[1] == 3);
    dispose(&h);
    /* Snapshot allocation failures leave both existing definition sets intact;
     * later failures retain the newly assigned records without replacing actors. */
    init(&h);
    assert(definitions(&h) == 0);
    h.props[0].number = 500;
    h.props[2].number = 600;
    base = h.attempts;
    assert(definitions(&h) == 0);
    n = h.attempts - base;
    dispose(&h);
    for (i = 1; i <= n; ++i)
    {
        init(&h);
        assert(definitions(&h) == 0);
        a = mm_preview_live_actor(&h.live, 2);
        h.props[0].number = 500;
        h.props[2].number = 600;
        h.fail_at = h.attempts + i;
        assert(definitions(&h) == MM_PREVIEW_LIVE_MEMORY && h.actor_live == 4);
        assert(mm_preview_live_actor(&h.live, 2) == a && ((Actor *)a)->number == 300);
        assert(((MMPreviewLiveProp *)h.live.props.storage.buffers[0].data)[0].number == (i <= 2 ? 300U : 500U));
        h.fail_at = 0;
        assert(definitions(&h) == 0 && ((Actor *)mm_preview_live_actor(&h.live, 2))->number == 500);
        dispose(&h);
    }
}
static void seek_tests(void)
{
    Host h;
    int i, result;
    for (i = -2; i <= 0; ++i)
    {
        init(&h);
        h.seek_fail = 2;
        h.seek_error = i;
        result = mm_preview_live_seek(&h.live, 16, &h.services);
        assert(result == (i == -2   ? MM_PREVIEW_LIVE_EXCEPTION
                          : i == -1 ? MM_PREVIEW_LIVE_SEEK_MEMORY
                                    : MM_PREVIEW_LIVE_SEEK));
        assert(((Actor *)mm_preview_live_actor(&h.live, 0))->frame == 16 &&
               ((Actor *)mm_preview_live_actor(&h.live, 1))->frame == 15);
        assert(h.live.status.frame == 15 && h.live.status.ready == (i == -2));
        assert(!h.cameras && !h.stats && h.begins == 1 && h.published == 1);
        dispose(&h);
    }
    for (i = -2; i <= 0; ++i)
    {
        init(&h);
        h.camera_result = i;
        h.camera_ready = 1;
        result = mm_preview_live_seek(&h.live, 16, &h.services);
        assert(result == (i == -2   ? MM_PREVIEW_LIVE_EXCEPTION
                          : i == -1 ? MM_PREVIEW_LIVE_SEEK_MEMORY
                                    : MM_PREVIEW_LIVE_CAMERA));
        assert(h.live.status.frame == 15 && h.live.status.ready == (i != -1) && h.seeks == 2 && h.selected == 3 &&
               !h.stats);
        dispose(&h);
    }
    init(&h);
    h.unhealthy = 1;
    assert(mm_preview_live_seek(&h.live, 16, &h.services) == MM_PREVIEW_LIVE_SEEK && !h.live.status.ready &&
           h.seeks == 1);
    dispose(&h);
    init(&h);
    h.camera_result = 0;
    h.camera_ready = 0;
    assert(mm_preview_live_seek(&h.live, 16, &h.services) == MM_PREVIEW_LIVE_CAMERA && !h.live.status.ready);
    dispose(&h);
    init(&h);
    assert(mm_preview_live_seek(&h.live, 9, &h.services) == MM_PREVIEW_LIVE_FRAME && !h.seeks && h.begins == 1);
    assert(mm_preview_live_seek(&h.live, 16, &h.services) == 0 && h.live.status.frame == 16 && h.stats == 1);
    assert(mm_preview_live_seek(&h.live, 21, &h.services) == MM_PREVIEW_LIVE_FRAME && h.stats == 1);
    assert(h.live.publication.phase == MM_MOVIE_FRAME_COMMITTED && h.live.publication.frame == 16);
    h.live.status.background = 0;
    assert(mm_preview_live_seek(&h.live, 10, &h.services) == 0 && h.live.status.frame == 10 && h.cameras == 1);
    dispose(&h);
    /* Original cues retain wire order: a future cue does not hide a later
     * qualifying record, and the last qualifying record wins. */
    init(&h);
    assert(mm_preview_live_camera_add(&h.live, 30, 1) == 0 && mm_preview_live_camera_add(&h.live, 12, 0) == 0);
    assert(mm_preview_live_seek(&h.live, 16, &h.services) == 0 && h.selected == 0);
    dispose(&h);
}
static void edit_get(void *v, MMU32 i, MMPreviewLiveCamera *out)
{
    *out = ((MMPreviewLiveCamera *)v)[i];
}
static void camera_tests(void)
{
    Host h;
    MMPreviewLiveCamera edits[] = {{17, 1}, {MIN_FRAME, 3}, {17, 2}, {9, 1}};
    MMPreviewLiveCamera unsorted[] = {{17, 1}, {9, 0}};
    MMPreviewLiveCamera rejected = {15, 0};
    MMPreviewLiveCamera many[16], saved[8];
    MMPreviewLiveCameraEdits input;
    MMPreviewLiveStatus status;
    unsigned i;
    int base;
    init(&h);
    assert(mm_preview_live_range(&h.live, 5, 25) == 0);
    assert(cues(&h)[0].frame == 5 && cues(&h)[0].camera == 2 && cues(&h)[1].camera == 1);
    status = h.live.status;
    assert(mm_preview_live_range(&h.live, MIN_FRAME, 25) == MM_PREVIEW_LIVE_RANGE);
    assert(mm_preview_live_range(&h.live, 11, 25) == MM_PREVIEW_LIVE_RANGE);
    assert(mm_preview_live_range(&h.live, 5, 19) == MM_PREVIEW_LIVE_RANGE &&
           !memcmp(&status, &h.live.status, sizeof(status)));
    input.context = edits;
    input.count = 4;
    input.get = edit_get;
    h.live.status.frame = 30;
    assert(mm_preview_live_cameras(&h.live, &input, &h.services, &h.services) == 0);
    assert(h.live.status.first == 5 && h.live.status.last == 25 && h.live.status.frame == 25 && h.selected == 2);
    assert(mm_preview_live_seek(&h.live, 16, &h.services) == 0 && h.selected == 3 && h.live.status.current_camera == 3);
    assert(mm_preview_live_seek(&h.live, 17, &h.services) == 0 && h.selected == 2);
    assert(mm_preview_live_seek(&h.live, 25, &h.services) == 0 && h.selected == 2);
    assert(h.live.cameras.count == 4 && cues(&h)[0].frame == 5 && cues(&h)[0].camera == 3 && cues(&h)[1].frame == 9 &&
           cues(&h)[2].frame == 15 && cues(&h)[3].frame == 17 && cues(&h)[3].camera == 2);
    memcpy(saved, cues(&h), 4 * sizeof(*saved));
    status = h.live.status;
    base = h.begins;
    edits[0].camera = 4;
    assert(mm_preview_live_cameras(&h.live, &input, &h.services, &h.services) == MM_PREVIEW_LIVE_CAMERA_INDEX &&
           !memcmp(saved, cues(&h), 4 * sizeof(*saved)) && !memcmp(&status, &h.live.status, sizeof(status)) &&
           h.begins == base);
    input.count = 0;
    assert(mm_preview_live_cameras(&h.live, &input, &h.services, &h.services) == 0 && h.live.cameras.count == 3 && h.selected == 3);
    assert(cues(&h)[0].frame == 5 && cues(&h)[0].camera == 2 && cues(&h)[1].camera == 1);
    dispose(&h);
    /* A failed sort scratch allocation retains stable equal-frame ordering. */
    init(&h);
    input.context = unsorted;
    input.count = 2;
    h.fail_at = h.attempts + 3;
    assert(mm_preview_live_cameras(&h.live, &input, &h.services, &h.services) == 0 && h.attempts >= h.fail_at);
    assert(cues(&h)[0].frame == 9 && cues(&h)[1].camera == 1 && cues(&h)[2].camera == 2 && cues(&h)[4].frame == 17);
    dispose(&h);
    init(&h);
    for (i = 0; i < 16; ++i)
    {
        many[i].frame = (MMI32)i + 30;
        many[i].camera = i % 4;
    }
    input.context = many;
    input.count = 16;
    h.fail_at = h.attempts + 1;
    assert(mm_preview_live_cameras(&h.live, &input, &h.services, &h.services) == MM_PREVIEW_LIVE_MEMORY);
    assert(!h.begins && !h.seeks && h.live.status.ready && h.live.cameras.count == 3);
    dispose(&h);
    /* Restore the old track and visible camera after a rejected cut. */
    init(&h);
    memcpy(saved, cues(&h), 3 * sizeof(*saved));
    input.context = &rejected;
    input.count = 1;
    h.reject_camera_once = h.cameras + 1;
    assert(mm_preview_live_cameras(&h.live, &input, &h.services, &h.services) == MM_PREVIEW_LIVE_CAMERA);
    assert(h.begins == 2 && h.stats == 1 && h.live.status.ready && h.selected == 3 && h.forced_camera == 1);
    assert(h.live.cameras.count == 3 && !memcmp(saved, cues(&h), 3 * sizeof(*saved)));
    h.camera_result = 0;
    assert(mm_preview_live_cameras(&h.live, &input, &h.services, &h.services) == MM_PREVIEW_LIVE_CAMERA);
    assert(!h.live.status.ready && h.live.cameras.count == 3 && !memcmp(saved, cues(&h), 3 * sizeof(*saved)));
    dispose(&h);
    /* Evaluation exceptions remain distinct from camera-edit preparation OOM. */
    init(&h);
    input.count = 0;
    h.seek_fail = 2;
    h.seek_error = -1;
    assert(mm_preview_live_cameras(&h.live, &input, &h.services, &h.services) == MM_PREVIEW_LIVE_SEEK_MEMORY);
    assert(h.begins == 1 && !h.live.status.ready && h.live.status.frame == 15 && !h.stats);
    dispose(&h);
    /* Sealing publishes the original actor prefix before snapshot allocation. */
    init(&h);
    for (i = 0; i < 10; ++i)
        assert(mm_preview_live_camera_add(&h.live, (MMI32)i + 30, i % 4) == 0);
    assert(mm_preview_live_adopt(&h.live, actor_new(&h)) == 0);
    h.fail_at = h.attempts + 1;
    assert(mm_preview_live_seal(&h.live) == MM_PREVIEW_LIVE_MEMORY);
    assert(h.live.original_actors == 3 && h.live.original_cameras.count == 3 && h.actor_live == 3);
    dispose(&h);
}
typedef struct Background
{
    MMU32 count, selected;
    int loads, selections, commits, load_result, select_result;
} Background;

static int load_background(void *context, MMU32 *count)
{
    Background *b = (Background *)context;
    ++b->loads;
    *count = b->count;
    return b->load_result;
}

static int select_background_camera(void *context, MMU32 camera)
{
    Background *b = (Background *)context;
    ++b->selections;
    b->selected = camera;
    return b->select_result;
}

static void commit_background(void *context)
{
    ++((Background *)context)->commits;
}

static void background_tests(void)
{
    Host h;
    Background b = {0};
    MMPreviewLiveBackgroundServices s;
    MMPreviewLiveStatus old;
    init(&h);
    s.context = &b;
    s.load = load_background;
    s.select = select_background_camera;
    s.commit = commit_background;
    h.live.status.ready = 0;
    assert(mm_preview_live_replace_background(&h.live, &s) == MM_PREVIEW_LIVE_SCENE && !b.loads);

    h.live.status.ready = 1;
    mm_preview_live_background_loaded(&h.live, 4, 3);
    old = h.live.status;
    assert(mm_preview_live_replace_background(&h.live, &s) == MM_PREVIEW_LIVE_BACKGROUND && b.loads == 1);
    assert(!b.selections && !b.commits && !memcmp(&old, &h.live.status, sizeof(old)));

    b.count = 2;
    b.load_result = 1;
    b.select_result = -2;
    assert(mm_preview_live_replace_background(&h.live, &s) == MM_PREVIEW_LIVE_EXCEPTION && b.selected == 1);
    assert(b.selections == 1 && !b.commits && !memcmp(&old, &h.live.status, sizeof(old)));

    b.select_result = 1;
    assert(mm_preview_live_replace_background(&h.live, &s) == MM_PREVIEW_LIVE_OK);
    assert(b.commits == 1 && b.selections == 2 && h.live.status.camera_count == 2 && h.live.status.current_camera == 1);
    assert(h.live.status.ready && h.live.status.frame == old.frame);
    dispose(&h);
}

static const MMPreviewActor *object_view(const void *actor)
{
    return (const MMPreviewActor *)actor;
}

static void object_snapshot_tests(void)
{
    Host host = {0};
    MMPreviewActor actors[3] = {0};
    void *handles[3];
    MMPreviewObjectRows rows;
    MMPreviewActorInfo actor;
    MMStorageAllocator allocator = {NULL, resize, release_memory};
    float position[3];
    allocator.context = &host;
    handles[0] = actors;
    handles[1] = actors + 1;
    handles[2] = actors + 2;
    host.live.actors.storage.buffers[0].data = (unsigned char *)handles;
    host.live.actors.count = 2;
    host.live.actor_view = object_view;
    actors[0].header.actor_id = actors[1].header.actor_id = 4;
    actors[0].number = 7;
    actors[0].state.onStage = 1;
    actors[0].state.position.dxr = -32768;
    actors[0].state.routeTranslation.dxr = 98304;
    actors[1].number = 3;
    actors[1].inactive = 1;
    assert(mm_preview_object_rows_init(&rows, &allocator));
    assert(!mm_preview_object_count(&host.live) && !mm_preview_object_at(&host.live, 0, &actor));
    host.live.status.ready = 1;
    assert(mm_preview_object_rows_capture(&rows, &host.live) == MM_STORAGE_OK && rows.count == 2);
    assert(mm_preview_object_rows_data(&rows)[0].number == 7 && mm_preview_object_rows_data(&rows)[0].visible);
    assert(mm_preview_object_rows_data(&rows)[1].number == 3 && !mm_preview_object_rows_data(&rows)[1].has_lifetime);
    assert(mm_preview_object_rows_find(&rows, 4, 0) == 0 && mm_preview_object_rows_find(&rows, 4, 1) == 1);
    mm_preview_object_position(mm_preview_object_rows_data(&rows), position);
    assert(position[0] == 1.0f && position[1] == 0.0f && position[2] == 0.0f);
    actors[0].number = 12;
    host.live.actors.count = 3;
    host.fail_at = host.attempts + 1;
    assert(mm_preview_object_rows_capture(&rows, &host.live) == MM_STORAGE_MEMORY && rows.count == 2);
    assert(mm_preview_object_rows_data(&rows)[0].number == 7);
    handles[2] = NULL;
    host.fail_at = 0;
    assert(mm_preview_object_rows_capture(&rows, &host.live) == MM_STORAGE_ARGUMENT && rows.count == 2);
    assert(mm_preview_object_rows_data(&rows)[0].number == 7);
    assert(mm_preview_object_at(&host.live, 0, &actor) && mm_preview_object_rows_replace(&rows, 0, &actor));
    assert(mm_preview_object_rows_data(&rows)[0].number == 12 && mm_preview_object_rows_data(&rows)[1].number == 3);
    host.live.status.ready = 0;
    assert(mm_preview_object_rows_capture(&rows, &host.live) == MM_STORAGE_OK && !rows.count);
    mm_preview_object_rows_dispose(&rows);
    assert(!host.memory);
}

int main(void)
{
    MMPreviewLive invalid;
    MMStorageAllocator bad;
    memset(&bad, 0, sizeof(bad));
    assert(!mm_preview_live_init(&invalid, &bad));
    reconciliation_tests();
    seek_tests();
    camera_tests();
    background_tests();
    object_snapshot_tests();
    puts("C89 live Preview: definitions/actor ownership, retained prefixes, cue edits, seek failures and allocation "
         "recovery passed.");
    return 0;
}
