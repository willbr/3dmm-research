#include "mm/preview_live.h"
#include "mm/movie_session.h"
#include "mm/scene_runtime.h"
#include <string.h>
#define MIN_FRAME (-2147483647 - 1)
static void array_init(MMPreviewLiveArray *a, MMU32 width, const MMStorageAllocator *allocator)
{
    memset(a, 0, sizeof(*a));
    mm_storage_init(&a->storage, allocator);
    a->width = width;
}
static void *data(const MMPreviewLiveArray *a)
{
    return a->storage.buffers[0].data;
}
static MMArray view(MMPreviewLiveArray *a)
{
    MMArray v;
    memset(&v, 0, sizeof(v));
    v.storage = &a->storage;
    v.width = a->width;
    v.count = a->count;
    v.free_count = -1;
    return v;
}
static int reserve(MMPreviewLiveArray *a, MMU32 count)
{
    MMArray v = view(a);
    if (count <= a->count)
        return 1;
    return mm_array_reserve(&v, count - a->count, 0) == MM_STORAGE_OK;
}
static int add(MMPreviewLiveArray *a, const void *value)
{
    MMArray v = view(a);
    MMU32 extra = a->storage.buffers[0].capacity / 2;
    int ok;
    if (extra < a->width)
        extra = a->width * 8;
    a->storage.min_grow[0] = extra;
    ok = mm_array_add(&v, value, 0) == MM_STORAGE_OK;
    a->count = v.count;
    return ok;
}
static void erase(MMPreviewLiveArray *a, MMU32 index)
{
    MMArray v = view(a);
    mm_array_delete(&v, index, 1);
    a->count = v.count;
}
static int copy(MMPreviewLiveArray *to, const MMPreviewLiveArray *from)
{
    if (!reserve(to, from->count))
        return 0;
    if (from->count)
        memcpy(data(to), data(from), from->count * from->width);
    to->count = from->count;
    return 1;
}
static void swap(MMPreviewLiveArray *a, MMPreviewLiveArray *b)
{
    MMPreviewLiveArray temp = *a;
    *a = *b;
    *b = temp;
}
static void release_prefix(MMPreviewLiveArray *a, MMU32 first, const MMPreviewLiveServices *s)
{
    MMU32 i;
    void **actors = (void **)data(a);
    for (i = first; i < a->count; ++i)
        s->actor_release(s->context, actors[i]);
    a->count = first;
}
static void publish(const MMPreviewLive *p, const MMPreviewLiveServices *s)
{
    if (s->publish)
        s->publish(s->context, p);
}
int mm_preview_live_init(MMPreviewLive *p, const MMStorageAllocator *a)
{
    memset(p, 0, sizeof(*p));
    if (a && (!a->resize || !a->release))
        return 0;
    array_init(&p->scenes, sizeof(MMPreviewLiveScene), a);
    array_init(&p->props, sizeof(MMPreviewLiveProp), a);
    array_init(&p->actors, sizeof(void *), a);
    array_init(&p->cameras, sizeof(MMPreviewLiveCamera), a);
    array_init(&p->original_cameras, sizeof(MMPreviewLiveCamera), a);
    return 1;
}
void mm_preview_live_clear(MMPreviewLive *p, const MMPreviewLiveServices *s)
{
    release_prefix(&p->actors, 0, s);
    p->original_actors = p->initial_camera = 0;
    p->cameras.count = p->original_cameras.count = 0;
    p->virtual_scene = 0;
    memset(&p->status, 0, sizeof(p->status));
    memset(&p->publication, 0, sizeof(p->publication));
}
void mm_preview_live_dispose(MMPreviewLive *p, const MMPreviewLiveServices *s)
{
    mm_preview_live_clear(p, s);
    mm_storage_dispose(&p->original_cameras.storage);
    mm_storage_dispose(&p->cameras.storage);
    mm_storage_dispose(&p->actors.storage);
    mm_storage_dispose(&p->props.storage);
    mm_storage_dispose(&p->scenes.storage);
    p->scenes.count = p->props.count = 0;
}
void *mm_preview_live_actor(const MMPreviewLive *p, MMU32 index)
{
    return index < p->actors.count ? ((void **)data(&p->actors))[index] : 0;
}
const MMPreviewLiveScene *mm_preview_live_scene(const MMPreviewLive *p, MMU32 number)
{
    MMU32 i;
    const MMPreviewLiveScene *scenes = (const MMPreviewLiveScene *)data(&p->scenes);
    for (i = 0; i < p->scenes.count; ++i)
        if (scenes[i].number == number)
            return scenes + i;
    return 0;
}
int mm_preview_live_adopt(MMPreviewLive *p, void *actor)
{
    return add(&p->actors, &actor) ? MM_PREVIEW_LIVE_OK : MM_PREVIEW_LIVE_MEMORY;
}
int mm_preview_live_camera_add(MMPreviewLive *p, MMI32 frame, MMU32 camera)
{
    MMPreviewLiveCamera c;
    c.frame = frame;
    c.camera = camera;
    return add(&p->cameras, &c) ? MM_PREVIEW_LIVE_OK : MM_PREVIEW_LIVE_MEMORY;
}
int mm_preview_live_seal(MMPreviewLive *p)
{
    p->original_actors = p->actors.count;
    return copy(&p->original_cameras, &p->cameras) ? MM_PREVIEW_LIVE_OK : MM_PREVIEW_LIVE_MEMORY;
}
void mm_preview_live_background_loaded(MMPreviewLive *p, MMU32 count, MMU32 camera)
{
    p->status.background = 1;
    p->status.camera_count = count;
    p->status.current_camera = camera;
}

