/* Scene state and persistence converted from engine scene_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_data.h"
#include "mm/collections.h"
#include "mm/movie_archive_edit.h"
#include "mm/movie_data.h"
#include "mm/scene_edit.h"
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
static int encode_group_header(int system, int big, MMU32 rows, MMU32 bytes, unsigned char *out)
{
    MMCollectionHeader header = {0};
    header.kind = MM_COLLECTION_GROUP;
    header.system = system;
    header.big_endian = big;
    header.width = MM_SCENE_EVENT_BYTES;
    header.count = rows;
    header.bytes = bytes;
    header.free_count = -1;
    return mm_collection_header_write(&header, out, MM_SCENE_EMPTY_GROUP_BYTES) == MM_STORAGE_OK;
}
int mm_scene_encode_empty_group(int system, int big, unsigned char out[MM_SCENE_EMPTY_GROUP_BYTES])
{
    return encode_group_header(system, big, 0, 0, out);
}
int mm_scene_encode_background_group(int system, int big, MMI32 frame, MMI32 source,
                                     MMU32 background, unsigned char out[MM_SCENE_BACKGROUND_GROUP_BYTES])
{
    enum { row_bytes = MM_SCENE_EVENT_BYTES + 16 };
    unsigned char *row;
    if (!encode_group_header(system, big, 1, row_bytes, out))
        return 0;
    row = out + MM_SCENE_EMPTY_GROUP_BYTES;
    put(row, (MMU32)frame, 4, big);
    put(row + 4, MM_SCENE_SET_BACKGROUND, 4, big);
    put(row + 8, (MMU32)source, 4, big);
    put(row + 12, 0, 4, big);
    put(row + 16, 0x424b4744UL, 4, big);
    put(row + 20, background, 4, big);
    put(row + row_bytes, 0, 4, big);
    put(row + row_bytes + 4, row_bytes, 4, big);
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

#define SCENE_TYPE 0x5343454eUL
#define FRAME_GROUP_TYPE 0x47474652UL
#define THUMB_TYPE 0x5448554dUL

int mm_scene_archive_read_header(const MMMovieArchiveEditor *editor, MMU32 scene, MMSceneState *state)
{
    MMReadStream stream;
    MMSceneState decoded;
    if (!mm_scene_init(&decoded, editor->text_unit) ||
        !editor->read(editor->context, SCENE_TYPE, scene, &stream) ||
        !mm_scene_decode_header(&stream, &decoded))
        return 0;
    *state = decoded;
    return 1;
}

typedef struct SceneFrameEdit
{
    const MMMovieArchiveEditor *editor;
    MMSceneState scene;
    MMCollectionStorage storage;
    MMGroup group;
    MMU32 number, previous;
    int big, result;
} SceneFrameEdit;

static unsigned char *frame_record(SceneFrameEdit *edit, MMU32 row, MMI32 *size)
{
    MMGroupLocation location;
    if (mm_group_location(&edit->group, row, &location) != MM_STORAGE_OK)
    {
        edit->result = MM_SCENE_ARCHIVE_EDIT_INVALID_FRAMES;
        return NULL;
    }
    if (size)
        *size = location.bytes - MM_SCENE_EVENT_BYTES;
    return edit->storage.buffers[0].data + location.offset;
}

static int open_frames(void *context)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    const MMMovieArchiveEditor *editor = edit->editor;
    MMChunkAccess chunks;
    MMReadStream stream;
    MMCollectionHeader header;
    const unsigned short one = 1;
    const unsigned char *row;
    MMI32 size, kind;
    MMU32 i;
    edit->result = MM_SCENE_ARCHIVE_EDIT_READ_FRAMES;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_link_number(&chunks, SCENE_TYPE, edit->number, 0, FRAME_GROUP_TYPE, &edit->previous) != MM_CHUNK_OK ||
        !editor->read(editor->context, FRAME_GROUP_TYPE, edit->previous, &stream))
        return 0;
    edit->result = MM_SCENE_ARCHIVE_EDIT_INVALID_FRAMES;
    if (mm_collection_header_read(&stream, MM_COLLECTION_GROUP, 0, &header) != MM_STORAGE_OK ||
        header.width != MM_SCENE_EVENT_BYTES ||
        mm_storage_read(&edit->storage, &stream, header.header_bytes, header.first_bytes,
                        header.second_bytes) != MM_STORAGE_OK)
        return 0;
    edit->big = header.big_endian;
    edit->group.fixed_bytes = header.width;
    edit->group.count = header.count;
    edit->group.bytes = header.bytes;
    edit->group.free_count = header.free_count;
    if (edit->big != (*(const unsigned char *)&one == 0))
        mm_group_swap_locations(&edit->group);
    for (i = 0; i < header.count; ++i)
    {
        row = frame_record(edit, i, &size);
        if (!row)
            return 0;
        kind = mm_signed_word(word(row + 4, edit->big));
        if (!valid_event(kind, size, edit->big, 0, row + MM_SCENE_EVENT_BYTES))
            return 0;
    }
    edit->result = MM_SCENE_ARCHIVE_EDIT_OK;
    return 1;
}

static MMI32 frame_count(void *context)
{
    return (MMI32)((SceneFrameEdit *)context)->group.count;
}

static void frame_event(void *context, MMI32 index, MMI32 *frame, int *kind)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    const unsigned char *row = frame_record(edit, (MMU32)index, NULL);
    *frame = row ? mm_signed_word(word(row, edit->big)) : 0;
    *kind = row ? mm_signed_word(word(row + 4, edit->big)) : -1;
}

static void erase_camera(void *context, MMI32 index)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    if (mm_group_delete(&edit->group, (MMU32)index) != MM_STORAGE_OK)
        edit->result = MM_SCENE_ARCHIVE_EDIT_INVALID_FRAMES;
}

static int insert_camera(void *context, MMI32 index, MMI32 frame, MMU32 camera)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    unsigned char fixed[MM_SCENE_EVENT_BYTES], payload[4];
    if (edit->result != MM_SCENE_ARCHIVE_EDIT_OK)
        return 0;
    put(fixed, (MMU32)frame, 4, edit->big);
    put(fixed + 4, MM_SCENE_CHANGE_CAMERA, 4, edit->big);
    put(payload, camera, 4, edit->big);
    if (mm_group_insert(&edit->group, (MMU32)index, sizeof(payload), payload, fixed) == MM_STORAGE_OK)
        return 1;
    edit->result = MM_SCENE_ARCHIVE_EDIT_INSERT_CAMERA;
    return 0;
}

static void move_opening_camera(void *context, MMI32 index, MMI32 frame)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    unsigned char *row = frame_record(edit, (MMU32)index, NULL);
    if (row)
    {
        put(row, (MMU32)frame, 4, edit->big);
        if (mm_group_move(&edit->group, (MMU32)index, 0) != MM_STORAGE_OK)
            edit->result = MM_SCENE_ARCHIVE_EDIT_INVALID_FRAMES;
    }
}

static int replace_frames(void *context)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    const MMMovieArchiveEditor *editor = edit->editor;
    MMStorageWriter writer;
    unsigned char header[MM_SCENE_EMPTY_GROUP_BYTES];
    const unsigned short one = 1;
    MMU32 number, size;
    if (edit->result != MM_SCENE_ARCHIVE_EDIT_OK)
        return 0;
    edit->result = MM_SCENE_ARCHIVE_EDIT_WRITE_FRAMES;
    size = sizeof(header) + edit->group.bytes + edit->group.count * 8;
    if (!encode_group_header(editor->system, edit->big, edit->group.count, edit->group.bytes, header) ||
        !editor->add(editor->context, FRAME_GROUP_TYPE, size, &number, &writer))
        return 0;
    /* The edit owns this group and discards it after writing. */
    if (edit->big != (*(const unsigned char *)&one == 0))
        mm_group_swap_locations(&edit->group);
    if (mm_storage_write(&edit->storage, &writer, header, sizeof(header), edit->group.bytes,
                         edit->group.count * 8) != MM_STORAGE_OK)
        return 0;
    editor->unlink(editor->context, SCENE_TYPE, edit->number, FRAME_GROUP_TYPE, edit->previous, 0);
    if (!editor->adopt(editor->context, SCENE_TYPE, edit->number, FRAME_GROUP_TYPE, number, 0))
    {
        edit->result = MM_SCENE_ARCHIVE_EDIT_LINK_FRAMES;
        return 0;
    }
    edit->result = MM_SCENE_ARCHIVE_EDIT_OK;
    return 1;
}

