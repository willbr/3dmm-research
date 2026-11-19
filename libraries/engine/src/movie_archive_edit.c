#include "mm/movie_archive_edit.h"
#include "mm/chunk_edit.h"
#include "mm/collections.h"
#include "mm/movie_root.h"
#include "mm/scene_data.h"
#include <stdio.h>

#define MOVIE_TYPE 0x4d564945UL
#define SCENE_TYPE 0x5343454eUL
#define FRAME_GROUP_TYPE 0x47474652UL
#define START_GROUP_TYPE 0x47475354UL
#define ACTOR_TYPE 0x41435452UL
#define ROLLCALL_TYPE 0x47535420UL
#define BACKGROUND_TYPE 0x424b4744UL
#define THUMB_TYPE 0x5448554dUL

static int add_record(const MMMovieArchiveEditor *editor, MMU32 type, const void *bytes, MMU32 size, MMU32 *number)
{
    MMStorageWriter writer;
    return editor->add(editor->context, type, size, number, &writer) &&
           writer.write_at(writer.context, 0, bytes, size);
}

static int write_child(void *context, MMU32 type, const unsigned char *data, MMU32 bytes, MMU32 *number)
{
    const MMMovieArchiveEditor *editor = (const MMMovieArchiveEditor *)context;
    MMStorageWriter writer;
    if (!editor->add(editor->context, type, bytes, number, &writer))
        return 0;
    if (writer.write_at(writer.context, 0, data, bytes))
        return 1;
    if (editor->discard)
        editor->discard(editor->context, type, *number);
    return 0;
}

static int adopt_child(void *context, MMU32 parent_type, MMU32 parent, const MMChunkChild *child)
{
    const MMMovieArchiveEditor *editor = (const MMMovieArchiveEditor *)context;
    return editor->adopt(editor->context, parent_type, parent, child->type, child->number, child->slot);
}

static void remove_child(void *context, MMU32 parent_type, MMU32 parent, const MMChunkChild *child, int linked)
{
    const MMMovieArchiveEditor *editor = (const MMMovieArchiveEditor *)context;
    if (linked)
        editor->unlink(editor->context, parent_type, parent, child->type, child->number, child->slot);
    else if (editor->discard)
        editor->discard(editor->context, child->type, child->number);
}

int mm_movie_archive_replace_child(const MMMovieArchiveEditor *editor, MMU32 parent_type, MMU32 parent,
                                   const MMChunkChild *child, int had_child, const unsigned char *data, MMU32 bytes)
{
    MMChunkChildPayload services = {NULL, write_child, adopt_child, remove_child};
    services.context = (void *)editor;
    return mm_chunk_replace_child_bytes(parent_type, parent, child, had_child, data, bytes, &services);
}

typedef struct BackgroundReplacement
{
    const MMMovieArchiveEditor *editor;
    MMChunkChild previous;
    MMU32 scene;
} BackgroundReplacement;

static int receive_background(void *context, const unsigned char *data, MMU32 bytes)
{
    const BackgroundReplacement *replacement = (const BackgroundReplacement *)context;
    return mm_movie_archive_replace_child(replacement->editor, SCENE_TYPE, replacement->scene,
                                          &replacement->previous, 1, data, bytes);
}

int mm_movie_archive_retarget_background(const MMMovieArchiveEditor *editor, MMU32 scene, MMI32 source, MMU32 background)
{
    MMChunkAccess chunks;
    MMReadStream stream;
    BackgroundReplacement replacement;
    chunks = editor->chunks(editor->context);
    replacement.editor = editor;
    replacement.scene = scene;
    replacement.previous.type = START_GROUP_TYPE;
    replacement.previous.slot = 1;
    return mm_chunk_link_number(&chunks, SCENE_TYPE, scene, 1, START_GROUP_TYPE, &replacement.previous.number) == MM_CHUNK_OK &&
        editor->read(editor->context, START_GROUP_TYPE, replacement.previous.number, &stream) &&
        mm_scene_retarget_start_background(&stream, source, background, receive_background, &replacement);
}

static void unlink_scene_child(const MMMovieArchiveEditor *editor, MMU32 scene, MMU32 type)
{
    MMChunkAccess chunks;
    MMU32 child;
    chunks = editor->chunks(editor->context);
    if (mm_chunk_link_number(&chunks, SCENE_TYPE, scene, 0, type, &child) == MM_CHUNK_OK)
        editor->unlink(editor->context, SCENE_TYPE, scene, type, child, 0);
}

