/* Scene state and persistence converted from engine scene_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_data.h"
#include "mm/scene_track.h"
#include "mm/background_resource.h"
#include "mm/collections.h"
#include "mm/movie_archive_edit.h"
#include "mm/movie_data.h"
#include "mm/movie_sound.h"
#include "mm/scene_edit.h"
#include "mm/scene_sound.h"
#include "mm/text_store.h"
#include "mm/wide.h"
#include "sound_archive_internal.h"
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
    mm_chunk_title_read(&loaded.name, io->context, MM_SCENE_CHUNK, number, &io->titles);
    *s = loaded;
    return 1;
}
int mm_scene_write(const MMSceneState *s, MMU32 number, int system, int big, const MMSceneIO *io)
{
    unsigned char bytes[16];
    if (!mm_scene_encode_header(s, system, big, bytes))
        return 0;
    return io->put_header(io->context, number, bytes) &&
           mm_chunk_title_write(&s->name, io->context, MM_SCENE_CHUNK, number, &io->titles);
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
    MMSceneSoundWire sound;
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
        return !start && mm_scene_sound_wire(data, bytes, big, &sound) &&
               bytes == mm_scene_sound_size(sound.header.ctagc, MM_SCENE_SOUND_WIRE_TAG_BYTES);
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
    put(row + 16, MM_BACKGROUND, 4, big);
    put(row + 20, background, 4, big);
    put(row + row_bytes, 0, 4, big);
    put(row + row_bytes + 4, row_bytes, 4, big);
    return 1;
}
static int edit_start_backgrounds(const MMReadStream *stream, MMI32 source, MMU32 background,
                                  int (*map)(void *, MMU32, MMU32 *), MMByteReceiver receive, void *context)
{
    MMCollectionHeader header;
    unsigned char *copy, *records, *tag, original[4];
    MMU32 index, offset, number, mapped;
    MMI32 bytes, type;
    int changed = 0, result = 0;
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
        if (type != MM_SCENE_SET_BACKGROUND)
            continue;
        tag = records + offset + MM_SCENE_EVENT_BYTES;
        if (!map)
        {
            put(tag, (MMU32)source, 4, header.big_endian);
            put(tag + 4, 0, 4, header.big_endian);
            put(tag + 8, MM_BACKGROUND, 4, header.big_endian);
            put(tag + 12, background, 4, header.big_endian);
            changed = 1;
        }
        else if (!word(tag, header.big_endian) && word(tag + 8, header.big_endian) == MM_BACKGROUND)
        {
            /* Rows can share a location. Read the original number in that case too. */
            if (!mm_read_exact(stream, header.header_bytes + offset + MM_SCENE_EVENT_BYTES + 12,
                               original, sizeof(original)))
                goto done;
            number = word(original, header.big_endian);
            if (!map(context, number, &mapped))
                goto done;
            if (number != mapped)
            {
                put(tag + 12, mapped, 4, header.big_endian);
                changed = 1;
            }
        }
    }
    result = changed ? receive(context, copy, stream->length) : map != NULL;