static int drop_frame_thumbnail(void *context)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    const MMMovieArchiveEditor *editor = edit->editor;
    MMChunkAccess chunks;
    MMU32 thumb;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_link_number(&chunks, SCENE_TYPE, edit->number, 0, THUMB_TYPE, &thumb) == MM_CHUNK_OK)
        editor->unlink(editor->context, SCENE_TYPE, edit->number, THUMB_TYPE, thumb, 0);
    return 1;
}

static int write_frame_range(void *context)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    const MMMovieArchiveEditor *editor = edit->editor;
    unsigned char header[MM_SCENE_HEADER_BYTES];
    if (mm_scene_encode_header(&edit->scene, editor->system, editor->big_endian, header) &&
        editor->put(editor->context, SCENE_TYPE, edit->number, header, sizeof(header)))
        return 1;
    edit->result = MM_SCENE_ARCHIVE_EDIT_WRITE_SCENE;
    return 0;
}

static int begin_frame_edit(SceneFrameEdit *edit, const MMMovieArchiveEditor *editor, MMU32 scene)
{
    memset(edit, 0, sizeof(*edit));
    edit->editor = editor;
    edit->number = scene;
    if (!mm_scene_archive_read_header(editor, scene, &edit->scene))
        return 0;
    mm_storage_init(&edit->storage, NULL);
    edit->group.storage = &edit->storage;
    return 1;
}

