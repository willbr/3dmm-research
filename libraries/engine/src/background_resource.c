/* Background resources converted from engine/bkgd.cpp and preview_background.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/background_resource.h"
#include "mm/string_wire.h"
#include <assert.h>
#include <stdlib.h>
#include "mm/movie_session.h"
#include "mm/chunky_file.h"
#include "mm/collections.h"
#include "mm/indexed_display.h"
#include "mm/wide.h"
#include <string.h>
#define MAXIMUM 2147483647
static MMU32 word(const unsigned char *p, int n, int big)
{
    MMU32 value = 0;
    int i;
    for (i = 0; i < n; ++i)
        value = (value << 8) | p[big ? i : n - 1 - i];
    return value;
}
static int order(const unsigned char *p)
{
    if (p[0] == 1 && !p[1])
        return 0;
    if (!p[0] && p[1] == 1)
        return 1;
    return -1;
}
int mm_background_header_read(const MMReadStream *r, MMBackgroundHeader *out)
{
    unsigned char bytes[8];
    MMBackgroundHeader d;
    int big;
    if (r->length != 8 || !mm_read_exact(r, 0, bytes, 8) || (big = order(bytes)) < 0)
        return 0;
    d.system = (unsigned)word(bytes + 2, 2, big);
    d.palette_base = bytes[4];
    *out = d;
    return 1;
}
int mm_background_sound_read(const MMReadStream *r, MMBackgroundSound *out)
{
    unsigned char bytes[28];
    MMBackgroundSound d;
    int big;
    if (r->length != 28 || !mm_read_exact(r, 0, bytes, 28) || (big = order(bytes)) < 0)
        return 0;
    memset(&d, 0, sizeof(d));
    d.system = (unsigned)word(bytes + 2, 2, big);
    d.volume = mm_signed_word(word(bytes + 4, 4, big));
    d.loop = word(bytes + 8, 4, big) != 0;
    d.tag.sid = mm_signed_word(word(bytes + 12, 4, big));
    d.tag.cache = NULL;
    d.tag.type = word(bytes + 20, 4, big);
    d.tag.number = word(bytes + 24, 4, big);
    *out = d;
    return 1;
}
void mm_background_light_decode(const unsigned char *b, int big, MMBackgroundLight *out)
{
    int i;
    for (i = 0; i < 12; ++i)
        out->matrix.m[i / 3][i % 3] = mm_signed_word(word(b + i * 4, 4, big));
    out->intensity = mm_signed_word(word(b + 48, 4, big));
    out->type = mm_signed_word(word(b + 52, 4, big));
}

int mm_background_light_values_valid(const MMPreviewLightValues *v)
{
    return v->x >= -32700 && v->x <= 32700 && v->y >= -32700 && v->y <= 32700 &&
           v->z >= -32700 && v->z <= 32700 && v->yaw >= -360 && v->yaw <= 360 &&
           v->pitch >= -89 && v->pitch <= 89 && v->intensity >= 0 && v->intensity <= 1;
}

static unsigned short light_angle(float degrees)
{
    return (unsigned short)(int)(degrees * (65536.f / 360));
}

static MMI32 light_scalar(float value)
{
    return (MMI32)(value * 65536.f);
}

int mm_background_light_rig_prepare(const MMPreviewLightValues *values, size_t count,
                                    MMBackgroundLightRotation rotate, MMBackgroundLightRig *out)
{
    MMBackgroundLightRig rig;
    size_t i;
    if (count > MM_BACKGROUND_LIGHT_RIG_LIMIT || (count && !values))
        return 0;
    memset(&rig, 0, sizeof(rig));
    for (i = 0; i < count; ++i)
    {
        const MMPreviewLightValues *v = &values[i];
        MMBackgroundLight *light;
        float attenuation;
        if (!mm_background_light_values_valid(v))
            return 0;
        if (v->intensity == 0)
            continue;
        light = &rig.lights[rig.count++];
        rotate(light_angle(-v->pitch), light_angle(v->yaw), &light->matrix);
        light->matrix.m[3][0] = light_scalar(v->x);
        light->matrix.m[3][1] = light_scalar(v->y);
        light->matrix.m[3][2] = light_scalar(v->z);
        /* Retail stores the constant attenuation divisor in its intensity field. */
        attenuation = 1.f / v->intensity;
        light->intensity = light_scalar(attenuation > 32700.f ? 32700.f : attenuation);
        light->type = v->point ? MM_BACKGROUND_LIGHT_POINT : MM_BACKGROUND_LIGHT_DIRECT;
    }
    *out = rig;
    return 1;
}