int mm_movie_archive_embed_storyboard(const MMMovieArchiveEditor *editor, MMU32 scene, MMU32 background,
                                       const unsigned char *metadata, MMU32 bytes)
{
    MMChunkChild child = {0};
    unlink_scene_child(editor, scene, BACKGROUND_TYPE);
    unlink_scene_child(editor, scene, MM_STORYBOARD_CHUNK);
    child.type = MM_STORYBOARD_CHUNK;
    if (!mm_movie_archive_replace_child(editor, SCENE_TYPE, scene, &child, 0, metadata, bytes) ||
        !editor->adopt(editor->context, SCENE_TYPE, scene, BACKGROUND_TYPE, background, 0) ||
        !mm_movie_archive_retarget_background(editor, scene, 0, background))
        return 0;
    unlink_scene_child(editor, scene, THUMB_TYPE);
    return 1;
}

int mm_movie_archive_next_child(const MMChunkAccess *chunks, MMU32 parent_type, MMU32 parent,
                                 MMU32 type, MMU32 *slot)
{
    MMChunkChild child;
    MMU32 position, i, next;
    next = 0;
    if (mm_chunk_find(chunks, parent_type, parent, &position) == MM_CHUNK_OK)
        for (i = 0; mm_chunk_child_at(chunks, position, i, &child) == MM_CHUNK_OK; ++i)
            if (child.type == type)
            {
                if (child.slot >= 0x7fffffffUL)
                    return 0;
                if (child.slot >= next)
                    next = child.slot + 1;
            }
    *slot = next;
    return 1;
}

int mm_movie_archive_ensure_root(const MMMovieArchiveEditor *editor, MMMovieArchiveRoot *out)
{
    MMMovieArchiveRoot root;
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMU32 position, scene;
    unsigned char header[MM_MOVIE_PREFIX_BYTES];
    int status;
    chunks = editor->chunks(editor->context);
    status = mm_chunk_type_at(&chunks, MOVIE_TYPE, 0, &entry, &position);
    if (status == MM_CHUNK_OK)
    {
        root.number = entry.number;
        root.created = 0;
    }
    else
    {
        if (status != MM_CHUNK_NOT_FOUND)
            return MM_MOVIE_SCENE_ARCHIVE_ROOT_WRITE;
        mm_movie_encode_root(editor->system, editor->big_endian, header);
        if (!add_record(editor, MOVIE_TYPE, header, sizeof(header), &root.number))
            return MM_MOVIE_SCENE_ARCHIVE_ROOT_WRITE;
        root.created = 1;
        chunks = editor->chunks(editor->context);
        for (scene = 0;; ++scene)
        {
            status = mm_chunk_type_at(&chunks, SCENE_TYPE, scene, &entry, &position);
            if (status == MM_CHUNK_NOT_FOUND)
                break;
            if (status != MM_CHUNK_OK ||
                !editor->adopt(editor->context, MOVIE_TYPE, root.number, SCENE_TYPE, entry.number, scene))
                return MM_MOVIE_SCENE_ARCHIVE_ROOT_LINK;
        }
    }
    *out = root;
    return MM_MOVIE_SCENE_ARCHIVE_OK;
}

static int name_scene(MMSceneState *state, unsigned unit, MMU32 ordinal)
{
    char label[sizeof("Scene ") + 10];
    unsigned short wide[sizeof(label)];
    int count, i;
    if (!mm_scene_init(state, unit))
        return 0;
    count = sprintf(label, "Scene %lu", (unsigned long)ordinal);
    if (unit == 1)
        return mm_movie_title_set(&state->name, label, (MMU32)count);
    for (i = 0; i < count; ++i)
        wide[i] = (unsigned char)label[i];
    return mm_movie_title_set(&state->name, wide, (MMU32)count);
}

typedef struct SceneGroups
{
    MMU32 frames, starts;
} SceneGroups;

static int add_groups(const MMMovieArchiveEditor *editor, MMU32 scene, const unsigned char *empty,
                      const unsigned char *starts, MMU32 start_size, SceneGroups *groups)
{
    return add_record(editor, FRAME_GROUP_TYPE, empty, MM_SCENE_EMPTY_GROUP_BYTES, &groups->frames) &&
           add_record(editor, START_GROUP_TYPE, starts, start_size, &groups->starts) &&
           editor->adopt(editor->context, SCENE_TYPE, scene, FRAME_GROUP_TYPE, groups->frames, 0) &&
           editor->adopt(editor->context, SCENE_TYPE, scene, START_GROUP_TYPE, groups->starts, 1);
}

