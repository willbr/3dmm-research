/* Scene event storage converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_track.h"
#include "mm/movie_session.h"
#include <string.h>

int mm_scene_track_init(MMSceneTrack *track, const MMStorageAllocator *allocator, int trash_unused)
{
    memset(track, 0, sizeof(*track));
    if (mm_storage_init(&track->storage, allocator) != MM_STORAGE_OK)
        return 0;
    /* Match the historical event group's initial growth for both buffers. */
    track->storage.min_grow[0] = 16 * MM_SCENE_EVENT_BYTES;
    track->storage.min_grow[1] = 16 * sizeof(MMGroupLocation);
    track->rows.storage = &track->storage;
    track->rows.fixed_bytes = MM_SCENE_EVENT_BYTES;
    track->rows.free_count = -1;
    track->rows.trash_unused = trash_unused;
    return 1;
}

void mm_scene_track_dispose(MMSceneTrack *track)
{
    if (mm_storage_dispose(&track->storage) == MM_STORAGE_OK)
        track->rows.count = track->rows.bytes = 0;
}

void *mm_scene_track_payload(const MMSceneTrack *track, MMI32 index, MMI32 *bytes)
{
    MMGroupLocation loc;
    if (mm_group_location(&track->rows, (MMU32)index, &loc) != MM_STORAGE_OK)
        return NULL;
    if (bytes)
        *bytes = loc.bytes - MM_SCENE_EVENT_BYTES;
    return track->storage.buffers[0].data + loc.offset + MM_SCENE_EVENT_BYTES;
}

int mm_scene_track_get(const MMSceneTrack *track, MMI32 index, MMSceneTrackEvent *out)
{
    const unsigned char *payload = (const unsigned char *)mm_scene_track_payload(track, index, NULL);
    if (!payload)
        return 0;
    memcpy(out, payload - MM_SCENE_EVENT_BYTES, MM_SCENE_EVENT_BYTES);
    return 1;
}

static void read_view_event(void *context, void *group, MMI32 index, MMI32 *frame, int *kind)
{
    MMSceneTrackEvent event;
    (void)context;
    if (mm_scene_track_get((const MMSceneTrack *)group, index, &event))
    {
        *frame = event.frame;
        *kind = event.kind;
    }
    else
    {
        *frame = 0;
        *kind = -1;
    }
}
MMMovieEventView mm_scene_track_event_view(MMSceneTrack *track)
{
    MMMovieEventView view;
    view.context = NULL;
    view.group = track;
    view.count = track->rows.count;
    view.read = read_view_event;
    return view;
}

int mm_scene_track_get_object(const MMSceneTrack *track, MMI32 index, void **out)
{
    MMI32 bytes;
    const void *data = mm_scene_track_payload(track, index, &bytes);
    if (!data || bytes != (MMI32)sizeof(*out))
        return 0;
    memcpy(out, data, sizeof(*out));
    return 1;
}

int mm_scene_track_insert(MMSceneTrack *track, MMI32 index, MMI32 bytes, const void *data,
                          const MMSceneTrackEvent *event)
{
    return mm_group_insert(&track->rows, (MMU32)index, (MMU32)bytes, data, event) == MM_STORAGE_OK;
}

int mm_scene_track_put(MMSceneTrack *track, MMI32 index, MMI32 bytes, const void *data)
{
    return mm_group_put(&track->rows, (MMU32)index, (MMU32)bytes, data) == MM_STORAGE_OK;
}

int mm_scene_track_set(MMSceneTrack *track, MMI32 index, const MMSceneTrackEvent *event)
{
    unsigned char *payload = (unsigned char *)mm_scene_track_payload(track, index, NULL);
    if (!payload)
        return 0;
    memcpy(payload - MM_SCENE_EVENT_BYTES, event, MM_SCENE_EVENT_BYTES);
    return 1;
}

static MMU32 wire_width(void *group)
{
    return ((const MMGroup *)group)->fixed_bytes;
}

static MMU32 wire_count(void *group)
{
    return ((const MMGroup *)group)->count;
}

static const unsigned char *wire_row(const MMGroup *group, MMU32 index, MMI32 *bytes)
{
    MMGroupLocation loc;
    if (mm_group_read_slot(group, index, &loc) != MM_GROUP_SLOT_PRESENT)
        return NULL;
    if (bytes)
        *bytes = loc.bytes - MM_SCENE_EVENT_BYTES;
    return group->storage->buffers[0].data + loc.offset;
}

static void wire_fixed(void *group, MMU32 index, unsigned char bytes[8])
{
    const unsigned char *row = wire_row(group, index, NULL);
    if (row)
        memcpy(bytes, row, MM_SCENE_EVENT_BYTES);
    else
        memset(bytes, 0, MM_SCENE_EVENT_BYTES);
}

static MMI32 wire_payload_size(void *group, MMU32 index)
{
    MMI32 bytes = -1;
    wire_row(group, index, &bytes);
    return bytes;
}

static const void *wire_payload(void *group, MMU32 index)
{
    const unsigned char *row = wire_row(group, index, NULL);
    return row ? row + MM_SCENE_EVENT_BYTES : NULL;
}

MMSceneGroupServices mm_scene_group_wire_services(void)
{
    MMSceneGroupServices groups = {wire_width, wire_count, wire_fixed, wire_payload_size, wire_payload};
    return groups;
}

static int native_big(void)
{
    const unsigned short one = 1;
    return *(const unsigned char *)&one == 0;
}

static int wire_header(const MMGroup *group, MMCollectionHeader *header)
{
    memset(header, 0, sizeof(*header));
    header->kind = MM_COLLECTION_GROUP;
    header->width = group->fixed_bytes;
    header->count = group->count;
    header->bytes = group->bytes;
    header->free_count = -1;
    return group->fixed_bytes == MM_SCENE_EVENT_BYTES && group->free_count == -1 &&
           mm_collection_layout(header) == MM_STORAGE_OK;
}

MMU32 mm_scene_group_wire_size(const MMGroup *group)
{
    MMCollectionHeader header;
    if (!wire_header(group, &header))
        return 0;
    return header.header_bytes + header.first_bytes + header.second_bytes;
}

int mm_scene_group_write(MMGroup *group, int system, int big, const MMStorageWriter *writer)
{
    MMCollectionHeader header;
    unsigned char bytes[MM_SCENE_EMPTY_GROUP_BYTES];
    int ok, swap = big != native_big();
    if (!wire_header(group, &header))
        return 0;
    header.system = system;
    header.big_endian = big;
    if (mm_collection_header_write(&header, bytes, sizeof(bytes)) != MM_STORAGE_OK)
        return 0;
    if (swap)
        mm_group_swap_locations(group);
    ok = mm_storage_write(group->storage, writer, bytes, header.header_bytes, header.first_bytes,
                          header.second_bytes) == MM_STORAGE_OK;
    if (swap)
        mm_group_swap_locations(group);
    return ok;
}
