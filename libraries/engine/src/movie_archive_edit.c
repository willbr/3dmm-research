#include "mm/movie_archive_edit.h"
#include "mm/chunk_edit.h"
#include "mm/chunk_graph.h"
#include "mm/collections.h"
#include "mm/material_resource.h"
#include "mm/movie_root.h"
#include "mm/scene_data.h"
#include <stdio.h>
#include <stdlib.h>

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

typedef struct BackgroundRebind
{
    BackgroundReplacement replacement;
    const MMMovieEmbeddedArchive *assets;
} BackgroundRebind;

static int copy_background(void *context, MMU32 number, MMU32 *mapped)
{
    const BackgroundRebind *rebind = (const BackgroundRebind *)context;
    MMTag tag = {0};
    tag.type = BACKGROUND_TYPE;
    tag.number = number;
    if (!mm_movie_archive_embed_tag(rebind->assets, SCENE_TYPE, rebind->replacement.scene, &tag))
        return 0;
    *mapped = tag.number;
    return 1;
}

static int receive_rebound_backgrounds(void *context, const unsigned char *data, MMU32 bytes)
{
    BackgroundRebind *rebind = (BackgroundRebind *)context;
    return receive_background(&rebind->replacement, data, bytes);
}

int mm_movie_archive_rebind_backgrounds(const MMMovieArchiveEditor *editor, const MMMovieEmbeddedArchive *assets,
                                         MMU32 scene)
{
    BackgroundRebind rebind;
    MMChunkAccess chunks;
    MMReadStream stream;
    int status;
    chunks = editor->chunks(editor->context);
    rebind.replacement.editor = editor;
    rebind.replacement.scene = scene;
    rebind.replacement.previous.type = START_GROUP_TYPE;
    rebind.replacement.previous.slot = 1;
    rebind.assets = assets;
    status = mm_chunk_link_number(&chunks, SCENE_TYPE, scene, 1, START_GROUP_TYPE, &rebind.replacement.previous.number);
    if (status == MM_CHUNK_NOT_FOUND)
        return 1; /* Legacy scenes may omit the start group. */
    return status == MM_CHUNK_OK &&
        editor->read(editor->context, START_GROUP_TYPE, rebind.replacement.previous.number, &stream) &&
        mm_scene_rebind_start_backgrounds(&stream, copy_background, receive_rebound_backgrounds, &rebind);
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

int mm_movie_archive_embed_tag(const MMMovieEmbeddedArchive *s, MMU32 parent_type, MMU32 parent, MMTag *tag)
{
    MMChunkAccess chunks;
    MMChunkChild link, existing;
    MMU32 row, i;
    int created;
    link.type = tag->type;
    if (!s->copy_reference(s->context, tag, &link.number, &created))
        return 0;
    chunks = s->chunks(s->context);
    if (mm_chunk_find(&chunks, parent_type, parent, &row) != MM_CHUNK_OK)
        goto fail;
    for (i = 0; mm_chunk_child_at(&chunks, row, i, &existing) == MM_CHUNK_OK; ++i)
        if (existing.type == link.type && existing.number == link.number)
        {
            tag->number = link.number;
            return 1;
        }
    if (!mm_movie_archive_next_child(&chunks, parent_type, parent, link.type, &link.slot) ||
        !s->adopt(s->context, parent_type, parent, &link, created))
        goto fail;
    tag->number = link.number;
    return 1;
fail:
    if (created)
        s->discard(s->context, link.type, link.number);
    return 0;
}

typedef struct SceneAssetGraph
{
    const MMChunkAccess *chunks;
    int failed;
} SceneAssetGraph;

static MMU32 scene_delete_children(void *p, MMU32 type, MMU32 number)
{
    SceneAssetGraph *graph = (SceneAssetGraph *)p;
    const MMChunkAccess *chunks = graph->chunks;
    MMChunkEntry entry;
    MMU32 row;
    if (mm_chunk_find(chunks, type, number, &row) == MM_CHUNK_OK && mm_chunk_at(chunks, row, &entry) == MM_CHUNK_OK)
        return entry.child_count;
    graph->failed = 1;
    return 0;
}

static int scene_delete_child(void *p, MMU32 type, MMU32 number, MMU32 index, MMChunkChild *child)
{
    SceneAssetGraph *graph = (SceneAssetGraph *)p;
    const MMChunkAccess *chunks = graph->chunks;
    MMU32 row;
    int status = mm_chunk_find(chunks, type, number, &row);
    if (status == MM_CHUNK_OK)
    {
        status = mm_chunk_child_at(chunks, row, index, child);
        if (status == MM_CHUNK_OK || status == MM_CHUNK_NOT_FOUND)
            return status == MM_CHUNK_OK;
    }
    graph->failed = 1;
    return 0;
}

int mm_movie_archive_preserve_scene_assets(const MMChunkAccess *chunks, MMU32 scene,
                                            const MMStorageAllocator *allocator, void *context,
                                            void (*keep)(void *, MMU32 type, MMU32 number))
{
    static const MMU32 types[] = {BACKGROUND_TYPE, MM_MATERIAL_CHUNK};
    SceneAssetGraph source = {NULL, 0};
    MMChunkGraph graph = {NULL, scene_delete_children, scene_delete_child};
    MMChunkWalk walk;
    MMChunkEntry entry;
    MMChunkChild node;
    MMU32 row, flags, first[2], kept[2], count = 0, i, j, *removed;
    int status, skip = 0, ok = 0;
    source.chunks = chunks;
    graph.context = &source;
    for (i = 0; i < 2; ++i)
    {
        status = mm_chunk_type_at(chunks, types[i], 0, &entry, &first[i]);
        if (status == MM_CHUNK_NOT_FOUND)
            first[i] = chunks->count;
        else if (status != MM_CHUNK_OK)
            return 0;
    }
    if (first[0] == chunks->count && first[1] == chunks->count)
        return 1;
    if (mm_chunk_find(chunks, SCENE_TYPE, scene, &row) != MM_CHUNK_OK ||
        mm_chunk_at(chunks, row, &entry) != MM_CHUNK_OK || !entry.parent_count)
        return 0;
    if ((entry.flags & MM_CHUNK_LONER) || entry.parent_count > 1 || !entry.child_count)
        return 1;
    if ((size_t)chunks->count > (size_t)-1 / sizeof(*removed) ||
        mm_chunk_walk_init(&walk, allocator) != MM_CHUNK_OK)
        return 0;
    removed = (MMU32 *)calloc((size_t)chunks->count, sizeof(*removed));
    if (!removed || mm_chunk_walk_reset(&walk, &graph, SCENE_TYPE, scene) != MM_CHUNK_OK)
        goto done;
    /* Preview the same last-reference rule as ChunkyFile::DeleteChild. Follow
     * a shared node once, if all of its incoming links are being dropped. */
    while (mm_chunk_walk_next(&walk, skip, &node, NULL, &flags))
    {
        skip = 0;
        if (source.failed || (flags & MM_WALK_ERROR))
            goto done;
        if (!(flags & MM_WALK_PRE))
            continue;
        if (mm_chunk_find(chunks, node.type, node.number, &row) != MM_CHUNK_OK ||
            mm_chunk_at(chunks, row, &entry) != MM_CHUNK_OK || removed[row] >= entry.parent_count)
            goto done;
        ++removed[row];
        skip = (entry.flags & MM_CHUNK_LONER) || removed[row] < entry.parent_count ||
               node.type == BACKGROUND_TYPE || node.type == MM_MATERIAL_CHUNK;
    }
    if (source.failed)
        goto done;
    /* Compact the keep list in index order. Its write position never gets
     * ahead of the row whose removal count is being read. */
    for (i = 0; i < 2; ++i)
    {
        for (row = first[i]; row < chunks->count; ++row)
        {
            if (mm_chunk_at(chunks, row, &entry) != MM_CHUNK_OK)
                goto done;
            if (entry.type != types[i])
                break;
            if (removed[row] == entry.parent_count && removed[row] && !(entry.flags & MM_CHUNK_LONER))
                removed[count++] = entry.number;
        }
        kept[i] = count;
    }
    for (i = 0, j = 0; i < 2; ++i)
        for (; j < kept[i]; ++j)
            keep(context, types[i], removed[j]);
    ok = 1;
done:
    free(removed);
    mm_chunk_walk_dispose(&walk);
    return ok;
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