static void put_light_word(unsigned char *p, MMU32 value, int big)
{
    int i;
    for (i = 0; i < 4; ++i)
    {
        p[big ? 3 - i : i] = (unsigned char)value;
        value >>= 8;
    }
}

MMU32 mm_background_light_rig_encode(const MMBackgroundLightRig *rig, int system, int big,
                                    unsigned char *out, size_t capacity)
{
    MMCollectionHeader header = {0};
    MMU32 i, size;
    int j;
    if (rig->count > MM_BACKGROUND_LIGHT_RIG_LIMIT)
        return 0;
    header.kind = MM_COLLECTION_ARRAY;
    header.system = system;
    header.big_endian = big;
    header.width = MM_BACKGROUND_LIGHT_BYTES;
    header.count = rig->count;
    if (mm_collection_layout(&header) != MM_STORAGE_OK)
        return 0;
    size = header.header_bytes + header.first_bytes;
    if (capacity < size || mm_collection_header_write(&header, out, capacity) != MM_STORAGE_OK)
        return 0;
    for (i = 0; i < rig->count; ++i)
    {
        const MMBackgroundLight *light = &rig->lights[i];
        unsigned char *row = out + header.header_bytes + i * MM_BACKGROUND_LIGHT_BYTES;
        for (j = 0; j < 12; ++j)
            put_light_word(row + j * 4, (MMU32)light->matrix.m[j / 3][j % 3], big);
        put_light_word(row + 48, (MMU32)light->intensity, big);
        put_light_word(row + 52, (MMU32)light->type, big);
    }
    return size;
}

struct MMBackgroundOwner
{
    MMResourceObject binding;
    MMReference references;
    const MMBackgroundOwnerHost *host;
    MMBackgroundResource resource;
    MMBackgroundCameraState camera;
};

static MMU32 owner_bytes(const MMBackgroundOwnerHost *host)
{
    return sizeof(MMBackgroundOwner) + (MM_STRING_MAX + 2) * host->name_unit;
}

MMBackgroundOwner *mm_background_owner_create(const MMBackgroundOwnerHost *host)
{
    const MMStorageAllocator *allocator = host->allocator;
    MMBackgroundOwner *owner;
    MMU32 bytes;
    if (host->name_unit > 2)
        return NULL;
    bytes = owner_bytes(host);
    owner = allocator ? allocator->resize(allocator->context, NULL, 0, bytes, MM_STORAGE_NORMAL)
                      : malloc(bytes);
    if (owner)
    {
        memset(owner, 0, bytes);
        mm_reference_init(&owner->references);
        owner->host = host;
        mm_background_resource_init(&owner->resource);
        mm_background_camera_init(&owner->camera, NULL);
    }
    return owner;
}

MMBackgroundResource *mm_background_owner_resource(MMBackgroundOwner *owner) { return &owner->resource; }
MMBackgroundCameraState *mm_background_owner_camera(MMBackgroundOwner *owner) { return &owner->camera; }
const MMResourceObject *mm_background_owner_binding(const MMBackgroundOwner *owner) { return &owner->binding; }
MMI32 mm_background_owner_references(const MMBackgroundOwner *owner) { return owner->references.count; }

MMStringValue mm_background_owner_name(MMBackgroundOwner *owner)
{
    MMStringValue name = {0};
    if (owner->host->name_unit)
    {
        name.buffer = owner + 1;
        name.unit = owner->host->name_unit;
    }
    return name;
}

static MMResourceObject *background_binding(void *object) { return &((MMBackgroundOwner *)object)->binding; }
static MMReference *background_references(void *object) { return &((MMBackgroundOwner *)object)->references; }

static void destroy_background(void *object)
{
    MMBackgroundOwner *owner = object;
    const MMBackgroundOwnerHost *host = owner->host;
    const MMStorageAllocator *allocator = host->allocator;
    assert(!owner->resource.leave_on);
    mm_background_resource_dispose(&owner->resource, host->teardown);
    mm_background_camera_dispose(&owner->camera);
    mm_resource_object_dispose(owner, mm_background_owner_services());
    if (allocator)
        allocator->release(allocator->context, owner, owner_bytes(host));
    else
        free(owner);
}