done:
    free(copy);
    return result;
}
int mm_scene_retarget_start_background(const MMReadStream *stream, MMI32 source, MMU32 background,
                                        MMByteReceiver receive, void *context)
{
    return edit_start_backgrounds(stream, source, background, NULL, receive, context);
}
int mm_scene_rebind_start_backgrounds(const MMReadStream *stream, int (*map)(void *, MMU32, MMU32 *),
                                      MMByteReceiver receive, void *context)
{
    return edit_start_backgrounds(stream, 0, 0, map, receive, context);
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

#define THUMB_TYPE 0x5448554dUL

int mm_scene_archive_read_header(const MMMovieArchiveEditor *editor, MMU32 scene, MMSceneState *state)
{
    MMReadStream stream;
    MMSceneState decoded;
    if (!mm_scene_init(&decoded, editor->text_unit) ||
        !editor->read(editor->context, MM_SCENE_CHUNK, scene, &stream) ||
        !mm_scene_decode_header(&stream, &decoded))
        return 0;
    *state = decoded;
    return 1;
}

static unsigned char *event_record(const MMGroup *group, MMU32 row, MMI32 *size)
{
    MMGroupLocation location;
    if (mm_group_location(group, row, &location) != MM_STORAGE_OK)
        return NULL;
    if (size)
        *size = location.bytes - MM_SCENE_EVENT_BYTES;
    return group->storage->buffers[0].data + location.offset;
}

static int read_archive_events(const MMReadStream *stream, int start, MMGroup *group, int *big)
{
    MMCollectionHeader header;
    const unsigned short one = 1;
    const unsigned char *row;
    MMI32 size, kind;
    MMU32 i;
    if (mm_collection_header_read(stream, MM_COLLECTION_GROUP, 0, &header) != MM_STORAGE_OK ||
        header.width != MM_SCENE_EVENT_BYTES ||
        mm_storage_read(group->storage, stream, header.header_bytes, header.first_bytes,
                        header.second_bytes) != MM_STORAGE_OK)
        return 0;
    *big = header.big_endian;
    group->fixed_bytes = header.width;
    group->count = header.count;
    group->bytes = header.bytes;
    group->free_count = header.free_count;
    if (*big != (*(const unsigned char *)&one == 0))
        mm_group_swap_locations(group);
    for (i = 0; i < header.count; ++i)
    {
        row = event_record(group, i, &size);
        if (!row)
            return 0;
        kind = mm_signed_word(word(row + 4, *big));
        if (!valid_event(kind, size, *big, start, row + MM_SCENE_EVENT_BYTES))
            return 0;
    }
    return 1;
}

/* The caller owns the group and discards it after writing. */
static int write_archive_events(const MMMovieArchiveEditor *editor, MMU32 type, MMGroup *group, int big,
                                MMU32 *number)
{
    MMStorageWriter writer;
    unsigned char header[MM_SCENE_EMPTY_GROUP_BYTES];
    const unsigned short one = 1;
    MMU32 size;
    if (!encode_group_header(editor->system, big, group->count, group->bytes, header))
        return 0;
    size = sizeof(header) + group->bytes + group->count * 8;
    if (!editor->add(editor->context, type, size, number, &writer))
        return 0;
    if (big != (*(const unsigned char *)&one == 0))
        mm_group_swap_locations(group);
    return mm_storage_write(group->storage, &writer, header, sizeof(header), group->bytes,
                            group->count * 8) == MM_STORAGE_OK;
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
    unsigned char *record;
    record = event_record(&edit->group, row, size);
    if (!record)
        edit->result = MM_SCENE_ARCHIVE_EDIT_INVALID_FRAMES;
    return record;
}

static int open_frames(void *context)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    const MMMovieArchiveEditor *editor = edit->editor;
    MMChunkAccess chunks;
    MMReadStream stream;
    edit->result = MM_SCENE_ARCHIVE_EDIT_READ_FRAMES;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, edit->number, 0, MM_SCENE_FRAME_GROUP, &edit->previous) != MM_CHUNK_OK ||
        !editor->read(editor->context, MM_SCENE_FRAME_GROUP, edit->previous, &stream))
        return 0;
    edit->result = MM_SCENE_ARCHIVE_EDIT_INVALID_FRAMES;
    if (!read_archive_events(&stream, 0, &edit->group, &edit->big))
        return 0;
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