int mm_preview_live_replace_background(MMPreviewLive *p, const MMPreviewLiveBackgroundServices *s)
{
    MMU32 count = 0, camera;
    int result;
    if (!p->status.ready || !p->status.scene)
        return MM_PREVIEW_LIVE_SCENE;
    result = s->load(s->context, &count);
    if (result <= 0 || !count)
        return result == -1 ? MM_PREVIEW_LIVE_MEMORY : result < 0 ? MM_PREVIEW_LIVE_EXCEPTION : MM_PREVIEW_LIVE_BACKGROUND;
    camera = p->status.current_camera < count ? p->status.current_camera : count - 1;
    result = s->select(s->context, camera);
    if (result <= 0)
        return result == -1 ? MM_PREVIEW_LIVE_MEMORY : result < 0 ? MM_PREVIEW_LIVE_EXCEPTION : MM_PREVIEW_LIVE_CAMERA;
    s->commit(s->context);
    mm_preview_live_background_loaded(p, count, camera);
    return MM_PREVIEW_LIVE_OK;
}

int mm_preview_live_sync(MMPreviewLive *p, const MMPreviewLiveServices *s)
{
    MMPreviewLiveArray desired, added;
    MMPreviewLiveProp *props = (MMPreviewLiveProp *)data(&p->props);
    const MMPreviewLiveProp **items;
    const MMStorageAllocator *a = &p->actors.storage.allocator;
    MMU32 i, kept = 0;
    int result = MM_PREVIEW_LIVE_OK, effect;
    void *actor;
    array_init(&desired, sizeof(MMPreviewLiveProp *), a);
    array_init(&added, sizeof(void *), a);
    for (i = 0; i < p->props.count; ++i)
        if (props[i].scene == p->status.asset_number)
        {
            const MMPreviewLiveProp *prop = props + i;
            if (!add(&desired, &prop))
            {
                result = MM_PREVIEW_LIVE_MEMORY;
                goto done;
            }
        }
    items = (const MMPreviewLiveProp **)data(&desired);
    while (kept < desired.count && p->original_actors + kept < p->actors.count &&
           s->actor_number(s->context, mm_preview_live_actor(p, p->original_actors + kept)) == items[kept]->number)
        ++kept;
    for (i = kept; i < desired.count; ++i)
    {
        actor = s->actor_new(s->context);
        if (!actor)
        {
            result = MM_PREVIEW_LIVE_MEMORY;
            goto done;
        }
        effect = s->actor_create(s->context, actor, items[i]);
        if (effect > 0)
            effect = s->actor_seek(s->context, actor, p->status.frame);
        if (effect <= 0)
        {
            s->actor_release(s->context, actor);
            result = effect == -2   ? MM_PREVIEW_LIVE_EXCEPTION
                     : effect == -1 ? MM_PREVIEW_LIVE_MEMORY
                                    : MM_PREVIEW_LIVE_CREATE;
            goto done;
        }
        if (!add(&added, &actor))
        {
            s->actor_release(s->context, actor);
            result = MM_PREVIEW_LIVE_MEMORY;
            goto done;
        }
    }
    if (desired.count > (MMU32)-1 - p->original_actors || !reserve(&p->actors, p->original_actors + desired.count))
    {
        result = MM_PREVIEW_LIVE_MEMORY;
        goto done;
    }
    release_prefix(&p->actors, p->original_actors + kept, s);
    if (added.count)
        memcpy((void **)data(&p->actors) + p->actors.count, data(&added), added.count * sizeof(void *));
    p->actors.count += added.count;
    added.count = 0;
    p->status.reported_actors = p->actors.count;
    publish(p, s);
done:
    release_prefix(&added, 0, s);
    mm_storage_dispose(&added.storage);
    mm_storage_dispose(&desired.storage);
    return result;
}
int mm_preview_live_definitions(MMPreviewLive *p, const MMPreviewLiveDefinitions *d, const MMPreviewLiveServices *s)
{
    MMPreviewLiveArray scenes, props;
    MMPreviewLiveScene scene;
    MMPreviewLiveProp prop;
    MMU32 i;
    int result = MM_PREVIEW_LIVE_MEMORY;
    array_init(&scenes, sizeof(scene), &p->scenes.storage.allocator);
    array_init(&props, sizeof(prop), &p->props.storage.allocator);
    if (!copy(&scenes, &p->scenes) || !copy(&props, &p->props))
        goto done;
    if (!reserve(&p->scenes, d->scene_count))
        goto done;
    for (i = 0; i < d->scene_count; ++i)
    {
        d->scene(d->context, i, &scene);
        memcpy((MMPreviewLiveScene *)data(&p->scenes) + i, &scene, sizeof(scene));
    }
    p->scenes.count = d->scene_count;
    if (!reserve(&p->props, d->prop_count))
        goto done;
    for (i = 0; i < d->prop_count; ++i)
    {
        d->prop(d->context, i, &prop);
        memcpy((MMPreviewLiveProp *)data(&p->props) + i, &prop, sizeof(prop));
    }
    p->props.count = d->prop_count;
    result = MM_PREVIEW_LIVE_OK;
    if (p->virtual_scene && !mm_preview_live_scene(p, p->status.asset_number))
    {
        mm_preview_live_clear(p, s);
        s->clear_asset(s->context);
    }
    else if (p->status.ready && p->status.scene)
    {
        result = mm_preview_live_sync(p, s);
        if (result == MM_PREVIEW_LIVE_CREATE)
        {
            swap(&p->scenes, &scenes);
            swap(&p->props, &props);
        }
    }
    publish(p, s);
done:
    mm_storage_dispose(&props.storage);
    mm_storage_dispose(&scenes.storage);
    return result;
}
static MMI32 camera_count(void *v)
{
    return (MMI32)((MMPreviewLive *)v)->cameras.count;
}
static void camera_event(void *v, MMI32 i, MMI32 *frame, int *kind)
{
    *frame = ((MMPreviewLiveCamera *)data(&((MMPreviewLive *)v)->cameras))[i].frame;
    *kind = MM_SCENE_CHANGE_CAMERA;
}
static void move_camera(void *v, MMI32 i, MMI32 frame)
{
    MMPreviewLiveArray *a = &((MMPreviewLive *)v)->cameras;
    MMArray list = view(a);
    ((MMPreviewLiveCamera *)data(a))[i].frame = frame;
    mm_array_move(&list, (MMU32)i, 0);
}
int mm_preview_live_range(MMPreviewLive *p, MMI32 first, MMI32 last)
{
    MMSceneState scene;
    MMSceneRuntimeServices s;
    if (!p->status.ready || !p->status.scene || first == MIN_FRAME || first > p->original_first ||
        last < p->original_last)
        return MM_PREVIEW_LIVE_RANGE;
    if (first == p->status.first && last == p->status.last)
        return MM_PREVIEW_LIVE_OK;
    memset(&scene, 0, sizeof(scene));
    memset(&s, 0, sizeof(s));
    scene.firstFrame = p->status.first;
    s.context = p;
    s.event_count = camera_count;
    s.event = camera_event;
    s.move_camera = move_camera;
    if (first != p->status.first)
        mm_scene_move_first(&scene, first, &s);
    p->status.first = first;
    p->status.last = last;
    return MM_PREVIEW_LIVE_OK;
}
static void sort_cameras(MMPreviewLive *p)
{
    MMPreviewLiveArray scratch;
    MMPreviewLiveCamera *a = (MMPreviewLiveCamera *)data(&p->cameras), *b, value;
    MMU32 n = p->cameras.count, width, start, mid, end, i, j, k;
    if (n < 2)
        return;
    array_init(&scratch, sizeof(value), &p->cameras.storage.allocator);
    if (!reserve(&scratch, n))
    {
        for (i = 1; i < n; ++i)
        {
            value = a[i];
            j = i;
            while (j && value.frame < a[j - 1].frame)
            {
                a[j] = a[j - 1];
                --j;
            }
            a[j] = value;
        }
    }
    else
    {
        b = (MMPreviewLiveCamera *)data(&scratch);
        for (width = 1; width < n; width *= 2)
        {
            for (start = 0; start < n; start = end)
            {
                mid = start + width;
                if (mid > n)
                    mid = n;
                end = mid + width;
                if (end > n)
                    end = n;
                i = start;
                j = mid;
                for (k = start; k < end; ++k)
                    if (i < mid && (j == end || a[i].frame <= a[j].frame))
                        b[k] = a[i++];
                    else
                        b[k] = a[j++];
            }
            memcpy(a, b, n * sizeof(*a));
        }
    }
    mm_storage_dispose(&scratch.storage);
}
int mm_preview_live_cameras(MMPreviewLive *p, const MMPreviewLiveCameraEdits *edits, const MMPreviewLiveServices *s)
{
    MMU32 i, j;
    MMI32 first = p->status.first, last = p->status.last, frame;
    MMPreviewLiveCamera edit;
    int result;
    if (!p->status.ready || !p->status.scene)
        return MM_PREVIEW_LIVE_SCENE;
    for (i = 0; i < edits->count; ++i)
    {
        edits->get(edits->context, i, &edit);
        if (edit.camera >= p->status.camera_count)
            return MM_PREVIEW_LIVE_CAMERA_INDEX;
    }
    if (!copy(&p->cameras, &p->original_cameras))
        return MM_PREVIEW_LIVE_MEMORY;
    p->status.first = p->original_first;
    p->status.last = p->original_last;
    result = mm_preview_live_range(p, first, last);
    if (result != MM_PREVIEW_LIVE_OK)
        return result;
    for (i = 0; i < edits->count; ++i)
    {
        edits->get(edits->context, i, &edit);
        frame = edit.frame == MIN_FRAME ? first : edit.frame;
        for (j = 0; j < p->cameras.count;)
            if (((MMPreviewLiveCamera *)data(&p->cameras))[j].frame == frame)
                erase(&p->cameras, j);
            else
                ++j;
        edit.frame = frame;
        if (!add(&p->cameras, &edit))
        {
            publish(p, s);
            return MM_PREVIEW_LIVE_MEMORY;
        }
    }
    sort_cameras(p);
    frame = p->status.frame < last ? p->status.frame : last;
    if (frame < first)
        frame = first;
    return mm_preview_live_seek(p, frame, s);
}
static void read_camera_change(void *context, void *group, MMI32 index, MMI32 *frame, int *kind)
{
    const MMPreviewLiveCamera *changes = (const MMPreviewLiveCamera *)group;
    (void)context;
    *frame = changes[index].frame;
    *kind = MM_SCENE_CHANGE_CAMERA;
}

