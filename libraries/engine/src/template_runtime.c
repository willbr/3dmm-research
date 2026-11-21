/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/template_runtime.h"
#include "mm/movie_session.h"
#include <string.h>
void *mm_template_fetch_child(const MMResourceObject *resource, MMU32 id, int kind, MMResourceLoader reader,
                             MMMovieAsset *asset)
{
    MMChunkAccess graph;
    MMChunkEntry entry;
    MMMovieAsset resolved;
    MMU32 parent;
    void *object;
    if (!resource || !resource->cache || !reader.key || kind < 0 || kind >= MM_TEMPLATE_RESOURCE_COUNT)
        return NULL;
    graph = mm_resource_cache_chunks(resource->cache);
    if (mm_chunk_find(&graph, resource->type, resource->number, &parent) != MM_CHUNK_OK)
        return NULL;
    if (kind == MM_TEMPLATE_MODEL)
    {
        if (mm_movie_cel_model(&graph, parent, id, 0, &resolved) != MM_MOVIE_OK)
            return NULL;
    }
    else if (kind == MM_TEMPLATE_CUSTOM)
    {
        if (mm_movie_costume_link(&graph, parent, id, &entry, NULL) != MM_MOVIE_OK)
            return NULL;
        resolved.source = 0;
        resolved.type = entry.type;
        resolved.number = entry.number;
    }
    else
    {
        MMChunkChild child;
        if (mm_chunk_child_exact(&graph, parent, id, MM_ACTN, &child) != MM_CHUNK_OK)
            return NULL;
        resolved.source = 0;
        resolved.type = child.type;
        resolved.number = child.number;
    }
    object = mm_resource_cache_fetch_chunk(resource->cache, resolved.type, resolved.number, reader);
    if (object && asset)
        *asset = resolved;
    return object;
}

static void *fetch_action(void *object, MMI32 id, const MMTemplateRuntimeServices *s)
{
    if (s->generated_action)
        return s->generated_action(s->storage.context, object, id);
    return mm_template_fetch_child(s->resource, (MMU32)id, MM_TEMPLATE_ACTION, s->action_reader, NULL);
}

int mm_template_action_name(MMI32 id, void *out, const MMTemplateRuntimeServices *s)
{
    const MMResourceObject *resource = s->resource;
    MMChunkAccess graph;
    MMU32 number;
    if (!resource || !resource->cache)
        return 0;
    graph = mm_resource_cache_chunks(resource->cache);
    if (mm_chunk_link_number(&graph, resource->type, resource->number, (MMU32)id, MM_ACTN, &number) != MM_CHUNK_OK)
        return 0;
    return s->name(s->storage.context, resource->cache->owner, MM_ACTN, number, out);
}
MMI32 mm_template_wrap_cel(MMI32 cel, MMI32 count)
{
    MMI32 index = cel % count;
    return index < 0 ? index + count : index;
}

static MMI32 wrapped(void *action, MMI32 cel, const MMTemplateRuntimeServices *s)
{
    MMI32 count = s->cel_count(s->storage.context, action);
    return count > 0 ? mm_template_wrap_cel(cel, count) : -1;
}

typedef struct TemplateCelPart
{
    MMMovieCelPart binding;
    void *model;
} TemplateCelPart;

typedef struct TemplateCel
{
    const MMTemplateRuntimeServices *services;
    void *object, *action;
    TemplateCelPart *parts;
    MMI32 index;
} TemplateCel;

static void render_cel_indices(void *context, MMU32 part, MMI32 *model, MMI32 *matrix)
{
    const TemplateCel *cel = (const TemplateCel *)context;
    const MMTemplateRuntimeServices *s = cel->services;
    if (!s->part(s->storage.context, cel->action, cel->index, (MMI32)part, model, matrix))
    {
        *model = MM_TEMPLATE_KEEP_MODEL;
        *matrix = -1;
    }
}