static int publish_frames(SceneFrameEdit *edit, MMGroup *group)
{
    const MMMovieArchiveEditor *editor = edit->editor;
    MMU32 number;
    if (edit->result != MM_SCENE_ARCHIVE_EDIT_OK)
        return 0;
    edit->result = MM_SCENE_ARCHIVE_EDIT_WRITE_FRAMES;
    if (!write_archive_events(editor, MM_SCENE_FRAME_GROUP, group, edit->big, &number))
        return 0;
    editor->unlink(editor->context, MM_SCENE_CHUNK, edit->number, MM_SCENE_FRAME_GROUP, edit->previous, 0);
    if (!editor->adopt(editor->context, MM_SCENE_CHUNK, edit->number, MM_SCENE_FRAME_GROUP, number, 0))
    {
        edit->result = MM_SCENE_ARCHIVE_EDIT_LINK_FRAMES;
        return 0;
    }
    edit->result = MM_SCENE_ARCHIVE_EDIT_OK;
    return 1;
}

static int replace_frames(void *context)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    return publish_frames(edit, &edit->group);
}

static int drop_frame_thumbnail(void *context)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    const MMMovieArchiveEditor *editor = edit->editor;
    MMChunkAccess chunks;
    MMU32 thumb;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, edit->number, 0, THUMB_TYPE, &thumb) == MM_CHUNK_OK)
        editor->unlink(editor->context, MM_SCENE_CHUNK, edit->number, THUMB_TYPE, thumb, 0);
    return 1;
}

static int write_frame_range(void *context)
{
    SceneFrameEdit *edit = (SceneFrameEdit *)context;
    const MMMovieArchiveEditor *editor = edit->editor;
    unsigned char header[MM_SCENE_HEADER_BYTES];
    if (mm_scene_encode_header(&edit->scene, editor->system, editor->big_endian, header) &&
        editor->put(editor->context, MM_SCENE_CHUNK, edit->number, header, sizeof(header)))
        return 1;
    edit->result = MM_SCENE_ARCHIVE_EDIT_WRITE_SCENE;
    return 0;
}

static void init_frame_edit(SceneFrameEdit *edit, const MMMovieArchiveEditor *editor, MMU32 scene)
{
    memset(edit, 0, sizeof(*edit));
    edit->editor = editor;
    edit->number = scene;
    mm_storage_init(&edit->storage, NULL);
    edit->group.storage = &edit->storage;
}

