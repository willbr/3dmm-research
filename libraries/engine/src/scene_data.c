/* Scene state and persistence converted from engine scene_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/scene_data.h"
#include "three_mm/wide.h"
#include <string.h>
static ThreeMMU32 word(const unsigned char *p, int big)
{
    return big ? ((ThreeMMU32)p[0] << 24) | ((ThreeMMU32)p[1] << 16) | ((ThreeMMU32)p[2] << 8) | p[3]
               : p[0] | ((ThreeMMU32)p[1] << 8) | ((ThreeMMU32)p[2] << 16) | ((ThreeMMU32)p[3] << 24);
}
static void put(unsigned char *p, ThreeMMU32 value, unsigned count, int big)
{
    unsigned i;
    for (i = 0; i < count; ++i)
        p[big ? count - 1 - i : i] = (unsigned char)(value >> (i * 8));
}
int three_mm_scene_init(ThreeMMSceneState *s, unsigned unit)
{
    if (!s || (unit != 1 && unit != 2))
        return 0;
    memset(s, 0, sizeof(*s));
    s->currentFrame = s->firstFrame = s->lastFrame = 1;
    three_mm_movie_title_init(&s->name, unit);
    return 1;
}
static int valid(const ThreeMMSceneState *s)
{
    return s->firstFrame != (-2147483647L - 1) && s->lastFrame >= s->firstFrame && s->transition >= -1 &&
           s->transition < THREE_MM_SCENE_TRANSITION_LIMIT;
}
int three_mm_scene_decode_header(const ThreeMMReadStream *stream, ThreeMMSceneState *s)
{
    unsigned char bytes[16];
    int big;
    ThreeMMSceneState loaded;
    if (stream->length != 16 || !three_mm_read_exact(stream, 0, bytes, 16))
        return 0;
    big = bytes[0] == 0 && bytes[1] == 1;
    if (!big && !(bytes[0] == 1 && bytes[1] == 0))
        return 0;
    three_mm_scene_init(&loaded, s->name.unit);
    loaded.lastFrame = three_mm_signed_word(word(bytes + 4, big));
    loaded.firstFrame = three_mm_signed_word(word(bytes + 8, big));
    loaded.transition = three_mm_signed_word(word(bytes + 12, big));
    if (!valid(&loaded))
        return 0;
    loaded.currentFrame = loaded.firstFrame - 1;
    *s = loaded;
    return 1;
}
int three_mm_scene_encode_header(const ThreeMMSceneState *s, int system, int big, unsigned char bytes[16])
{
    if (!valid(s))
        return 0;
    put(bytes, 1, 2, big);
    put(bytes + 2, (ThreeMMU32)system, 2, big);
    put(bytes + 4, (ThreeMMU32)s->lastFrame, 4, big);
    put(bytes + 8, (ThreeMMU32)s->firstFrame, 4, big);
    put(bytes + 12, (ThreeMMU32)s->transition, 4, big);
    return 1;
}
int three_mm_scene_read(ThreeMMSceneState *s, ThreeMMU32 number, const ThreeMMSceneIO *io)
{
    ThreeMMReadStream stream;
    ThreeMMSceneState loaded;
    three_mm_scene_init(&loaded, s->name.unit);
    if (!io->read(io->context, number, &stream) || !three_mm_scene_decode_header(&stream, &loaded))
        return 0;
    io->load_name(io->context, number, &loaded.name);
    *s = loaded;
    return 1;
}
int three_mm_scene_write(const ThreeMMSceneState *s, ThreeMMU32 number, int system, int big, const ThreeMMSceneIO *io)
{
    unsigned char bytes[16];
    if (!three_mm_scene_encode_header(s, system, big, bytes))
        return 0;
    return io->put_header(io->context, number, bytes) && io->set_name(io->context, number, &s->name);
}
int three_mm_scene_valid_events(void *group, int big, int start, const ThreeMMSceneGroupServices *s)
{
    ThreeMMU32 i;
    ThreeMMI32 type, count, bytes;
    unsigned char fixed[8];
    const unsigned char *data;
    if (s->fixed_width(group) != 8)
        return 0;
    for (i = 0; i < s->count(group); ++i)
    {
        s->fixed(group, i, fixed);
        type = three_mm_signed_word(word(fixed + 4, big));
        bytes = s->size(group, i);
        switch (type)
        {
        case THREE_MM_SCENE_ADD_ACTOR:
        case THREE_MM_SCENE_ADD_TEXT:
            if (!start || bytes != 4)
                return 0;
            break;
        case THREE_MM_SCENE_SET_BACKGROUND:
            if (!start || bytes != 16)
                return 0;
            break;
        case THREE_MM_SCENE_CHANGE_CAMERA:
            if (bytes != 4)
                return 0;
            break;
        case THREE_MM_SCENE_PAUSE:
            if (start || bytes != 8)
                return 0;
            break;
        case THREE_MM_SCENE_PLAY_SOUND:
            if (start || bytes < 16)
                return 0;
            data = (const unsigned char *)s->data(group, i);
            count = three_mm_signed_word(word(data + 12, big));
            if (count <= 0 || (ThreeMMU32)count > (ThreeMMU32)(bytes - 16) / 20 ||
                (ThreeMMU32)bytes != 16 + (ThreeMMU32)count * 20)
                return 0;
            break;
        default:
            return 0;
        }
    }
    return 1;
}
int three_mm_scene_write_events(ThreeMMU32 scene, void *frames, void *starts, const ThreeMMSceneEventIO *s)
{
    ThreeMMU32 old_frame, old_start, frame = 0xffffffffUL, start = 0xffffffffUL;
    int had_frame, had_start, created_frame = 0, created_start = 0, adopted_frame = 0, adopted_start = 0;
    const unsigned short one = 1;
    int big = *(const unsigned char *)&one == 0;
    if (!three_mm_scene_valid_events(frames, big, 0, &s->groups) ||
        !three_mm_scene_valid_events(starts, big, 1, &s->groups))
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
int three_mm_scene_create(const ThreeMMSceneState *state, ThreeMMU32 *number, int system, int big,
                          const ThreeMMSceneCreateServices *s)
{
    void *frames = s->new_group(s->io.context);
    void *starts = s->new_group(s->io.context);
    int created, success;
    *number = 0xffffffffUL;
    created = frames && starts && s->add_scene(s->io.context, 16, number);
    success = created && three_mm_scene_write(state, *number, system, big, &s->io) &&
              three_mm_scene_write_events(*number, frames, starts, &s->events);
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