typedef struct PreviewSeek
{
    MMPreviewLive *live;
    const MMPreviewLiveServices *services;
    MMU32 initial_camera;
} PreviewSeek;

static int begin_seek(void *context, MMI32 frame)
{
    const PreviewSeek *seek = (const PreviewSeek *)context;
    const MMPreviewLive *p = seek->live;
    const MMPreviewLiveServices *s = seek->services;
    if (s->begin_seek)
        s->begin_seek(s->context);
    if (!p->status.ready || !p->status.timeline || frame < p->status.first || frame > p->status.last)
        return MM_PREVIEW_LIVE_FRAME;
    return MM_PREVIEW_LIVE_OK;
}

static MMU32 seek_actor_count(void *context)
{
    return ((const PreviewSeek *)context)->live->actors.count;
}

static int seek_actor(void *context, MMU32 ordinal, MMI32 frame)
{
    const PreviewSeek *seek = (const PreviewSeek *)context;
    MMPreviewLive *p = seek->live;
    const MMPreviewLiveServices *s = seek->services;
    int effect = s->actor_seek(s->context, mm_preview_live_actor(p, ordinal), frame);
    if (effect > 0 && s->healthy && !s->healthy(s->context))
        effect = 0;
    if (effect <= 0)
    {
        if (effect != -2)
            p->status.ready = 0;
        publish(p, s);
        return effect == -2   ? MM_PREVIEW_LIVE_EXCEPTION
               : effect == -1 ? MM_PREVIEW_LIVE_SEEK_MEMORY
                              : MM_PREVIEW_LIVE_SEEK;
    }
    return MM_PREVIEW_LIVE_OK;
}