int mm_scene_archive_set_camera(const MMMovieArchiveEditor *editor, MMU32 scene, MMI32 key, MMU32 camera)
{
    SceneFrameEdit edit;
    MMSceneCameraStoreServices services = {
        {NULL, frame_count, frame_event, erase_camera, insert_camera}, open_frames, replace_frames,
        drop_frame_thumbnail};
    init_frame_edit(&edit, editor, scene);
    if (!mm_scene_archive_read_header(editor, scene, &edit.scene))
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
    init_frame_edit(&edit, editor, scene);
    if (!mm_scene_archive_read_header(editor, scene, &edit.scene))
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

static int open_text_events(const MMMovieArchiveEditor *editor, const MMChunkAccess *chunks, MMU32 parent,
                            MMChunkChild *previous, MMGroup *group, int *big)
{
    MMReadStream stream;
    if (mm_chunk_child_exact(chunks, parent, 1, MM_SCENE_START_GROUP, previous) != MM_CHUNK_OK ||
        !editor->read(editor->context, MM_SCENE_START_GROUP, previous->number, &stream))
        return MM_SCENE_ARCHIVE_TEXT_READ_START;
    return read_archive_events(&stream, 1, group, big) ? MM_SCENE_ARCHIVE_TEXT_OK
                                                      : MM_SCENE_ARCHIVE_TEXT_INVALID_START;
}

static int text_slot(const MMGroup *group, MMU32 index, int big, MMU32 *slot)
{
    const unsigned char *row = event_record(group, index, NULL);
    if (!row || word(row + 4, big) != MM_SCENE_ADD_TEXT)
        return 0;
    *slot = word(row + MM_SCENE_EVENT_BYTES, big);
    return 1;
}

static int collect_text_links(const MMChunkAccess *chunks, MMU32 parent, const MMGroup *group, int big,
                              MMSceneArchiveText **out, MMU32 *count)
{
    MMChunkEntry scene;
    MMChunkChild box;
    MMSceneArchiveText *links = NULL;
    unsigned char *seen = NULL;
    MMU32 limit, row, slot, index, found = 0;
    int result, query;
    if (mm_chunk_at(chunks, parent, &scene) != MM_CHUNK_OK)
        return MM_SCENE_ARCHIVE_TEXT_LINK;
    if (group->count)
    {
        limit = scene.child_count < group->count ? scene.child_count : group->count;
        result = MM_SCENE_ARCHIVE_TEXT_MEMORY;
        if ((size_t)scene.child_count != scene.child_count)
            goto done;
        seen = (unsigned char *)calloc((size_t)scene.child_count, 1);
        links = (MMSceneArchiveText *)calloc((size_t)limit, sizeof(*links));
        if (!seen || !links)
            goto done;
    }
    for (row = 0; row < group->count; ++row)
    {
        if (!text_slot(group, row, big, &slot))
            continue;
        /* Number zero searches for the first child of this type at the slot. */
        query = mm_chunk_find_child(chunks, parent, slot, MM_TEXT_BOX, 0, &index);
        if ((query != MM_CHUNK_OK && query != MM_CHUNK_NOT_FOUND) ||
            mm_chunk_child_at(chunks, parent, index, &box) != MM_CHUNK_OK ||
            box.slot != slot || box.type != MM_TEXT_BOX)
        {
            result = MM_SCENE_ARCHIVE_TEXT_LINK;
            goto done;
        }
        if (!seen[index])
        {
            seen[index] = 1;
            links[found].slot = slot;
            links[found++].number = box.number;
        }
    }
    *out = links;
    *count = found;
    links = NULL;
    result = MM_SCENE_ARCHIVE_TEXT_OK;
done:
    free(seen);
    free(links);
    return result;
}

int mm_scene_archive_list_text(const MMMovieArchiveEditor *editor, MMU32 scene,
                               MMSceneArchiveText **out, MMU32 *count)
{
    MMChunkAccess chunks;
    MMChunkChild previous;
    MMCollectionStorage storage;
    MMGroup group = {0};
    MMU32 parent;
    int big, result;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_find(&chunks, MM_SCENE_CHUNK, scene, &parent) != MM_CHUNK_OK)
        return MM_SCENE_ARCHIVE_TEXT_READ_START;
    mm_storage_init(&storage, NULL);
    group.storage = &storage;
    result = open_text_events(editor, &chunks, parent, &previous, &group, &big);
    if (result == MM_SCENE_ARCHIVE_TEXT_OK)
        result = collect_text_links(&chunks, parent, &group, big, out, count);
    mm_storage_dispose(&storage);
    return result;
}

void mm_scene_archive_free_text_list(MMSceneArchiveText *links)
{
    free(links);
}

static int first_text_slot(const MMChunkAccess *chunks, MMU32 parent, MMU32 *slot)
{
    MMChunkEntry scene;
    MMChunkChild child;
    MMU32 index, next = 0;
    if (mm_chunk_at(chunks, parent, &scene) != MM_CHUNK_OK)
        return 0;
    for (index = 0; index < scene.child_count; ++index)
    {
        if (mm_chunk_child_at(chunks, parent, index, &child) != MM_CHUNK_OK)
            return 0;
        if (child.slot > next)
            break;
        if (child.slot == next && child.type == MM_TEXT_BOX)
            ++next;
    }
    *slot = next;
    return 1;
}

static int update_text_events(MMGroup *group, int big, const MMSceneArchiveTextEdit *edit, MMU32 slot)
{
    unsigned char fixed[MM_SCENE_EVENT_BYTES], payload[4];
    MMU32 row, existing;
    if (!edit->original)
    {
        put(fixed, (MMU32)edit->opening_frame, 4, big);
        put(fixed + 4, MM_SCENE_ADD_TEXT, 4, big);
        put(payload, slot, 4, big);
        return mm_group_add(group, sizeof(payload), payload, fixed, NULL) == MM_STORAGE_OK;
    }
    for (row = group->count; row > 0;)
    {
        --row;
        if (text_slot(group, row, big, &existing) && existing == slot &&
            mm_group_delete(group, row) != MM_STORAGE_OK)
            return 0;
    }
    return 1;
}

