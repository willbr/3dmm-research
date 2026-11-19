#include "actor_archive_internal.h"
#include "mm/actor_math.h"
#include "mm/collections.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

#define ACTOR_TYPE 0x41435452UL
#define SCENE_TYPE 0x5343454eUL
#define EVENTS_TYPE 0x47474145UL
#define ROUTE_TYPE 0x50415448UL
#define THUMB_TYPE 0x5448554dUL
#define MATERIAL_TYPE 0x4d54524cUL

typedef struct ActorGroup
{
    MMGroup group;
    MMCollectionStorage storage;
} ActorGroup;

static void *new_group(void *context, MMU32 width)
{
    ActorGroup *owner;
    (void)context;
    owner = (ActorGroup *)calloc(1, sizeof(*owner));
    if (!owner)
        return NULL;
    mm_storage_init(&owner->storage, NULL);
    owner->group.storage = &owner->storage;
    owner->group.fixed_bytes = width;
    owner->group.free_count = -1;
    return &owner->group;
}

static void release_group(void *value)
{
    ActorGroup *owner = (ActorGroup *)value;
    mm_storage_dispose(&owner->storage);
    free(owner);
}

static MMU32 group_width(void *value) { return ((MMGroup *)value)->fixed_bytes; }
static MMU32 group_count(void *value) { return ((MMGroup *)value)->count; }

static void *group_fixed(void *value, MMU32 row)
{
    MMGroup *group = (MMGroup *)value;
    MMGroupLocation location;
    return mm_group_location(group, row, &location) == MM_STORAGE_OK
        ? group->storage->buffers[0].data + location.offset : NULL;
}

static MMI32 group_size(void *value, MMU32 row)
{
    MMGroup *group = (MMGroup *)value;
    MMGroupLocation location;
    return mm_group_location(group, row, &location) == MM_STORAGE_OK
        ? location.bytes - (MMI32)group->fixed_bytes : -1;
}

static void *group_data(void *value, MMU32 row)
{
    unsigned char *fixed = (unsigned char *)group_fixed(value, row);
    return fixed ? fixed + group_width(value) : NULL;
}

static int group_insert(void *value, MMU32 row, MMU32 size, const void *data, const void *fixed)
{
    return mm_group_insert((MMGroup *)value, row, size, data, fixed) == MM_STORAGE_OK;
}

static const MMActorEventServices track_groups = {
    NULL, group_width, group_count, group_fixed, group_size, group_data, new_group, group_insert, release_group
};

void *mm_actor_archive_event(MMGroup *group, MMU32 row) { return group_fixed(group, row); }
void *mm_actor_archive_payload(MMGroup *group, MMU32 row) { return group_data(group, row); }
void mm_actor_archive_release_event_group(MMGroup *group) { release_group(group); }

MMGroup *mm_actor_archive_read_event_group(const MMMovieArchiveEditor *editor, MMU32 actor, MMU32 *number)
{
    MMChunkAccess chunks;
    MMReadStream stream;
    MMU32 source;
    void *group;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_link_number(&chunks, ACTOR_TYPE, actor, 0, EVENTS_TYPE, &source) != MM_CHUNK_OK ||
        !editor->read(editor->context, EVENTS_TYPE, source, &stream) ||
        mm_actor_read_events_stream(&stream, &track_groups, &group) != MM_MOVIE_OK)
        return NULL;
    if (number)
        *number = source;
    return (MMGroup *)group;
}

static int write_track(const MMMovieArchiveEditor *editor, MMGroup *group, MMU32 *number,
                       const MMU32 *attach, int replace)
{
    MMCollectionHeader header = {0};
    MMStorageWriter writer;
    unsigned char bytes[20];
    const unsigned short one = 1;
    MMU32 size;
    header.kind = MM_COLLECTION_GROUP;
    header.system = editor->system;
    header.big_endian = *(const unsigned char *)&one == 0;
    header.width = group->fixed_bytes;
    header.count = group->count;
    header.bytes = group->bytes;
    header.free_count = group->free_count;
    if (mm_collection_layout(&header) != MM_STORAGE_OK ||
        mm_collection_header_write(&header, bytes, sizeof(bytes)) != MM_STORAGE_OK)
        return 0;
    size = header.header_bytes + header.first_bytes + header.second_bytes;
    if (replace ? !editor->replace(editor->context, EVENTS_TYPE, *number, size, &writer)
                : !editor->add(editor->context, EVENTS_TYPE, size, number, &writer))
        return 0;
    return (!attach || editor->adopt(editor->context, ACTOR_TYPE, *attach, EVENTS_TYPE, *number, 0)) &&
        mm_storage_write(group->storage, &writer, bytes, sizeof(bytes), header.first_bytes, header.second_bytes) ==
            MM_STORAGE_OK;
}