static void mark_background(void *object)
{
    MMBackgroundOwner *owner = object;
    if (owner->host->mark)
        owner->host->mark(owner);
}

const MMResourceObjectServices *mm_background_owner_services(void)
{
    static const MMResourceObjectServices services = {
        background_binding, background_references, destroy_background, NULL, mark_background
    };
    return &services;
}

void mm_background_owner_retain(MMBackgroundOwner *owner)
{
    mm_reference_add(&owner->references);
}

void mm_background_owner_release(MMBackgroundOwner *owner)
{
    if (owner)
        mm_resource_object_release(owner, mm_background_owner_services());
}

void mm_background_resource_init(MMBackgroundResource *b)
{
    memset(b, 0, sizeof(*b));
    mm_storage_init(&b->palette, NULL);
}
static int child_number(const MMChunkAccess *chunks, MMU32 parent, MMU32 slot, MMU32 type, MMU32 *number)
{
    MMChunkChild child;
    if (mm_chunk_child_exact(chunks, parent, slot, type, &child) != MM_CHUNK_OK)
        return 0;
    *number = child.number;
    return 1;
}

static MMI32 cameras(const MMChunkAccess *chunks, MMU32 parent, int profile, const MMBackgroundServices *s)
{
    MMI32 count = 0, i, total = 0;
    MMU32 number;
    MMChunkChild child;
    while (child_number(chunks, parent, (MMU32)count, MM_BACKGROUND_CAMERA, &number))
    {
        if (count == MAXIMUM || (profile == MM_BACKGROUND_PREVIEW && !s->append_camera(s->context, number)))
            return -1;
        ++count;
    }
    if (s->debug_checks && profile == MM_BACKGROUND_STUDIO)
    {
        for (i = 0; mm_chunk_child_at(chunks, parent, (MMU32)i, &child) == MM_CHUNK_OK; ++i)
        {
            if (child.type == MM_BACKGROUND_CAMERA)
            {
                if (total == MAXIMUM)
                    return -1;
                ++total;
            }
            if (i == MAXIMUM)
                return -1;
        }
        if (total != count)
            s->diagnostic(s->context, MM_BACKGROUND_CAMERA_GAPS);
    }
    return count;
}
static int read_header(void *context, const MMReadStream *stream)
{
    return mm_background_header_read(stream, (MMBackgroundHeader *)context);
}
static int read_sound(void *context, const MMReadStream *stream)
{
    return mm_background_sound_read(stream, (MMBackgroundSound *)context);
}
static int read_record(const MMBackgroundSource *s, MMU32 type, MMU32 number, MMReadReceiver read, void *record)
{
    int decoded;
    return s->payload(s->context, type, number, read, record, &decoded) == MM_CHUNK_OK && decoded;
}

typedef struct BackgroundArray
{
    MMCollectionStorage *storage;
    MMCollectionHeader header;
} BackgroundArray;

static int read_array(void *context, const MMReadStream *stream)
{
    BackgroundArray *array = (BackgroundArray *)context;
    MMCollectionHeader *header = &array->header;
    return mm_collection_header_read(stream, MM_COLLECTION_ARRAY, 0, header) == MM_STORAGE_OK &&
           mm_storage_read(array->storage, stream, header->header_bytes, header->first_bytes, 0) == MM_STORAGE_OK;
}