static int publish_text_edit(const MMMovieArchiveEditor *editor, const MMSceneArchiveTextEdit *edit,
                             const MMChunkAccess *chunks, MMU32 parent, MMGroup *group, int big, MMU32 previous)
{
    MMChunkChild box;
    MMU32 number, slot;
    int changes_start = !edit->original || !edit->write_box;
    if (edit->original)
    {
        slot = edit->original->slot;
        if (mm_chunk_child_exact(chunks, parent, slot, MM_TEXT_BOX, &box) != MM_CHUNK_OK ||
            box.number != edit->original->number)
            return MM_SCENE_ARCHIVE_TEXT_CHANGED;
    }
    else if (!edit->write_box || !first_text_slot(chunks, parent, &slot))
        return MM_SCENE_ARCHIVE_TEXT_LINK;
    if (changes_start && !update_text_events(group, big, edit, slot))
        return MM_SCENE_ARCHIVE_TEXT_WRITE_START;
    if (edit->write_box)
    {
        if (!edit->write_box(edit->context, edit->original, &number))
            return MM_SCENE_ARCHIVE_TEXT_WRITE_BOX;
        if (!editor->adopt(editor->context, MM_SCENE_CHUNK, edit->scene, MM_TEXT_BOX, number, slot))
            return MM_SCENE_ARCHIVE_TEXT_LINK;
    }
    if (changes_start)
    {
        if (!write_archive_events(editor, MM_SCENE_START_GROUP, group, big, &number) ||
            !editor->adopt(editor->context, MM_SCENE_CHUNK, edit->scene, MM_SCENE_START_GROUP, number, 1))
            return MM_SCENE_ARCHIVE_TEXT_WRITE_START;
        editor->unlink(editor->context, MM_SCENE_CHUNK, edit->scene, MM_SCENE_START_GROUP, previous, 1);
    }
    if (edit->original)
        editor->unlink(editor->context, MM_SCENE_CHUNK, edit->scene, MM_TEXT_BOX, edit->original->number, slot);
    return MM_SCENE_ARCHIVE_TEXT_OK;
}

int mm_scene_archive_edit_text(const MMMovieArchiveEditor *editor, const MMSceneArchiveTextEdit *edit)
{
    MMChunkAccess chunks;
    MMChunkChild previous;
    MMCollectionStorage storage;
    MMGroup group = {0};
    MMU32 parent;
    int big, result;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_find(&chunks, MM_SCENE_CHUNK, edit->scene, &parent) != MM_CHUNK_OK)
        return MM_SCENE_ARCHIVE_TEXT_READ_START;
    mm_storage_init(&storage, NULL);
    group.storage = &storage;
    result = open_text_events(editor, &chunks, parent, &previous, &group, &big);
    if (result == MM_SCENE_ARCHIVE_TEXT_OK)
        result = publish_text_edit(editor, edit, &chunks, parent, &group, big, previous.number);
    mm_storage_dispose(&storage);
    return result;
}

typedef struct ArchiveSoundTag
{
    MMU32 child;
    MMTag tag;
} ArchiveSoundTag;

static void *allocate_packet(void *context, MMI32 bytes)
{
    (void)context;
    return malloc((size_t)bytes);
}
static void copy_packet_tag(void *context, void *out, const void *tag)
{
    (void)context;
    memcpy(out, tag, sizeof(ArchiveSoundTag));
}
static void retain_packet_tag(void *context, void *tag)
{
    (void)context;
    (void)tag; /* Archive tags have no runtime cache owner. */
}
static void read_packet_tag(void *context, const void *record, MMU32 *child, MMTag *tag)
{
    const ArchiveSoundTag *entry = (const ArchiveSoundTag *)record;
    (void)context;
    *child = entry->child;
    *tag = entry->tag;
}
static void write_packet_tag(void *context, void *record, MMU32 child, const MMTag *tag)
{
    ArchiveSoundTag *entry = (ArchiveSoundTag *)record;
    (void)context;
    entry->child = child;
    entry->tag = *tag;
}