static int encode_track(const MMMovieArchiveEditor *editor, MMGroup *group, MMU32 *number,
                        const MMU32 *attach, int replace)
{
    MMGroup *wire;
    int result;
    wire = (MMGroup *)mm_actor_write_events(group, &track_groups);
    if (!wire)
        return 0;
    result = write_track(editor, wire, number, attach, replace);
    release_group(wire);
    return result;
}

int mm_actor_archive_write_event_group(const MMMovieArchiveEditor *editor, MMGroup *group, MMU32 *number,
                                       const MMU32 *attach)
{
    return encode_track(editor, group, number, attach, 0);
}

int mm_actor_archive_replace_event_group(const MMMovieArchiveEditor *editor, MMU32 actor, MMGroup *group)
{
    MMChunkAccess chunks;
    MMU32 existing;
    chunks = editor->chunks(editor->context);
    return mm_chunk_link_number(&chunks, ACTOR_TYPE, actor, 0, EVENTS_TYPE, &existing) == MM_CHUNK_OK &&
        encode_track(editor, group, &existing, NULL, 1);
}

static int replace_track(const MMMovieArchiveEditor *editor, MMU32 actor, MMU32 old, MMU32 events)
{
    editor->unlink(editor->context, ACTOR_TYPE, actor, EVENTS_TYPE, old, 0);
    return editor->adopt(editor->context, ACTOR_TYPE, actor, EVENTS_TYPE, events, 0)
        ? MM_ACTOR_EDIT_ARCHIVE_OK : MM_ACTOR_EDIT_ARCHIVE_LINK_EVENTS;
}

int mm_actor_archive_link_events(const MMMovieArchiveEditor *editor, MMU32 actor, MMU32 events)
{
    MMChunkAccess chunks;
    MMU32 old;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_link_number(&chunks, ACTOR_TYPE, actor, 0, EVENTS_TYPE, &old) != MM_CHUNK_OK)
        return MM_ACTOR_EDIT_ARCHIVE_READ_EVENTS;
    return replace_track(editor, actor, old, events);
}

static int rewrite_track(const MMMovieArchiveEditor *editor, MMU32 actor,
                         int (*edit)(MMGroup *, const void *), const void *changes)
{
    MMGroup *runtime;
    MMU32 old, number;
    int result;
    runtime = mm_actor_archive_read_event_group(editor, actor, &old);
    if (!runtime)
        return MM_ACTOR_EDIT_ARCHIVE_READ_EVENTS;
    result = edit(runtime, changes);
    if (result == MM_ACTOR_EDIT_ARCHIVE_OK)
        result = mm_actor_archive_write_event_group(editor, runtime, &number, NULL)
            ? replace_track(editor, actor, old, number) : MM_ACTOR_EDIT_ARCHIVE_WRITE_EVENTS;
    release_group(runtime);
    return result;
}

static int replace_material(MMGroup *group, const void *changes)
{
    MMActorEvent event;
    MMActorCostume costume = {0};
    MMU32 row;
    costume.tag.type = MATERIAL_TYPE;
    costume.tag.number = *(const MMU32 *)changes;
    for (row = group->count; row > 0;)
    {
        --row;
        memcpy(&event, group_fixed(group, row), sizeof(event));
        if (event.aet == MM_ACTOR_COSTUME && mm_group_delete(group, row) != MM_STORAGE_OK)
            return MM_ACTOR_EDIT_ARCHIVE_EDIT_EVENTS;
    }
    for (row = 0; row < group->count; ++row)
    {
        memcpy(&event, group_fixed(group, row), sizeof(event));
        if (event.aet == MM_ACTOR_ADD)
        {
            event.aet = MM_ACTOR_COSTUME;
            if (!group_insert(group, ++row, sizeof(costume), &costume, &event))
                return MM_ACTOR_EDIT_ARCHIVE_EDIT_EVENTS;
        }
    }
    return MM_ACTOR_EDIT_ARCHIVE_OK;
}

int mm_actor_archive_set_material(const MMMovieArchiveEditor *editor, MMU32 actor, MMU32 material)
{
    return rewrite_track(editor, actor, replace_material, &material);
}