static int palette(MMBackgroundResource *b, MMU32 parent, unsigned base, int profile,
                   const MMBackgroundSource *source, const MMBackgroundServices *s)
{
    BackgroundArray array;
    unsigned char *bytes;
    MMU32 child, i;
    int decoded, status;
    if (!child_number(&source->chunks, parent, 0, MM_BACKGROUND_COLORS, &child))
        return 1;
    array.storage = &b->palette;
    status = source->payload(source->context, MM_BACKGROUND_COLORS, child, read_array, &array, &decoded);
    if (status == MM_CHUNK_NOT_FOUND)
        return 0;
    if (status != MM_CHUNK_OK || !decoded)
    {
        mm_storage_dispose(&b->palette);
        if (profile == MM_BACKGROUND_STUDIO)
            b->palette_base = base;
        return profile == MM_BACKGROUND_STUDIO;
    }
    if (array.header.width != 4 || (profile == MM_BACKGROUND_PREVIEW && array.header.count > 256U - base))
    {
        mm_storage_dispose(&b->palette);
        return 0;
    }
    bytes = b->palette.buffers[0].data;
    if (array.header.big_endian != s->host_big_endian)
        mm_palette_swap_word_order(bytes, array.header.count);
    if (profile == MM_BACKGROUND_PREVIEW)
        for (i = 0; i < array.header.count; ++i, bytes += 4)
            s->palette_color(s->context, (MMI32)(base + i), bytes);
    if (profile == MM_BACKGROUND_PREVIEW && !s->retain_palette)
        mm_storage_dispose(&b->palette);
    else
        b->palette_count = array.header.count;
    b->palette_base = base;
    return 1;
}
int mm_background_read(MMBackgroundResource *b, MMBackgroundCameraState *camera, MMU32 type,
                       MMU32 number, int profile, const MMBackgroundSource *source, const MMBackgroundServices *s)
{
    MMBackgroundHeader header;
    MMBackgroundLight light;
    MMCollectionStorage lights;
    BackgroundArray array;
    const unsigned char *bytes;
    MMU32 parent, child;
    MMI32 i;
    mm_storage_init(&lights, NULL);
    array.storage = &lights;
    if (mm_chunk_find(&source->chunks, type, number, &parent) != MM_CHUNK_OK)
        goto fail;
    if (profile == MM_BACKGROUND_STUDIO)
    {
        camera->count = cameras(&source->chunks, parent, profile, s);
        camera->current = -1;
        if (camera->count < 0)
            goto fail;
    }
    if (!read_record(source, type, number, read_header, &header))
        goto fail;
    if (profile == MM_BACKGROUND_STUDIO)
    {
        if (!s->name(s->context, type, number))
            goto fail;
        if (child_number(&source->chunks, parent, 0, MM_BACKGROUND_SOUND, &child))
        {
            if (!read_record(source, MM_BACKGROUND_SOUND, child, read_sound, &b->sound))
                goto fail;
        }
        else
            b->sound.tag.sid = -1;
    }
    /* Only a present palette publishes the stored base in Studio. */
    if (!palette(b, parent, header.palette_base, profile, source, s))
        goto fail;
    if (profile == MM_BACKGROUND_PREVIEW)
        s->resolve_palette(s->context);
    if (!child_number(&source->chunks, parent, 0, MM_BACKGROUND_LIGHTS, &child) ||
        !read_record(source, MM_BACKGROUND_LIGHTS, child, read_array, &array))
        goto fail;
    b->light_count = (MMI32)array.header.count;
    if (array.header.width != MM_BACKGROUND_LIGHT_BYTES ||
        (profile == MM_BACKGROUND_PREVIEW && b->light_count > MM_BACKGROUND_LIGHT_RIG_LIMIT))
        goto fail;
    if (profile == MM_BACKGROUND_STUDIO)
    {
        if (s->actor_bytes <= 0 || s->light_bytes <= 0 || b->light_count > MAXIMUM / s->actor_bytes ||
            b->light_count > MAXIMUM / s->light_bytes ||
            !s->allocate(s->context, b->light_count * s->actor_bytes, &b->actors) ||
            !s->allocate(s->context, b->light_count * s->light_bytes, &b->lights))
            goto fail;
    }
    bytes = lights.buffers[0].data;
    for (i = 0; i < b->light_count; ++i, bytes += MM_BACKGROUND_LIGHT_BYTES)
    {
        mm_background_light_decode(bytes, array.header.big_endian, &light);
        if (!s->configure_light(s->context, b, i, &light))
            goto fail;
    }
    mm_storage_dispose(&lights);
    if (profile == MM_BACKGROUND_PREVIEW)
    {
        camera->count = cameras(&source->chunks, parent, profile, s);
        return camera->count > 0 && s->select_camera(s->context, 0);
    }
    return 1;
fail:
    if (profile == MM_BACKGROUND_STUDIO)
        s->diagnostic(s->context, MM_BACKGROUND_READ_ERROR);
    mm_storage_dispose(&lights);
    return 0;
}
typedef struct GraphRead
{
    MMReadReceiver receive;
    void *context;
    int called, result;
} GraphRead;