static int render_cel_model(void *context, MMU32 part, MMU32 slot, MMMovieAsset *asset)
{
    TemplateCel *cel = (TemplateCel *)context;
    const MMTemplateRuntimeServices *s = cel->services;
    if (s->generated_model)
        cel->parts[part].model = s->generated_model(s->storage.context, cel->object, slot, asset);
    else
        cel->parts[part].model = mm_template_fetch_child(s->resource, slot, MM_TEMPLATE_MODEL, s->model_reader, asset);
    return cel->parts[part].model ? MM_MOVIE_OK : MM_MOVIE_ASSET_NOT_FOUND;
}

static int render_cel_matrix(void *context, MMI32 matrix, MMActorMatrix *out)
{
    const TemplateCel *cel = (const TemplateCel *)context;
    const MMTemplateRuntimeServices *s = cel->services;
    return s->matrix(s->storage.context, cel->action, matrix, out) ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

int mm_template_set_cel(void *object, void *body, MMI32 id, MMI32 cel, void *distance,
                        const MMTemplateRuntimeServices *s)
{
    const MMTemplateCreateServices *a = &s->storage;
    TemplateCel request;
    MMMovieCelRead read;
    MMTemplateState state;
    void *storage = NULL;
    MMI32 count, i, step;
    int result = 0;
    state = *a->state(a->context, object);
    count = state.parents->view.count;
    request.action = fetch_action(object, id, s);
    if (!request.action)
        return 0;
    request.services = s;
    request.object = object;
    request.index = wrapped(request.action, cel, s);
    if (request.index < 0 || !s->distance(a->context, request.action, request.index, &step))
        goto done;
    if (count < 0 || (MMU32)count > 0x7fffffffUL / sizeof(*request.parts) ||
        !s->allocate_cel(a->context, (size_t)count * sizeof(*request.parts), &storage))
        goto done;
    request.parts = (TemplateCelPart *)storage;
    read.context = &request;
    read.indices = render_cel_indices;
    read.model = render_cel_model;
    read.matrix = render_cel_matrix;
    for (i = 0; i < count; ++i)
        if (mm_movie_cel_part(&read, (MMU32)i, &request.parts[i].binding) != MM_MOVIE_OK)
            goto done;
    if (distance)
        memcpy(distance, &step, sizeof(step));
    for (i = 0; i < count; ++i)
    {
        const TemplateCelPart *part = &request.parts[i];
        if (part->binding.model.type)
            s->set_model(a->context, body, i, part->model);
        s->set_matrix(a->context, body, i, &part->binding.matrix);
    }
    result = 1;
done:
    if (storage)
    {
        TemplateCelPart *parts = (TemplateCelPart *)storage;
        for (i = 0; i < count; ++i)
            if (parts[i].model)
            {
                a->release(a->context, parts[i].model);
                parts[i].model = NULL;
            }
        a->free_memory(a->context, &storage);
    }
    a->release(a->context, request.action);
    return result;
}
int mm_template_distance(void *object,MMI32 id,MMI32 cel,MMI32 *out,const MMTemplateRuntimeServices *s)
{
    void *a=fetch_action(object,id,s); MMI32 index,d; int ok;
    if (!a) return 0;
    index=wrapped(a,cel,s);
    ok=index>=0 && s->distance(s->storage.context,a,index,&d);
    s->storage.release(s->storage.context,a);
    if (ok) *out=d;
    return ok;
}
int mm_template_cel_count(void *object,MMI32 id,void *out,const MMTemplateRuntimeServices *s)
{
    void *a=fetch_action(object,id,s); MMI32 count; if (!a) return 0;
    count=s->cel_count(s->storage.context,a); memcpy(out,&count,sizeof(count)); s->storage.release(s->storage.context,a); return 1;
}
int mm_template_action_flags(void *object,MMI32 id,void *out,const MMTemplateRuntimeServices *s)
{
    void *a=fetch_action(object,id,s); MMU32 flags; if (!a) return 0;
    flags=s->flags(s->storage.context,a); memcpy(out,&flags,sizeof(flags)); s->storage.release(s->storage.context,a); return 1;
}
int mm_template_sound(void *object,MMI32 id,MMI32 cel,void *exists,void *tag,const MMTemplateRuntimeServices *s)
{
    MMI32 index;
    void *a; s->sound_exists(s->storage.context,exists,0); a=fetch_action(object,id,s); if (!a) return 0;
    index=wrapped(a,cel,s);
    if (index>=0 && s->sound(s->storage.context,a,index,tag)) s->sound_exists(s->storage.context,exists,1);
    s->storage.release(s->storage.context,a); return index>=0;
}
MMI32 mm_template_costume_count(void *object,MMI32 set,const MMTemplateRuntimeServices *s)
{
    const MMTemplateState *state = s->storage.state(s->storage.context, object);
    return mm_template_costumes_count(state->costumes, set);
}
MMI32 mm_template_costume_id(void *object,MMI32 set,MMI32 index,const MMTemplateRuntimeServices *s)
{
    const MMTemplateState *state = s->storage.state(s->storage.context, object);
    MMI32 id = -1;
    mm_template_costumes_id(state->costumes, set, index, &id);
    return id;
}
int mm_template_has_models(const MMChunkAccess *graph, MMU32 type, MMU32 number)
{
    MMU32 model;
    return mm_chunk_link_number(graph, type, number, 0, MM_TEMPLATE_BMDL, &model) == MM_CHUNK_OK;
}
int mm_template_equal_models(const MMChunkAccess *graph, MMU32 first, MMU32 second)
{
    MMU32 id = 0, a, b;
    while (mm_chunk_link_number(graph, MM_CMTL, first, id, MM_TEMPLATE_BMDL, &a) == MM_CHUNK_OK)
    {
        if (mm_chunk_link_number(graph, MM_CMTL, second, id, MM_TEMPLATE_BMDL, &b) != MM_CHUNK_OK || a != b)
            return 0;
        ++id;
    }
    return mm_chunk_link_number(graph, MM_CMTL, second, id, MM_TEMPLATE_BMDL, &b) != MM_CHUNK_OK;
}
int mm_template_is_accessory(void *object,MMI32 set,const MMTemplateRuntimeServices *s)
{
    const MMResourceObject *resource = s->resource;
    MMChunkAccess graph;
    MMU32 costume;
    MMI32 id;
    if (!resource || !resource->cache)
        return 0;
    id = mm_template_costume_id(object, set, 0, s);
    graph = mm_resource_cache_chunks(resource->cache);
    if (mm_chunk_link_number(&graph, resource->type, resource->number, (MMU32)id, MM_CMTL, &costume) != MM_CHUNK_OK)
        return 0;
    return mm_template_has_models(&graph, MM_CMTL, costume);
}
int mm_template_accessory_set(void *object,MMI32 set,void *out,const MMTemplateRuntimeServices *s)
{
    MMTemplateState t; MMI32 candidate,part,parent; t=*s->storage.state(s->storage.context,object);
    if (mm_template_is_accessory(object,set,s)) { memcpy(out,&set,sizeof(set)); return 1; }
    for (candidate=0;candidate<t.set_count;++candidate) if (mm_template_is_accessory(object,candidate,s))
        for (part=0;part<t.parents->view.count;++part)
            if (mm_part_index_get(&t.sets->view,part)==candidate)
            {
                parent=mm_part_index_get(&t.parents->view,part);
                if (mm_part_index_get(&t.sets->view,parent)==set) { memcpy(out,&candidate,sizeof(candidate)); return 1; }
            }
    return 0;
}
int mm_template_same_accessory(MMI32 first,MMI32 second,const MMTemplateRuntimeServices *s)
{
    const MMResourceObject *resource = s->resource;
    MMChunkAccess graph;
    MMU32 a, b;
    if (!resource || !resource->cache)
        return 0;
    graph = mm_resource_cache_chunks(resource->cache);
    if (mm_chunk_link_number(&graph, resource->type, resource->number, (MMU32)first, MM_CMTL, &a) != MM_CHUNK_OK ||
        mm_chunk_link_number(&graph, resource->type, resource->number, (MMU32)second, MM_CMTL, &b) != MM_CHUNK_OK)
        return 0;
    return mm_template_equal_models(&graph, a, b);
}