typedef struct Shape
{
    MMActorMatrix rotation;
    MMActorStretch stretch;
    int rotate, resize;
} Shape;

static int replace_shape(MMGroup *group, const void *changes)
{
    const Shape *shape = (const Shape *)changes;
    MMActorEvent event;
    MMActorMatrix original, combined;
    MMActorStretch stretch;
    void *data;
    MMU32 row;
    for (row = 0; row < group->count; ++row)
    {
        memcpy(&event, group_fixed(group, row), sizeof(event));
        if (shape->rotate && (event.aet == MM_ACTOR_ROTATE_FORWARD || event.aet == MM_ACTOR_ROTATE_HERE))
        {
            data = group_data(group, row);
            memcpy(&original, data, sizeof(original));
            mm_matrix_multiply(&combined, &shape->rotation, &original);
            memcpy(data, &combined, sizeof(combined));
        }
        if (shape->resize && event.aet == MM_ACTOR_STRETCH)
        {
            data = group_data(group, row);
            memcpy(&stretch, data, sizeof(stretch));
            stretch.rScaleX = mm_fixed_mul(stretch.rScaleX, shape->stretch.rScaleX);
            stretch.rScaleY = mm_fixed_mul(stretch.rScaleY, shape->stretch.rScaleY);
            stretch.rScaleZ = mm_fixed_mul(stretch.rScaleZ, shape->stretch.rScaleZ);
            memcpy(data, &stretch, sizeof(stretch));
        }
        if (event.aet == MM_ACTOR_ADD)
        {
            if (shape->rotate)
            {
                event.aet = MM_ACTOR_ROTATE_FORWARD;
                if (!group_insert(group, ++row, sizeof(shape->rotation), &shape->rotation, &event))
                    return MM_ACTOR_EDIT_ARCHIVE_EDIT_EVENTS;
            }
            if (shape->resize)
            {
                event.aet = MM_ACTOR_STRETCH;
                if (!group_insert(group, ++row, sizeof(shape->stretch), &shape->stretch, &event))
                    return MM_ACTOR_EDIT_ARCHIVE_EDIT_EVENTS;
            }
        }
    }
    return MM_ACTOR_EDIT_ARCHIVE_OK;
}

static int apply_shape(const MMMovieArchiveEditor *editor, MMU32 actor, const MMActorArchivePose *pose)
{
    Shape shape = {0};
    unsigned i;
    for (i = 0; i < 9; ++i)
    {
        float value = pose->rotation[i];
        if (!(value >= -32768.0f && value < 32768.0f))
            return MM_ACTOR_EDIT_ARCHIVE_RANGE;
        shape.rotate |= value != (i % 4 == 0 ? 1.0f : 0.0f);
        shape.rotation.m[i / 3][i % 3] = (MMI32)(value * 65536.0f);
    }
    for (i = 0; i < 3; ++i)
    {
        float value = pose->scale[i];
        if (!(value >= -32768.0f && value < 32768.0f))
            return MM_ACTOR_EDIT_ARCHIVE_RANGE;
        shape.resize |= value != 1.0f;
    }
    shape.stretch.rScaleX = (MMI32)(pose->scale[0] * 65536.0f);
    shape.stretch.rScaleY = (MMI32)(pose->scale[1] * 65536.0f);
    shape.stretch.rScaleZ = (MMI32)(pose->scale[2] * 65536.0f);
    return shape.rotate || shape.resize ? rewrite_track(editor, actor, replace_shape, &shape)
                                        : MM_ACTOR_EDIT_ARCHIVE_OK;
}

static int actor_slot(const MMChunkAccess *chunks, MMU32 scene, MMU32 actor, MMU32 *slot)
{
    MMChunkChild child;
    MMU32 position, i;
    if (mm_chunk_find(chunks, SCENE_TYPE, scene, &position) == MM_CHUNK_OK)
        for (i = 0; mm_chunk_child_at(chunks, position, i, &child) == MM_CHUNK_OK; ++i)
            if (child.type == ACTOR_TYPE && child.number == actor)
            {
                *slot = child.slot;
                return 1;
            }
    return 0;
}

