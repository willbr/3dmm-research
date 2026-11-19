/* Scene state and persistence converted from engine scene_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_data.h"
#include "mm/collections.h"
#include "mm/movie_data.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>
static MMU32 word(const unsigned char *p, int big)
{
    return big ? ((MMU32)p[0] << 24) | ((MMU32)p[1] << 16) | ((MMU32)p[2] << 8) | p[3]
               : p[0] | ((MMU32)p[1] << 8) | ((MMU32)p[2] << 16) | ((MMU32)p[3] << 24);
}
static void put(unsigned char *p, MMU32 value, unsigned count, int big)
{
    unsigned i;
    for (i = 0; i < count; ++i)
        p[big ? count - 1 - i : i] = (unsigned char)(value >> (i * 8));
}
int mm_scene_init(MMSceneState *s, unsigned unit)
{
    if (!s || (unit != 1 && unit != 2))
        return 0;
    memset(s, 0, sizeof(*s));
    s->currentFrame = s->firstFrame = s->lastFrame = 1;
    mm_movie_title_init(&s->name, unit);
    return 1;
}
static int valid(const MMSceneState *s)
{
    return s->firstFrame != (-2147483647L - 1) && s->lastFrame >= s->firstFrame && s->transition >= -1 &&
           s->transition < MM_SCENE_TRANSITION_LIMIT;
}
int mm_scene_decode_header(const MMReadStream *stream, MMSceneState *s)
{
    unsigned char bytes[16];
    int big;
    MMSceneState loaded;
    if (stream->length != 16 || !mm_read_exact(stream, 0, bytes, 16))
        return 0;
    big = bytes[0] == 0 && bytes[1] == 1;
    if (!big && !(bytes[0] == 1 && bytes[1] == 0))
        return 0;
    mm_scene_init(&loaded, s->name.unit);
    loaded.lastFrame = mm_signed_word(word(bytes + 4, big));
    loaded.firstFrame = mm_signed_word(word(bytes + 8, big));
    loaded.transition = mm_signed_word(word(bytes + 12, big));
    if (!valid(&loaded))
        return 0;
    loaded.currentFrame = loaded.firstFrame - 1;
    *s = loaded;
    return 1;
}
int mm_scene_encode_header(const MMSceneState *s, int system, int big, unsigned char bytes[16])
{
    if (!valid(s))
        return 0;
    put(bytes, 1, 2, big);
    put(bytes + 2, (MMU32)system, 2, big);
    put(bytes + 4, (MMU32)s->lastFrame, 4, big);
    put(bytes + 8, (MMU32)s->firstFrame, 4, big);
    put(bytes + 12, (MMU32)s->transition, 4, big);
    return 1;
}
static int read_header(void *out, const MMReadStream *stream)
{
    return mm_scene_decode_header(stream, (MMSceneState *)out);
}
int mm_scene_read(MMSceneState *s, MMU32 number, const MMSceneIO *io)
{
    MMSceneState loaded;
    mm_scene_init(&loaded, s->name.unit);
    if (!io->read(io->context, number, read_header, &loaded))
        return 0;
    io->load_name(io->context, number, &loaded.name);
    *s = loaded;
    return 1;
}
int mm_scene_write(const MMSceneState *s, MMU32 number, int system, int big, const MMSceneIO *io)
{
    unsigned char bytes[16];
    if (!mm_scene_encode_header(s, system, big, bytes))
        return 0;
    return io->put_header(io->context, number, bytes) && io->set_name(io->context, number, &s->name);
}
int mm_scene_transition_read(MMU32 number, unsigned unit, MMI32 *out, const MMSceneIO *io)
{
    MMSceneState data;
    if (!mm_scene_init(&data, unit) || !mm_scene_read(&data, number, io))
        return 0;
    *out = data.transition;
    return 1;
}
int mm_scene_transition_write(MMU32 number, unsigned unit, MMI32 transition, int system, int big,
                                    const MMSceneIO *io)
{
    MMSceneState data;
    if (!mm_scene_init(&data, unit) || !mm_scene_read(&data, number, io))
        return 0;
    data.transition = transition;
    return mm_scene_write(&data, number, system, big, io);
}
static int valid_event(MMI32 type, MMI32 bytes, int big, int start, const unsigned char *data)
{
    MMI32 count;
    switch (type)
    {
    case MM_SCENE_ADD_ACTOR:
    case MM_SCENE_ADD_TEXT:
        return start && bytes == 4;
    case MM_SCENE_SET_BACKGROUND:
        return start && bytes == 16;
    case MM_SCENE_CHANGE_CAMERA:
        return bytes == 4;
    case MM_SCENE_PAUSE:
        return !start && bytes == 8;
    case MM_SCENE_PLAY_SOUND:
        if (start || bytes < 16 || !data)
            return 0;
        count = mm_signed_word(word(data + 12, big));
        return count > 0 && (MMU32)count <= (MMU32)(bytes - 16) / 20 &&
               (MMU32)bytes == 16 + (MMU32)count * 20;
    default:
        return 0;
    }
}
int mm_scene_valid_events(void *group, int big, int start, const MMSceneGroupServices *s)
{
    MMU32 i;
    MMI32 type, bytes;
    unsigned char fixed[8];
    const unsigned char *data;
    if (s->fixed_width(group) != 8)
        return 0;
    for (i = 0; i < s->count(group); ++i)
    {
        s->fixed(group, i, fixed);
        type = mm_signed_word(word(fixed + 4, big));
        bytes = s->size(group, i);
        data = type == MM_SCENE_PLAY_SOUND && !start && bytes >= 16
                   ? (const unsigned char *)s->data(group, i) : NULL;
        if (!valid_event(type, bytes, big, start, data))
            return 0;
    }
    return 1;
}
int mm_scene_retarget_start_background(const MMReadStream *stream, MMI32 source, MMU32 background,
                                        MMByteReceiver receive, void *context)
{
    MMCollectionHeader header;
    unsigned char *copy, *records, *tag;
    MMU32 index, offset;
    MMI32 bytes, type;
    int found = 0, result = 0;
    if (mm_collection_header_read(stream, MM_COLLECTION_GROUP, 0, &header) != MM_STORAGE_OK ||
        header.width != MM_SCENE_EVENT_BYTES || (size_t)stream->length != stream->length)
        return 0;
    copy = (unsigned char *)malloc((size_t)stream->length);
    if (!copy)
        return 0;
    if (!mm_read_exact(stream, 0, copy, stream->length))
        goto done;
    records = copy + header.header_bytes;
    for (index = 0; index < header.count; ++index)
    {
        if (!mm_collection_group_row(&header, records, index, &offset, &bytes))
            goto done;
        type = mm_signed_word(word(records + offset + 4, header.big_endian));
        if (!valid_event(type, bytes, header.big_endian, 1, NULL))
            goto done;
        if (type == MM_SCENE_SET_BACKGROUND)
        {
            tag = records + offset + MM_SCENE_EVENT_BYTES;
            put(tag, (MMU32)source, 4, header.big_endian);
            put(tag + 4, 0, 4, header.big_endian);
            put(tag + 8, 0x424b4744UL, 4, header.big_endian);
            put(tag + 12, background, 4, header.big_endian);
            found = 1;
        }
    }
    if (found)
        result = receive(context, copy, stream->length);
done:
    free(copy);
    return result;
}
int mm_scene_append_start_actor(const MMReadStream *stream, int system, MMI32 frame, MMU32 slot,
                                MMByteReceiver receive, void *context)
{
    enum { payload_bytes = 4, row_bytes = MM_SCENE_EVENT_BYTES + payload_bytes };
    MMCollectionHeader input, output;
    unsigned char *copy, *records, *row, *location;
    MMU32 total, index, offset;
    MMI32 bytes, type;
    int result, big;
    if (mm_collection_header_read(stream, MM_COLLECTION_GROUP, 0, &input) != MM_STORAGE_OK ||
        input.width != MM_SCENE_EVENT_BYTES)
        return MM_MOVIE_BAD_FILE;
    output = input;
    output.system = system;
    output.bytes += row_bytes;
    ++output.count;
    if (mm_collection_layout(&output) != MM_STORAGE_OK)
        return MM_MOVIE_BAD_FILE;
    total = output.header_bytes + output.first_bytes + output.second_bytes;
    if ((size_t)total != total)
        return MM_MOVIE_MEMORY;
    copy = (unsigned char *)malloc((size_t)total);
    if (!copy)
        return MM_MOVIE_MEMORY;
    records = copy + input.header_bytes;
    result = MM_MOVIE_BAD_FILE;
    if (!mm_read_exact(stream, input.header_bytes, records, input.first_bytes + input.second_bytes))
        goto done;
    big = input.big_endian;
    for (index = 0; index < input.count; ++index)
    {
        if (!mm_collection_group_row(&input, records, index, &offset, &bytes))
            goto done;
        type = mm_signed_word(word(records + offset + 4, big));
        if (!valid_event(type, bytes, big, 1, NULL))
            goto done;
    }
    if (mm_collection_header_write(&output, copy, output.header_bytes) != MM_STORAGE_OK)
        goto done;
    row = records + input.first_bytes;
    memmove(row + row_bytes, row, input.second_bytes);
    put(row, (MMU32)frame, 4, big);
    put(row + 4, MM_SCENE_ADD_ACTOR, 4, big);
    put(row + MM_SCENE_EVENT_BYTES, slot, payload_bytes, big);
    location = records + output.first_bytes + input.second_bytes;
    put(location, input.first_bytes, 4, big);
    put(location + 4, row_bytes, 4, big);
    result = receive(context, copy, total) ? MM_MOVIE_OK : MM_MOVIE_IO;
done:
    free(copy);
    return result;
}
void *mm_scene_read_event_group(int start, int native_big, int *order, const MMSceneReadGroupServices *s)
{
    int loaded_order;
    void *group = s->read(s->context, &loaded_order);
    if (!group)
        return NULL;
    if ((loaded_order != MM_SCENE_GROUP_NATIVE_ORDER && loaded_order != MM_SCENE_GROUP_OTHER_ORDER) ||
        (start == MM_SCENE_GROUP_HEADERS_ONLY
             ? s->groups.fixed_width(group) != MM_SCENE_EVENT_BYTES
             : !mm_scene_valid_events(
                   group, (loaded_order == MM_SCENE_GROUP_NATIVE_ORDER) == (native_big != 0), start, &s->groups)))
    {
        s->release(s->context, group);
        return NULL;
    }
    if (order)
        *order = loaded_order;
    return group;
}
int mm_scene_write_events(MMU32 scene, void *frames, void *starts, const MMSceneEventIO *s)
{
    MMU32 old_frame, old_start, frame = 0xffffffffUL, start = 0xffffffffUL;
    int had_frame, had_start, created_frame = 0, created_start = 0, adopted_frame = 0, adopted_start = 0;
    const unsigned short one = 1;
    int big = *(const unsigned char *)&one == 0;
    if (!mm_scene_valid_events(frames, big, 0, &s->groups) ||
        !mm_scene_valid_events(starts, big, 1, &s->groups))
        return 0;
    had_frame = s->previous(s->context, scene, 0, &old_frame);
    had_start = s->previous(s->context, scene, 1, &old_start);
    if (!s->add(s->context, 0, s->wire_size(frames), &frame))
        goto fail;
    created_frame = 1;
    if (!s->write(s->context, frames))
        goto fail;
    if (!s->add(s->context, 1, s->wire_size(starts), &start))
        goto fail;
    created_start = 1;
    if (!s->write(s->context, starts))
        goto fail;
    if (!s->adopt(s->context, scene, 0, frame))
        goto fail;
    adopted_frame = 1;
    if (!s->adopt(s->context, scene, 1, start))
        goto fail;
    adopted_start = 1;
    if (had_frame)
        s->remove(s->context, scene, 0, old_frame, 1);
    if (had_start)
        s->remove(s->context, scene, 1, old_start, 1);
    s->clear_loner(s->context, 0, frame);
    s->clear_loner(s->context, 1, start);
    return 1;
fail:
    if (adopted_frame || created_frame)
        s->remove(s->context, scene, 0, frame, adopted_frame);
    if (adopted_start || created_start)
        s->remove(s->context, scene, 1, start, adopted_start);
    return 0;
}
int mm_scene_create(const MMSceneState *state, MMU32 *number, int system, int big,
                          const MMSceneCreateServices *s)
{
    void *frames = s->new_group(s->io.context);
    void *starts = s->new_group(s->io.context);
    int created, success;
    *number = 0xffffffffUL;
    created = frames && starts && s->add_scene(s->io.context, 16, number);
    success = created && mm_scene_write(state, *number, system, big, &s->io) &&
              mm_scene_write_events(*number, frames, starts, &s->events);
    if (frames)
        s->release_group(frames);
    if (starts)
        s->release_group(starts);
    if (!success && created)
        s->remove_scene(s->io.context, *number);
    if (!success)
        *number = 0xffffffffUL;
    return success;
}