int mm_scene_archive_set_camera(const MMMovieArchiveEditor *editor, MMU32 scene, MMI32 key, MMU32 camera)
{
    SceneFrameEdit edit;
    MMSceneCameraStoreServices services = {
        {NULL, frame_count, frame_event, erase_camera, insert_camera}, open_frames, replace_frames,
        drop_frame_thumbnail};
    if (!begin_frame_edit(&edit, editor, scene))
        return MM_SCENE_ARCHIVE_EDIT_READ_SCENE;
    services.events.context = &edit;
    mm_scene_camera_store(edit.scene.firstFrame, key, camera, &services);
    mm_storage_dispose(&edit.storage);
    return edit.result;
}

int mm_scene_archive_set_range(const MMMovieArchiveEditor *editor, MMU32 scene, long first, long last)
{
    SceneFrameEdit edit;
    MMSceneRuntimeServices events = {0};
    MMSceneRangeStoreServices services = {NULL, open_frames, replace_frames, drop_frame_thumbnail, write_frame_range};
    int result;
    if (!begin_frame_edit(&edit, editor, scene))
        return MM_SCENE_ARCHIVE_EDIT_READ_SCENE;
    events.context = &edit;
    events.event_count = frame_count;
    events.event = frame_event;
    events.move_camera = move_opening_camera;
    services.events = &events;
    result = mm_scene_range_store(&edit.scene, first, last, &services);
    mm_storage_dispose(&edit.storage);
    return result == MM_SCENE_RANGE_INVALID ? MM_SCENE_ARCHIVE_EDIT_RANGE : edit.result;
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