int mm_actor_archive_find(const MMMovieArchiveEditor *editor, MMU32 scene, MMI32 actor_id, MMU32 *actor)
{
    MMChunkAccess chunks;
    MMChunkChild child;
    MMActorHeader header;
    MMU32 position, i;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_find(&chunks, SCENE_TYPE, scene, &position) != MM_CHUNK_OK)
        return 0;
    for (i = 0; mm_chunk_child_at(&chunks, position, i, &child) == MM_CHUNK_OK; ++i)
        if (child.type == ACTOR_TYPE && mm_actor_archive_read_actor(editor, child.number, &header) &&
            header.actor_id == actor_id)
        {
            *actor = child.number;
            return 1;
        }
    return 0;
}

int mm_actor_archive_read_actor(const MMMovieArchiveEditor *editor, MMU32 actor, MMActorHeader *header)
{
    MMReadStream stream;
    return editor->read(editor->context, ACTOR_TYPE, actor, &stream) && mm_actor_read_header(&stream, header);
}

int mm_actor_archive_copy_actor(const MMMovieArchiveEditor *editor, MMU32 actor, const MMActorHeader *header,
                                int share_track, MMU32 *copy)
{
    MMChunkAccess chunks;
    MMChunkChild child;
    MMChunkEntry original;
    MMU32 position, i;
    if (!editor->add(editor->context, ACTOR_TYPE, sizeof(*header), copy, NULL) ||
        !editor->put(editor->context, ACTOR_TYPE, *copy, header, sizeof(*header)))
        return MM_ACTOR_EDIT_ARCHIVE_COPY;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_find(&chunks, ACTOR_TYPE, actor, &position) != MM_CHUNK_OK ||
        mm_chunk_at(&chunks, position, &original) != MM_CHUNK_OK)
        return MM_ACTOR_EDIT_ARCHIVE_CHILD;
    for (i = 0; i < original.child_count; ++i)
    {
        if (mm_chunk_child_at(&chunks, position, i, &child) != MM_CHUNK_OK)
            return MM_ACTOR_EDIT_ARCHIVE_CHILD;
        if (!share_track && (child.type == ROUTE_TYPE || child.type == EVENTS_TYPE))
            continue;
        if (!editor->adopt(editor->context, ACTOR_TYPE, *copy, child.type, child.number, child.slot))
            return MM_ACTOR_EDIT_ARCHIVE_CHILD;
    }
    return MM_ACTOR_EDIT_ARCHIVE_OK;
}

int mm_actor_archive_place(const MMMovieArchiveEditor *editor, const MMActorArchivePose *pose)
{
    MMChunkAccess chunks;
    MMActorHeader header;
    MMU32 slot, copy, i, thumb;
    int result;
    chunks = editor->chunks(editor->context);
    if (!actor_slot(&chunks, pose->scene, pose->actor, &slot))
        return MM_ACTOR_EDIT_ARCHIVE_SCENE;
    if (!mm_actor_archive_read_actor(editor, pose->actor, &header))
        return MM_ACTOR_EDIT_ARCHIVE_HEADER;
    for (i = 0; i < 3; ++i)
    {
        double translated = (double)((float)mm_signed_word(header.translation[i]) / 65536.0f) + pose->translation[i];
        if (!(translated >= -32700.0 && translated <= 32700.0))
            return MM_ACTOR_EDIT_ARCHIVE_RANGE;
        header.translation[i] = (MMU32)(MMI32)((float)translated * 65536.0f);
    }
    result = mm_actor_archive_copy_actor(editor, pose->actor, &header, 1, &copy);
    if (result != MM_ACTOR_EDIT_ARCHIVE_OK)
        return result;
    if (pose->appearance && !pose->appearance(pose->context, copy))
        return MM_ACTOR_EDIT_ARCHIVE_APPEARANCE;
    result = apply_shape(editor, copy, pose);
    if (result != MM_ACTOR_EDIT_ARCHIVE_OK)
        return result;
    editor->unlink(editor->context, SCENE_TYPE, pose->scene, ACTOR_TYPE, pose->actor, slot);
    if (!editor->adopt(editor->context, SCENE_TYPE, pose->scene, ACTOR_TYPE, copy, slot))
        return MM_ACTOR_EDIT_ARCHIVE_LINK_ACTOR;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_link_number(&chunks, SCENE_TYPE, pose->scene, 0, THUMB_TYPE, &thumb) == MM_CHUNK_OK)
        editor->unlink(editor->context, SCENE_TYPE, pose->scene, THUMB_TYPE, thumb, 0);
    return MM_ACTOR_EDIT_ARCHIVE_OK;
}