int mm_movie_archive_append_scene(const MMMovieArchiveEditor *editor, MMU32 root, MMU32 scene,
                                  MMI32 source, MMU32 background)
{
    MMChunkAccess chunks;
    MMSceneState state;
    SceneGroups initial, final;
    MMU32 slot, number, position, first, end;
    unsigned char header[MM_SCENE_HEADER_BYTES], empty[MM_SCENE_EMPTY_GROUP_BYTES];
    unsigned char starts[MM_SCENE_BACKGROUND_GROUP_BYTES];
    int status;
    chunks = editor->chunks(editor->context);
    if (!mm_movie_archive_next_child(&chunks, MOVIE_TYPE, root, SCENE_TYPE, &slot))
        return MM_MOVIE_SCENE_ARCHIVE_SLOT;
    if (mm_chunk_find(&chunks, SCENE_TYPE, scene, &position) != MM_CHUNK_NOT_FOUND)
        return MM_MOVIE_SCENE_ARCHIVE_CREATE;
    status = mm_chunk_find(&chunks, SCENE_TYPE, 0, &first);
    if (status != MM_CHUNK_OK && status != MM_CHUNK_NOT_FOUND)
        return MM_MOVIE_SCENE_ARCHIVE_CREATE;
    status = mm_chunk_find(&chunks, SCENE_TYPE + 1, 0, &end);
    if ((status != MM_CHUNK_OK && status != MM_CHUNK_NOT_FOUND) ||
        !name_scene(&state, editor->text_unit, end - first + 1) ||
        !mm_scene_encode_header(&state, editor->system, editor->big_endian, header) ||
        !mm_scene_encode_empty_group(editor->system, editor->big_endian, empty) ||
        !mm_scene_encode_background_group(editor->system, editor->big_endian, state.firstFrame, source,
                                           background, starts) ||
        !editor->add(editor->context, SCENE_TYPE, sizeof(header), &number, NULL) ||
        !editor->put(editor->context, SCENE_TYPE, number, header, sizeof(header)) ||
        !editor->set_title(editor->context, SCENE_TYPE, number, &state.name) ||
        !add_groups(editor, number, empty, empty, sizeof(empty), &initial))
        return MM_MOVIE_SCENE_ARCHIVE_CREATE;
    if (number != scene && !editor->rekey(editor->context, SCENE_TYPE, number, scene))
        return MM_MOVIE_SCENE_ARCHIVE_CREATE;
    if (!add_groups(editor, scene, empty, starts, sizeof(starts), &final))
        return MM_MOVIE_SCENE_ARCHIVE_LINK;
    editor->unlink(editor->context, SCENE_TYPE, scene, FRAME_GROUP_TYPE, initial.frames, 0);
    editor->unlink(editor->context, SCENE_TYPE, scene, START_GROUP_TYPE, initial.starts, 1);
    if (!editor->adopt(editor->context, MOVIE_TYPE, root, SCENE_TYPE, scene, slot))
        return MM_MOVIE_SCENE_ARCHIVE_LINK;
    return MM_MOVIE_SCENE_ARCHIVE_OK;
}

int mm_movie_archive_ensure_empty_rollcall(const MMMovieArchiveEditor *editor)
{
    MMChunkAccess chunks;
    MMChunkEntry root, actor;
    MMCollectionHeader header = {0};
    MMU32 position, number;
    unsigned char bytes[20];
    int status;
    chunks = editor->chunks(editor->context);
    status = mm_chunk_type_at(&chunks, MOVIE_TYPE, 0, &root, &position);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND;
    status = mm_chunk_type_at(&chunks, ACTOR_TYPE, 0, &actor, &position);
    if (status != MM_CHUNK_NOT_FOUND)
        return status == MM_CHUNK_OK;
    status = mm_chunk_link_number(&chunks, MOVIE_TYPE, root.number, 0, ROLLCALL_TYPE, &number);
    if (status != MM_CHUNK_NOT_FOUND)
        return status == MM_CHUNK_OK;
    header.kind = MM_COLLECTION_STRINGS;
    header.width = MM_MOVIE_ACTOR_BYTES + 4;
    header.free_count = -1;
    header.system = editor->system;
    header.big_endian = editor->big_endian;
    return mm_collection_header_write(&header, bytes, sizeof(bytes)) == MM_STORAGE_OK &&
           add_record(editor, ROLLCALL_TYPE, bytes, sizeof(bytes), &number) &&
           editor->adopt(editor->context, MOVIE_TYPE, root.number, ROLLCALL_TYPE, number, 0);
}