static int receive_graph_chunk(void *context, const MMReadStream *stream)
{
    GraphRead *read = (GraphRead *)context;
    if (read->called)
        return MM_MOVIE_BAD_FILE;
    read->called = 1;
    read->result = read->receive(read->context, stream);
    return MM_MOVIE_OK;
}

int mm_background_graph_payload(const MMMovieSessionGraph *graph, MMU32 type, MMU32 number, MMReadReceiver receive,
                                void *out, int *result)
{
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMU32 position;
    GraphRead read;
    int status;
    graph->access(graph->context, &chunks);
    status = mm_chunk_find(&chunks, type, number, &position);
    if (status != MM_CHUNK_OK)
        return status;
    status = mm_chunk_at(&chunks, position, &entry);
    if (status != MM_CHUNK_OK)
        return status;
    read.receive = receive;
    read.context = out;
    read.called = 0;
    status = graph->payload(graph->context, &entry, receive_graph_chunk, &read);
    if (status == MM_MOVIE_MEMORY)
        return MM_CHUNK_MEMORY;
    if (status != MM_MOVIE_OK || !read.called)
        return MM_CHUNK_BAD_FILE;
    *result = read.result;
    return MM_CHUNK_OK;
}

static int graph_payload(void *context, MMU32 type, MMU32 number, MMReadReceiver receive, void *out, int *result)
{
    return mm_background_graph_payload((const MMMovieSessionGraph *)context, type, number, receive, out, result);
}

int mm_background_read_graph(MMBackgroundResource *b, MMBackgroundCameraState *camera, MMU32 type, MMU32 number,
                             const MMMovieSessionGraph *graph, const MMBackgroundServices *s)
{
    MMBackgroundSource source;
    graph->access(graph->context, &source.chunks);
    source.context = (void *)graph;
    source.payload = graph_payload;
    return mm_background_read(b, camera, type, number, MM_BACKGROUND_PREVIEW, &source, s);
}

typedef struct CachedBackground
{
    MMResourceCache *cache;
    MMBlockTransform decode;
    MMOwnedBlock *root;
    MMU32 type, number;
} CachedBackground;

static int cache_payload(void *context, MMU32 type, MMU32 number, MMReadReceiver receive, void *out, int *result)
{
    CachedBackground *source = (CachedBackground *)context;
    MMBlockIO io;
    MMReadStream stream;
    if (source->root && type == source->type && number == source->number)
    {
        if (mm_owned_block_decoded_stream(source->root, NULL, source->decode, &io, &stream) != MM_DATA_OK)
            return MM_CHUNK_BAD_FILE;
        *result = receive(out, &stream);
        return MM_CHUNK_OK;
    }
    return mm_resource_cache_visit_payload(source->cache, type, number, source->decode, NULL, receive, out, result);
}

int mm_background_read_cached(MMBackgroundResource *b, MMBackgroundCameraState *camera, MMU32 type, MMU32 number,
                              MMResourceCache *cache, MMOwnedBlock *root, MMBlockTransform decode,
                              const MMBackgroundServices *s)
{
    CachedBackground context;
    MMBackgroundSource source;
    context.cache = cache;
    context.decode = decode;
    context.root = root;
    context.type = type;
    context.number = number;
    source.chunks = mm_resource_cache_chunks(cache);
    source.context = &context;
    source.payload = cache_payload;
    return mm_background_read(b, camera, type, number, MM_BACKGROUND_STUDIO, &source, s);
}

static void cache_access(void *context, MMChunkAccess *out)
{
    *out = mm_resource_cache_chunks(((CachedBackground *)context)->cache);
}

static int camera_payload(void *context, const MMChunkEntry *entry, MMReadReceiver receive, void *out)
{
    int result;
    return cache_payload(context, entry->type, entry->number, receive, out, &result) == MM_CHUNK_OK
               ? result : MM_MOVIE_BAD_FILE;
}

typedef struct CachedCamera
{
    MMBackgroundCameraState *state;
    MMU32 slot;
    const MMBackgroundCameraServices *services;
} CachedCamera;

