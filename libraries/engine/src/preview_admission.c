#include "mm/preview_admission.h"
#include "mm/archive_reader.h"
#include "mm/scene_data.h"
#include "mm/wide.h"
#include <string.h>
int mm_preview_budget(const MMPreviewBudgetServices *s, MMChunkEntry *failed)
{
    MMU32 i, total = 0, decoded;
    MMChunkEntry c;
    unsigned char h[8];
    for (i = 0; i < s->chunks.count; ++i)
    {
        if (mm_chunk_at(&s->chunks, i, &c) != MM_CHUNK_OK)
            return MM_PREVIEW_BUDGET_HEADER;
        decoded = c.bytes;
        if (c.flags & MM_CHUNK_PACKED)
        {
            if (c.bytes < 8 || !s->header(s->context, &c, h))
                return MM_PREVIEW_BUDGET_HEADER;
            decoded = mm_chunk_packed_decoded_length(h);
        }
        if (c.bytes > 64u * 1024 * 1024 || decoded > 64u * 1024 * 1024 || decoded > 512u * 1024 * 1024 - total)
        {
            *failed = c;
            return MM_PREVIEW_BUDGET_LIMIT;
        }
        total += decoded;
    }
    return MM_PREVIEW_BUDGET_OK;
}

static int archive_packed_header(void *context, const MMChunkEntry *entry, unsigned char *bytes)
{
    return mm_archive_reader_read_stored((const MMArchiveReader *)context, entry, bytes, 8);
}

