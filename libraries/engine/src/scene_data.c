/* Scene state and persistence converted from engine scene_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_data.h"
#include "mm/wide.h"
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
int mm_scene_valid_events(void *group, int big, int start, const MMSceneGroupServices *s)
{
    MMU32 i;
    MMI32 type, count, bytes;
    unsigned char fixed[8];
    const unsigned char *data;
    if (s->fixed_width(group) != 8)
        return 0;
    for (i = 0; i < s->count(group); ++i)
    {
        s->fixed(group, i, fixed);
        type = mm_signed_word(word(fixed + 4, big));
        bytes = s->size(group, i);
        switch (type)
        {
        case MM_SCENE_ADD_ACTOR:
        case MM_SCENE_ADD_TEXT:
            if (!start || bytes != 4)
                return 0;
            break;
        case MM_SCENE_SET_BACKGROUND:
            if (!start || bytes != 16)
                return 0;
            break;
        case MM_SCENE_CHANGE_CAMERA:
            if (bytes != 4)
                return 0;
            break;
        case MM_SCENE_PAUSE:
            if (start || bytes != 8)
                return 0;
            break;
        case MM_SCENE_PLAY_SOUND:
            if (start || bytes < 16)
                return 0;
            data = (const unsigned char *)s->data(group, i);
            count = mm_signed_word(word(data + 12, big));
            if (count <= 0 || (MMU32)count > (MMU32)(bytes - 16) / 20 ||
                (MMU32)bytes != 16 + (MMU32)count * 20)
                return 0;
            break;
        default:
            return 0;
        }
    }
    return 1;
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