static const MMSceneSoundPacketServices archive_packets = {
    NULL, sizeof(ArchiveSoundTag), allocate_packet, copy_packet_tag, NULL, retain_packet_tag, NULL,
    read_packet_tag, write_packet_tag, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

typedef struct FrameSoundChange
{
    MMU32 row;
    MMI32 frame, bytes;
    unsigned char *payload;
} FrameSoundChange;

static int native_swap(int big)
{
    const unsigned short one = 1;
    return big != (*(const unsigned char *)&one == 0);
}

static MMSceneSound *decode_packet(const unsigned char *bytes, MMI32 size, int big)
{
    MMSceneSound *packet;
    void *copy = malloc((size_t)size);
    if (!copy)
        return NULL;
    memcpy(copy, bytes, (size_t)size);
    packet = mm_scene_sound_decode(copy, size, native_swap(big), &archive_packets);
    free(copy);
    return packet;
}

static int store_packet(FrameSoundChange *change, MMSceneSound *packet, int big)
{
    MMU32 value;
    MMI32 i;
    if (!packet)
        return MM_SOUND_ARCHIVE_MEMORY;
    if (!packet->ctagc)
    {
        free(packet);
        return MM_SOUND_ARCHIVE_OK;
    }
    change->payload = (unsigned char *)mm_scene_sound_encode(packet, &change->bytes, &archive_packets);
    free(packet);
    if (!change->payload)
        return MM_SOUND_ARCHIVE_PACKET;
    if (native_swap(big))
        for (i = 0; i < change->bytes; i += 4)
        {
            memcpy(&value, change->payload + i, 4);
            put(change->payload + i, value, 4, big);
        }
    return MM_SOUND_ARCHIVE_OK;
}

static MMSceneSound *new_sound_packet(const MMMovieSoundArchiveEdit *edit, const ArchiveSoundTag *added)
{
    return mm_scene_sound_make(edit->volume, edit->style, edit->loop, 1, added, &archive_packets);
}

static int select_sound_change(SceneFrameEdit *frames, const MMMovieSoundArchiveEdit *edit, MMU32 child,
                               FrameSoundChange *change)
{
    const MMMovieSoundArchiveCue *cue = edit->cue;
    ArchiveSoundTag added = {0};
    ArchiveSoundTag *tags;
    const unsigned char *row, *payload;
    MMSceneSound *packet, *next;
    MMU32 i;
    MMI32 j, frame, size;
    added.child = child;
    added.tag.type = MM_SOUND_CHUNK;
    if (edit->added)
        added.tag.number = *edit->added;
    for (i = 0; i < frames->group.count; ++i)
    {
        row = frame_record(frames, i, &size);
        if (!row)
            return MM_SOUND_ARCHIVE_INVALID_FRAMES;
        if (word(row + 4, frames->big) != MM_SCENE_PLAY_SOUND)
            continue;
        frame = mm_signed_word(word(row, frames->big));
        if ((!cue || frame != cue->frame) && (!edit->added || frame != edit->frame))
            continue;
        payload = row + MM_SCENE_EVENT_BYTES;
        if (mm_signed_word(word(payload + 4, frames->big)) != (cue ? cue->style : edit->style))
            continue;
        packet = decode_packet(payload, size, frames->big);
        if (!packet)
            return MM_SOUND_ARCHIVE_PACKET;
        tags = (ArchiveSoundTag *)(packet + 1);
        for (j = 0; cue && j < packet->ctagc; ++j)
            if (mm_sound_archive_match(cue, &tags[j].tag, tags[j].child, j > 0))
                break;
        change->row = i;
        change->frame = frame;
        if (cue && j < packet->ctagc)
        {
            if (edit->remove)
            {
                --packet->ctagc;
                memmove(tags + j, tags + j + 1, (size_t)(packet->ctagc - j) * sizeof(*tags));
            }
            else
            {
                packet->vlm = edit->volume;
                packet->sty = edit->style;
                packet->fLoop = edit->loop;
                change->frame = edit->frame;
            }
            return store_packet(change, packet, frames->big);
        }
        if (edit->added)
        {
            next = edit->queue && !edit->loop ? mm_scene_sound_append(packet, &added, &archive_packets)
                                              : new_sound_packet(edit, &added);
            free(packet);
            return store_packet(change, next, frames->big);
        }
        free(packet);
    }
    if (!edit->added)
        return MM_SOUND_ARCHIVE_NO_CUE;
    change->row = frames->group.count;
    change->frame = edit->frame;
    return store_packet(change, new_sound_packet(edit, &added), frames->big);
}

typedef struct SoundRowOrder
{
    MMI32 frame;
    MMU32 row;
} SoundRowOrder;

static int compare_sound_row(const void *left, const void *right)
{
    const SoundRowOrder *a = (const SoundRowOrder *)left, *b = (const SoundRowOrder *)right;
    if (a->frame != b->frame)
        return a->frame < b->frame ? -1 : 1;
    return a->row < b->row ? -1 : a->row != b->row;
}

static int rebuild_sound_frames(SceneFrameEdit *frames, const FrameSoundChange *change, MMGroup *output)
{
    SoundRowOrder *order;
    MMU32 rows, i;
    const unsigned char *row, *payload;
    unsigned char fixed[MM_SCENE_EVENT_BYTES];
    MMI32 size;
    int result;
    rows = frames->group.count + (change->row == frames->group.count);
    if ((size_t)rows > (size_t)-1 / sizeof(*order))
        return MM_SOUND_ARCHIVE_MEMORY;
    order = (SoundRowOrder *)malloc((size_t)rows * sizeof(*order));
    if (!order)
        return MM_SOUND_ARCHIVE_MEMORY;
    result = MM_SOUND_ARCHIVE_INVALID_FRAMES;
    for (i = 0; i < rows; ++i)
    {
        order[i].row = i;
        if (i == change->row)
            order[i].frame = change->frame;
        else
        {
            row = frame_record(frames, i, NULL);
            if (!row)
                goto done;
            order[i].frame = mm_signed_word(word(row, frames->big));
        }
    }
    qsort(order, rows, sizeof(*order), compare_sound_row);
    for (i = 0; i < rows; ++i)
    {
        if (order[i].row == change->row)
        {
            if (!change->payload)
                continue;
            put(fixed, (MMU32)change->frame, 4, frames->big);
            put(fixed + 4, MM_SCENE_PLAY_SOUND, 4, frames->big);
            row = fixed;
            size = change->bytes;
            payload = change->payload;
        }
        else
        {
            row = frame_record(frames, order[i].row, &size);
            if (!row)
                goto done;
            payload = row + MM_SCENE_EVENT_BYTES;
        }
        if (mm_group_insert(output, output->count, (MMU32)size, payload, row) != MM_STORAGE_OK)
        {
            result = MM_SOUND_ARCHIVE_MEMORY;
            goto done;
        }
    }
    result = MM_SOUND_ARCHIVE_OK;
done:
    free(order);
    return result;
}

int mm_scene_archive_edit_sound(const MMMovieArchiveEditor *editor, MMU32 scene,
                                const MMMovieSoundArchiveEdit *edit, MMU32 child)
{
    SceneFrameEdit frames;
    FrameSoundChange change = {0};
    MMCollectionStorage storage;
    MMGroup output = {NULL, MM_SCENE_EVENT_BYTES, 0, 0, -1, 0};
    int result;
    init_frame_edit(&frames, editor, scene);
    mm_storage_init(&storage, NULL);
    output.storage = &storage;
    if (!open_frames(&frames))
        result = frames.result == MM_SCENE_ARCHIVE_EDIT_READ_FRAMES ? MM_SOUND_ARCHIVE_READ_FRAMES :
            MM_SOUND_ARCHIVE_INVALID_FRAMES;
    else
    {
        result = select_sound_change(&frames, edit, child, &change);
        if (result == MM_SOUND_ARCHIVE_OK)
            result = rebuild_sound_frames(&frames, &change, &output);
        if (result == MM_SOUND_ARCHIVE_OK && !publish_frames(&frames, &output))
            result = frames.result == MM_SCENE_ARCHIVE_EDIT_LINK_FRAMES ? MM_SOUND_ARCHIVE_LINK_FRAMES :
                MM_SOUND_ARCHIVE_WRITE_FRAMES;
    }
    free(change.payload);
    mm_storage_dispose(&storage);
    mm_storage_dispose(&frames.storage);
    return result;
}

void *mm_scene_read_event_group(int start, int native_big, int *order, const MMSceneReadGroupServices *s)
{
    int loaded_order;
    void *group = s->read(s->context, &loaded_order);
    if (!group)
        return NULL;
    if ((loaded_order != MM_SCENE_GROUP_NATIVE_ORDER && loaded_order != MM_SCENE_GROUP_OTHER_ORDER) ||
        !mm_scene_valid_events(
            group, (loaded_order == MM_SCENE_GROUP_NATIVE_ORDER) == (native_big != 0), start, &s->groups))
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
    MMChunkAccess chunks;
    MMChunkChild old_frame, old_start;
    MMU32 scene_row, frame = 0xffffffffUL, start = 0xffffffffUL;
    int had_frame = 0, had_start = 0, created_frame = 0, created_start = 0, adopted_frame = 0, adopted_start = 0;
    const unsigned short one = 1;
    int big = *(const unsigned char *)&one == 0;
    if (!mm_scene_valid_events(frames, big, 0, &s->groups) ||
        !mm_scene_valid_events(starts, big, 1, &s->groups))
        return 0;
    chunks = s->chunks(s->context);
    if (mm_chunk_find(&chunks, MM_SCENE_CHUNK, scene, &scene_row) == MM_CHUNK_OK)
    {
        had_frame = mm_chunk_child_exact(&chunks, scene_row, 0, MM_SCENE_FRAME_GROUP, &old_frame) == MM_CHUNK_OK;
        had_start = mm_chunk_child_exact(&chunks, scene_row, 1, MM_SCENE_START_GROUP, &old_start) == MM_CHUNK_OK;
    }
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
        s->remove(s->context, scene, 0, old_frame.number, 1);
    if (had_start)
        s->remove(s->context, scene, 1, old_start.number, 1);
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
    MMSceneTrack frame_track, start_track;
    void *frames = &frame_track, *starts = &start_track;
    int created, success;
    if (s->new_group)
    {
        frames = s->new_group(s->io.context);
        starts = s->new_group(s->io.context);
    }
    else
    {
        mm_scene_track_init(&frame_track, NULL, 0);
        mm_scene_track_init(&start_track, NULL, 0);
    }
    *number = 0xffffffffUL;
    created = frames && starts && s->add_scene(s->io.context, 16, number);
    success = created && mm_scene_write(state, *number, system, big, &s->io) &&
              mm_scene_write_events(*number, frames, starts, &s->events);
    if (s->new_group)
    {
        if (frames)
            s->release_group(frames);
        if (starts)
            s->release_group(starts);
    }
    else
    {
        mm_scene_track_dispose(&frame_track);
        mm_scene_track_dispose(&start_track);
    }
    if (!success && created)
        s->remove_scene(s->io.context, *number);
    if (!success)
        *number = 0xffffffffUL;
    return success;
}