int mm_preview_archive_budget(const MMArchiveReader *reader, MMChunkEntry *failed)
{
    MMPreviewBudgetServices s;
    s.context = (void *)reader;
    s.chunks = mm_archive_reader_access(reader);
    s.header = archive_packed_header;
    return mm_preview_budget(&s, failed);
}
static void publish(MMPreviewAdmission *p, const MMPreviewAdmissionServices *s)
{
    p->live->status.asset_type = p->type;
    p->live->status.asset_number = p->number;
    s->publish(s->context, p);
}
static void clear(MMPreviewAdmission *p, int preserve, const MMPreviewAdmissionServices *s)
{
    s->clear(s->context, preserve);
    p->tmpl = p->body = p->model = 0;
    p->live->status.ready = p->live->status.prop = 0;
    p->action_count = 0;
}
static int effect(int r)
{
    return r == -1 ? MM_PREVIEW_ADMIT_MEMORY : MM_PREVIEW_ADMIT_EXCEPTION;
}
static int template_load(MMPreviewAdmission *p, const MMPreviewAdmissionServices *s)
{
    MMU32 i, j, n;
    MMI32 count, name;
    MMPreviewChunk child, c;
    MMActorMatrix rest;
    unsigned short angles[3];
    int r = s->template_fetch(s->context, p->type, p->number, &p->tmpl);
    if (r <= 0)
        return r;
    r = s->template_bind(s->context, p->tmpl);
    if (r <= 0)
        return r;
    r = s->body_create(s->context, p->tmpl, &p->body);
    if (r < 0)
        return r;
    p->live->status.prop = s->prop(s->context, p->tmpl);
    publish(p, s);
    for (i = 0; (MMI32)i < s->actions(s->context, p->tmpl); ++i)
    {
        count = 0;
        r = s->cels(s->context, p->tmpl, i, &count);
        if (r < 0)
            return r;
        if (!r || count <= 0)
            break;
        name = -1;
        if (s->child(s->context, p->type, p->number, i, 0, &child))
        {
            n = s->chunks(s->context);
            for (j = 0; j < n; ++j)
            {
                s->chunk(s->context, j, &c);
                if (c.type == child.type && c.number == child.number && c.named)
                {
                    name = (MMI32)j;
                    break;
                }
            }
        }
        r = s->action_add(s->context, i, (MMU32)count, name);
        if (r <= 0)
            return r;
        ++p->action_count;
    }
    if (!p->body || !p->action_count)
        return 0;
    r = s->pose(s->context, p->tmpl, p->body, 0, 0);
    if (r <= 0)
        return r;
    s->rest(s->context, p->tmpl, angles);
    s->rotate(s->context, &rest, 0, angles[0], 0);
    s->rotate(s->context, &rest, 1, angles[1], 1);
    s->rotate(s->context, &rest, 2, angles[2], 1);
    s->locate(s->context, p->body, &rest);
    return 1;
}
static int scene_load(MMPreviewAdmission *p, const MMPreviewAdmissionServices *s)
{
    MMCollectionStorage storage;
    MMArray actors;
    MMMovieSceneGroup group;
    MMMovieSessionScene header;
    MMMovieSceneEvent event;
    MMPreviewAdmissionTag tag;
    MMPreviewChunk child;
    MMPreviewLive *live = p->live;
    const MMMovieSceneReader *reader = &s->scene;
    MMU32 i, word;
    MMI32 first, last;
    void *actor = 0;
    int r, result = MM_PREVIEW_ADMIT_OK, has_background = 0;
    memset(&actors, 0, sizeof(actors));
    memset(&group, 0, sizeof(group));
    memset(&tag, 0, sizeof(tag));
    mm_storage_init(&storage, s->allocator);
    actors.storage = &storage;
    actors.width = sizeof(MMU32);
    actors.free_count = -1;
    if (p->kind == MM_PREVIEW_SCENE)
    {
        r = reader->header(reader->context, p->number, &header);
        if (r != MM_MOVIE_OK)
        {
            result = r < 0 ? effect(r) : r == MM_MOVIE_MEMORY ? MM_PREVIEW_ADMIT_MEMORY : MM_PREVIEW_ADMIT_SCENE_HEADER;
            goto done;
        }
        live->status.scene = 1;
        live->original_first = live->status.first = header.first_frame;
        live->original_last = live->status.last = header.last_frame;
        publish(p, s);
        r = reader->group(reader->context, p->number, 1, &group);
        if (r != MM_MOVIE_OK)
        {
            result = r < 0                          ? effect(r)
                     : r == MM_MOVIE_MEMORY            ? MM_PREVIEW_ADMIT_MEMORY
                     : r == MM_MOVIE_ASSET_NOT_FOUND ? MM_PREVIEW_ADMIT_START_MISSING
                                                     : MM_PREVIEW_ADMIT_START_READ;
            goto done;
        }
        for (i = 0; i < group.count; ++i)
        {
            reader->event(reader->context, &group, i, &event);
            if (event.kind == MM_SCENE_ADD_ACTOR)
            {
                word = mm_movie_scene_event_word(&event, 0);
                if (!s->child(s->context, p->type, p->number, word, 1, &child))
                {
                    p->detail = word;
                    p->mark_chunk = 1;
                    result = MM_PREVIEW_ADMIT_ACTOR_CHILD;
                    goto done;
                }
                if (mm_array_add(&actors, &child.number, 0) != MM_STORAGE_OK)
                {
                    result = MM_PREVIEW_ADMIT_MEMORY;
                    goto done;
                }
            }
            else if (event.kind == MM_SCENE_SET_BACKGROUND)
            {
                tag.source = mm_signed_word(mm_movie_scene_event_word(&event, 0));
                tag.type = mm_movie_scene_event_word(&event, 8);
                tag.number = mm_movie_scene_event_word(&event, 12);
                has_background = 1;
            }
            else if (event.kind == MM_SCENE_CHANGE_CAMERA)
                live->initial_camera = mm_movie_scene_event_word(&event, 0);
        }
        reader->close(reader->context, &group);
        if (has_background)
        {
            r = s->background(s->context, &tag, 0);
            if (r <= 0)
            {
                result = r < 0 ? effect(r) : MM_PREVIEW_ADMIT_BACKGROUND;
                goto done;
            }
        }
        r = reader->group(reader->context, p->number, 0, &group);
        if (r != MM_MOVIE_OK)
        {
            result = r < 0                          ? effect(r)
                     : r == MM_MOVIE_MEMORY            ? MM_PREVIEW_ADMIT_MEMORY
                     : r == MM_MOVIE_ASSET_NOT_FOUND ? MM_PREVIEW_ADMIT_FRAME_MISSING
                                                     : MM_PREVIEW_ADMIT_FRAME_READ;
            goto done;
        }
        for (i = 0; i < group.count; ++i)
        {
            reader->event(reader->context, &group, i, &event);
            if (event.kind == MM_SCENE_CHANGE_CAMERA &&
                mm_preview_live_camera_add(live, event.frame, mm_movie_scene_event_word(&event, 0)) != MM_PREVIEW_LIVE_OK)
            {
                result = MM_PREVIEW_ADMIT_MEMORY;
                goto done;
            }
        }
        reader->close(reader->context, &group);
    }
    else
    {
        r = s->actor_header(s->context, p->number, &first, &last);
        if (r <= 0)
        {
            result = r < 0 ? effect(r) : MM_PREVIEW_ADMIT_ACTOR_HEADER;
            goto done;
        }
        if (first == 2147483647)
        {
            result = MM_PREVIEW_ADMIT_OFFSTAGE;
            goto done;
        }
        live->status.first = first;
        live->status.last = last;
        publish(p, s);
        if (mm_array_add(&actors, &p->number, 0) != MM_STORAGE_OK)
        {
            result = MM_PREVIEW_ADMIT_MEMORY;
            goto done;
        }
    }
    for (i = 0; i < actors.count; ++i)
    {
        r = s->actor_load(s->context, ((MMU32 *)storage.buffers[0].data)[i], &actor);
        if (r <= 0)
        {
            result = r < 0 ? effect(r) : MM_PREVIEW_ADMIT_ACTOR_LOAD;
            if (!r)
            {
                r = s->save_actor_error(s->context);
                if (r < 0)
                    result = effect(r);
                else
                    clear(p, 0, s);
            }
            goto done;
        }
        if (mm_preview_live_adopt(live, actor) != MM_PREVIEW_LIVE_OK)
        {
            result = MM_PREVIEW_ADMIT_MEMORY;
            goto done;
        }
        actor = 0;
    }
    if (p->kind == MM_PREVIEW_ACTOR)
        s->actor_range(s->context, mm_preview_live_actor(live, 0), &live->status.first, &live->status.last);
    if (mm_preview_live_seal(live) != MM_PREVIEW_LIVE_OK)
    {
        result = MM_PREVIEW_ADMIT_MEMORY;
        goto done;
    }
    publish(p, s);
    if (p->kind == MM_PREVIEW_SCENE)
    {
        r = s->sync(s->context);
        if (r <= 0)
        {
            result = r < 0 ? effect(r) : MM_PREVIEW_ADMIT_EXTERNAL;
            goto done;
        }
    }
    live->status.timeline = 1;
    live->status.reported_actors = live->actors.count;
    live->status.ready = 1;
    publish(p, s);
    r = s->seek(s->context, live->status.first);
    if (r < 0)
        result = effect(r);
    else if (!r)
        result = MM_PREVIEW_ADMIT_DEPENDENCIES;
done:
    if (group.handle)
        reader->close(reader->context, &group);
    if (actor)
        s->actor_release(s->context, actor);
    mm_storage_dispose(&storage);
    return result;
}
int mm_preview_admit(MMPreviewAdmission *p, int preserve, const MMPreviewAdmissionServices *s)
{
    const MMPreviewLiveScene *definition;
    MMPreviewAdmissionTag tag;
    int r, result = MM_PREVIEW_ADMIT_OK;
    s->begin(s->context);
    clear(p, preserve, s);
    p->mark_chunk = 0;
    publish(p, s);
    definition = p->kind == MM_PREVIEW_SCENE ? mm_preview_live_scene(p->live, p->number) : 0;
    if (definition)
    {
        tag.source = definition->source;
        tag.type = 0;
        tag.number = definition->background;
        r = s->background(s->context, &tag, 1);
        if (r <= 0)
        {
            result = r < 0 ? effect(r) : MM_PREVIEW_ADMIT_VIRTUAL_BACKGROUND;
            goto done;
        }
        p->live->status.scene = p->live->status.timeline = 1;
        p->live->status.first = p->live->status.last = p->live->status.frame = 1;
        p->live->original_first = p->live->original_last = 1;
        p->live->virtual_scene = p->live->status.ready = 1;
        publish(p, s);
        r = s->sync(s->context);
        if (r > 0)
            r = s->seek(s->context, 1);
        if (r <= 0)
        {
            result = r < 0 ? effect(r) : MM_PREVIEW_ADMIT_EXTERNAL;
            goto done;
        }
        s->stats(s->context);
        return MM_PREVIEW_ADMIT_OK;
    }
    if (!p->cache)
        return MM_PREVIEW_ADMIT_CACHE;
    if (p->kind == MM_PREVIEW_TEMPLATE)
        r = template_load(p, s);
    else if (p->kind == MM_PREVIEW_MODEL)
        r = s->model_fetch(s->context, p->type, p->number, &p->model);
    else if (p->kind == MM_PREVIEW_BACKGROUND)
        r = s->background(s->context, 0, 0);
    else if (p->kind == MM_PREVIEW_SCENE || p->kind == MM_PREVIEW_ACTOR)
    {
        result = scene_load(p, s);
        if (result != MM_PREVIEW_ADMIT_OK && result != MM_PREVIEW_ADMIT_DEPENDENCIES)
            goto done;
        r = result == MM_PREVIEW_ADMIT_OK;
    }
    else
        return MM_PREVIEW_ADMIT_UNSUPPORTED;
    if (r < 0)
    {
        result = effect(r);
        goto done;
    }
    if (!r || !s->healthy(s->context))
    {
        clear(p, 0, s);
        p->mark_chunk = 1;
        return MM_PREVIEW_ADMIT_DEPENDENCIES;
    }
    p->live->status.ready = 1;
    publish(p, s);
    s->stats(s->context);
done:
    if (result == MM_PREVIEW_ADMIT_MEMORY)
        clear(p, 0, s);
    return result;
}
int mm_preview_admission_pose(MMPreviewAdmission *p, MMU32 action, MMU32 cel, const MMPreviewAdmissionServices *s)
{
    int r;
    s->begin(s->context);
    if (!p->live->status.ready || !p->tmpl || action >= p->action_count || cel >= s->action_cels(s->context, action))
        return MM_PREVIEW_ADMIT_POSE_RANGE;
    r = s->pose(s->context, p->tmpl, p->body, action, cel);
    if (r < 0)
        return effect(r);
    if (!r || !s->healthy(s->context))
    {
        p->live->status.ready = 0;
        publish(p, s);
        return MM_PREVIEW_ADMIT_POSE;
    }
    s->stats(s->context);
    return MM_PREVIEW_ADMIT_OK;
}
static void body_stats(const MMPreviewStatsServices *s, void *body, MMU32 *parts, MMU32 *triangles)
{
    MMI32 i, n = s->body_parts(s->context, body) - 2;
    void *model;
    for (i = 0; i < n; ++i)
        if ((model = s->part_model(s->context, body, i)) != 0)
        {
            ++*parts;
            *triangles += s->faces(s->context, model);
        }
}
void mm_preview_stats(const MMPreviewStatsServices *s, MMU32 *parts, MMU32 *triangles)
{
    MMU32 i;
    *parts = *triangles = 0;
    if (s->body)
        body_stats(s, s->body, parts, triangles);
    for (i = 0; i < s->actors; ++i)
        if (s->visible(s->context, i))
            body_stats(s, s->actor_body(s->context, i), parts, triangles);
    if (s->model)
    {
        *parts = 1;
        *triangles = s->faces(s->context, s->model);
    }
}