static int apply_camera(void *context, const MMBackgroundCameraBinding *binding, const MMReadStream *stream)
{
    CachedCamera *camera = (CachedCamera *)context;
    return mm_background_camera_apply(camera->state, (MMI32)camera->slot, binding, stream, camera->services)
               ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

static void camera_diagnostic(void *context, int issue)
{
    const MMBackgroundCameraServices *s = ((CachedCamera *)context)->services;
    if (s->diagnostic)
        s->diagnostic(s->context, issue);
}

int mm_background_select_cached(MMBackgroundCameraState *camera, MMResourceCache *cache, MMU32 background,
                                MMU32 slot, MMBlockTransform decode, const MMBackgroundCameraServices *s)
{
    CachedBackground source;
    CachedCamera selection;
    MMMovieSessionGraph graph = {0};
    source.cache = cache;
    source.decode = decode;
    source.root = NULL;
    graph.context = &source;
    graph.access = cache_access;
    graph.payload = camera_payload;
    selection.state = camera;
    selection.slot = slot;
    selection.services = s;
    return mm_movie_camera_graph(&graph, background, slot, apply_camera, &selection, camera_diagnostic) == MM_MOVIE_OK;
}

void mm_background_lights_on(MMBackgroundResource *b, const MMBackgroundServices *s)
{
    MMI32 i;
    if (b->enabled)
        return;
    for (i = 0; i < b->light_count; ++i)
    {
        s->add_light(s->context, b, i);
        s->enable_light(s->context, b, i);
    }
    b->enabled = 1;
}
void mm_background_lights_off(MMBackgroundResource *b, const MMBackgroundServices *s)
{
    MMI32 i;
    if (!b->enabled || b->leave_on)
        return;
    for (i = 0; i < b->light_count; ++i)
    {
        s->disable_light(s->context, b, i);
        s->remove_light(s->context, b, i);
    }
    b->enabled = 0;
}
void mm_background_resource_dispose(MMBackgroundResource *b, const MMBackgroundServices *s)
{
    if (s)
    {
        if (b->actors && b->lights)
            mm_background_lights_off(b, s);
        s->free_memory(s->context, &b->actors);
        s->free_memory(s->context, &b->lights);
    }
    mm_storage_dispose(&b->palette);
    b->palette_count = 0;
}
const unsigned char *mm_background_palette(const MMBackgroundResource *b, MMU32 *count, MMI32 *base)
{
    *count = b->palette_count;
    *base = (MMI32)b->palette_base;
    return b->palette.buffers[0].data;
}
int mm_background_factory(MMU32 type, MMU32 number, int materialize,
                                const MMBackgroundFactoryServices *s)
{
    void *object;
    MMI32 count, unit;
    if (s->actor_bytes < 0 || s->light_bytes < 0 || s->object_bytes < 0 || s->actor_bytes > MAXIMUM - s->light_bytes)
        return 0;
    unit = s->actor_bytes + s->light_bytes;
    if (s->object_bytes > MAXIMUM - unit)
        return 0;
    s->size(s->context, s->object_bytes + unit);
    if (!materialize)
        return 1;
    object = s->create(s->context);
    if (!object || !s->initialize(s->context, object, type, number))
        goto fail;
    count = s->lights(s->context, object);
    if (count < 0 || (unit && count > (MAXIMUM - s->object_bytes) / unit))
        goto fail;
    s->publish(s->context, object);
    s->size(s->context, s->object_bytes + count * unit);
    return 1;
fail:
    s->failure(s->context);
    if (object)
        s->release(s->context, object);
    return 0;
}
static const MMU32 children[4] = {MM_BACKGROUND_SOUND, MM_BACKGROUND_LIGHTS,
                                       MM_BACKGROUND_COLORS, MM_BACKGROUND_CAMERA};
int mm_background_tags(MMTagList *list, const MMTag *parent)
{
    int i;
    if (mm_tag_list_insert(list, parent, 0) != MM_STORAGE_OK)
        return 0;
    for (i = 0; i < 4; ++i)
        if (mm_tag_list_child(list, parent, 0, children[i]) != MM_STORAGE_OK)
            return 0;
    return 1;
}
int mm_background_cache(const MMTag *parent, const MMBackgroundTagServices *s)
{
    MMTag tags[4];
    int i;
    for (i = 0; i < 4; ++i)
        if (!s->child(s->context, children[i], &tags[i]))
            return 0;
    if (!s->cache(s->context, parent, 0))
        return 0;
    for (i = 0; i < 4; ++i)
        if (!s->cache(s->context, &tags[i], 1))
            return 0;
    return 1;
}