static int finish_seek(void *context, MMI32 frame)
{
    const PreviewSeek *seek = (const PreviewSeek *)context;
    MMPreviewLive *p = seek->live;
    const MMPreviewLiveServices *s = seek->services;
    MMU32 camera = seek->initial_camera;
    MMMovieEventView view;
    MMI32 selected;
    int effect;
    if (p->status.background)
    {
        view.context = NULL;
        view.group = data(&p->cameras);
        view.count = (MMI32)p->cameras.count;
        view.read = read_camera_change;
        selected = mm_movie_view_last_event(&view, MM_SCENE_CHANGE_CAMERA, frame, 1, NULL);
        if (selected >= 0)
            camera = ((const MMPreviewLiveCamera *)view.group)[selected].camera;
        effect = s->camera(s->context, camera);
        if (effect <= 0)
        {
            if (effect == -1)
                p->status.ready = 0;
            publish(p, s);
            return effect == -2   ? MM_PREVIEW_LIVE_EXCEPTION
                   : effect == -1 ? MM_PREVIEW_LIVE_SEEK_MEMORY
                                  : MM_PREVIEW_LIVE_CAMERA;
        }
        p->status.current_camera = camera;
    }
    return MM_PREVIEW_LIVE_OK;
}

static void published_seek(void *context)
{
    const PreviewSeek *seek = (const PreviewSeek *)context;
    const MMPreviewLiveServices *s = seek->services;
    MMPreviewLive *p = seek->live;
    publish(p, s);
    s->stats(s->context);
}

int mm_preview_live_seek(MMPreviewLive *p, MMI32 frame, const MMPreviewLiveServices *s)
{
    PreviewSeek seek;
    MMMovieFrameServices services;
    seek.live = p;
    seek.services = s;
    seek.initial_camera = p->initial_camera;
    services.context = &seek;
    services.begin = begin_seek;
    services.actor_count = seek_actor_count;
    services.seek_actor = seek_actor;
    services.finish = finish_seek;
    services.position = &p->status.frame;
    services.published = published_seek;
    return mm_movie_session_seek_live(s->movie, &p->publication, frame, &services);
}
