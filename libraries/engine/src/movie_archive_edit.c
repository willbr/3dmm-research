#include "mm/movie_archive_edit.h"
#include "mm/chunk_copy.h"
#include "mm/chunk_graph.h"
#include "mm/chunk_ops.h"
#include "mm/collections.h"
#include "mm/material_resource.h"
#include "mm/movie_root.h"
#include "mm/movie_sound.h"
#include "mm/scene_data.h"
#include <stdio.h>
#include <string.h>

#define MOVIE_TYPE 0x4d564945UL
#define SCENE_TYPE 0x5343454eUL
#define START_GROUP_TYPE 0x47475354UL
#define ACTOR_TYPE 0x41435452UL
#define ROLLCALL_TYPE 0x47535420UL
#define BACKGROUND_TYPE 0x424b4744UL
#define THUMB_TYPE 0x5448554dUL

int mm_movie_archive_file_init(MMMovieArchiveFile *binding, void *file, const MMArchiveWriter *archive,
                                const MMChunkTitleIO *titles)
{
    const MMChunkPayloadServices *payloads;
    if (!binding || !file || !archive || !archive->payloads || !titles)
        return 0;
    payloads = archive->payloads;
    if (mm_owned_block_init(&binding->input, payloads->handles, payloads->files) != MM_DATA_OK ||
        mm_owned_block_init(&binding->output, payloads->handles, payloads->files) != MM_DATA_OK)
        return 0;
    binding->file = file;
    binding->archive = *archive;
    binding->titles = *titles;
    return 1;
}

void mm_movie_archive_file_clear(MMMovieArchiveFile *binding)
{
    mm_owned_block_clear(&binding->input);
    mm_owned_block_clear(&binding->output);
}

static int read_payload(void *p, MMU32 type, MMU32 number, MMReadStream *stream)
{
    MMMovieArchiveFile *binding = p;
    const MMChunkPayloadServices *services = binding->archive.payloads;
    MMOwnedBlock *block = &binding->input;
    mm_owned_block_clear(block);
    return mm_chunk_payload_bind_block(services, binding->file, type, number, block) == MM_CHUNK_PAYLOAD_OK &&
           mm_owned_block_code(block, 0, services->decode_context, services->decode, NULL) == MM_DATA_OK &&
           mm_owned_block_io(block, &binding->input_io) == MM_DATA_OK &&
           mm_block_io_stream(&binding->input_io, stream) == MM_DATA_OK;
}

static int add_payload(void *p, MMU32 type, MMU32 bytes, MMU32 *number, MMStorageWriter *writer)
{
    MMMovieArchiveFile *binding = p;
    const MMArchiveWriter *archive = &binding->archive;
    MMOwnedBlock *block = writer ? &binding->output : NULL;
    if (block)
        mm_owned_block_clear(block);
    if (mm_chunk_add_block(&archive->edit, type, bytes, number, block) == MM_CHUNK_OK)
    {
        if (!writer || (mm_owned_block_io(block, &binding->output_io) == MM_DATA_OK &&
                        mm_block_io_writer(&binding->output_io, writer) == MM_DATA_OK))
            return 1;
        mm_chunk_delete(&archive->edit, type, *number);
        mm_owned_block_clear(block);
    }
    if (archive->invalidate_number)
        archive->invalidate_number(number);
    return 0;
}

static int put_payload(void *p, MMU32 type, MMU32 number, const void *bytes, MMU32 size)
{
    const MMArchiveWriter *archive = &((MMMovieArchiveFile *)p)->archive;
    return mm_chunk_put_bytes(&archive->edit, archive->payloads->files, type, number, bytes, size) == MM_CHUNK_OK;
}

static int set_title(void *p, MMU32 type, MMU32 number, const MMMovieTitle *title)
{
    const MMMovieArchiveFile *binding = p;
    return mm_chunk_title_write(title, binding->file, type, number, &binding->titles);
}

static int replace_payload(void *p, MMU32 type, MMU32 number, MMU32 bytes, MMChunkWritePayload write, void *context)
{
    const MMArchiveWriter *archive = &((const MMMovieArchiveFile *)p)->archive;
    return mm_chunk_put_payload(archive, type, number, bytes, write, context) == MM_CHUNK_OK;
}

static int patch_payload(void *p, MMU32 type, MMU32 number, MMU32 offset, const void *bytes, MMU32 size)
{
    const MMMovieArchiveFile *binding = p;
    return mm_chunk_patch_payload(&binding->archive, binding->file, type, number, offset, bytes, size) == MM_CHUNK_OK;
}

MMMovieArchiveEditor mm_movie_archive_file_editor(MMMovieArchiveFile *binding, unsigned text_unit,
                                                   MMStringConvert convert)
{
    MMMovieArchiveEditor editor = {0};
    editor.context = binding;
    editor.text_unit = text_unit;
    editor.system = (int)binding->archive.system;
    editor.big_endian = binding->archive.big_endian;
    editor.convert_text = convert;
    editor.read = read_payload;
    editor.add = add_payload;
    editor.put = put_payload;
    editor.set_title = set_title;
    editor.replace = replace_payload;
    editor.patch = patch_payload;
    editor.edit = binding->archive.edit;
    return editor;
}

static int add_record(const MMMovieArchiveEditor *editor, MMU32 type, const void *bytes, MMU32 size, MMU32 *number)
{
    MMStorageWriter writer;
    return editor->add(editor->context, type, size, number, &writer) &&
           writer.write_at(writer.context, 0, bytes, size);
}

static int adopt_chunk(const MMMovieArchiveEditor *editor, MMU32 parent_type, MMU32 parent,
                       MMU32 type, MMU32 number, MMU32 slot)
{
    MMChunkChild child;
    child.type = type;
    child.number = number;
    child.slot = slot;
    return mm_chunk_adopt(&editor->edit, parent_type, parent, &child, 1) == MM_CHUNK_OK;
}

int mm_movie_archive_replace_child(const MMMovieArchiveEditor *editor, MMU32 parent_type, MMU32 parent,
                                   const MMChunkChild *child, int had_child, const unsigned char *data, MMU32 bytes)
{
    MMChunkChild replacement = *child;
    MMStorageWriter writer;
    if (!editor->add(editor->context, child->type, bytes, &replacement.number, &writer))
        return 0;
    if (!writer.write_at(writer.context, 0, data, bytes) ||
        mm_chunk_adopt(&editor->edit, parent_type, parent, &replacement, 1) != MM_CHUNK_OK)
    {
        mm_chunk_delete(&editor->edit, replacement.type, replacement.number);
        return 0;
    }
    if (had_child)
        mm_chunk_unlink(&editor->edit, parent_type, parent, child);
    return 1;
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
    chunks = mm_chunk_edit_access(&editor->edit);
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
    chunks = mm_chunk_edit_access(&editor->edit);
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
    MMChunkChild child = {0};
    child.type = type;
    chunks = mm_chunk_edit_access(&editor->edit);
    if (mm_chunk_link_number(&chunks, SCENE_TYPE, scene, child.slot, child.type, &child.number) == MM_CHUNK_OK)
        mm_chunk_unlink(&editor->edit, SCENE_TYPE, scene, &child);
}

int mm_movie_archive_embed_storyboard(const MMMovieArchiveEditor *editor, MMU32 scene, MMU32 background,
                                       const unsigned char *metadata, MMU32 bytes)
{
    MMChunkChild child = {0};
    unlink_scene_child(editor, scene, BACKGROUND_TYPE);
    unlink_scene_child(editor, scene, MM_STORYBOARD_CHUNK);
    child.type = MM_STORYBOARD_CHUNK;
    if (!mm_movie_archive_replace_child(editor, SCENE_TYPE, scene, &child, 0, metadata, bytes) ||
        !adopt_chunk(editor, SCENE_TYPE, scene, BACKGROUND_TYPE, background, 0) ||
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

static int copy_asset_status(void *context, const MMTag *tag, MMU32 *number, int *created)
{
    const MMMovieArchiveAssets *assets = context;
    MMResourceChunkCopy binding;
    MMChunkEndpoint source;
    if (!tag->cache)
        return MM_CHUNK_NOT_FOUND;
    source = assets->tags->bind_cache(tag->cache, &binding);
    return mm_chunk_copy_reference(&source, assets->destination, tag->type, tag->number,
                                   number, created);
}

static int copy_asset(void *context, const MMTag *tag, MMU32 *number, int *created)
{
    return copy_asset_status(context, tag, number, created) == MM_CHUNK_OK;
}

MMMovieEmbeddedArchive mm_movie_archive_assets(MMMovieArchiveAssets *assets)
{
    MMMovieEmbeddedArchive archive;
    archive.context = assets;
    archive.copy_reference = copy_asset;
    archive.edit = assets->destination->edit;
    archive.copy_sound = copy_asset_status;
    return archive;
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
    chunks = mm_chunk_edit_access(&s->edit);
    if (mm_chunk_find(&chunks, parent_type, parent, &row) != MM_CHUNK_OK)
        goto fail;
    for (i = 0; mm_chunk_child_at(&chunks, row, i, &existing) == MM_CHUNK_OK; ++i)
        if (existing.type == link.type && existing.number == link.number)
        {
            tag->number = link.number;
            return 1;
        }
    if (!mm_movie_archive_next_child(&chunks, parent_type, parent, link.type, &link.slot) ||
        mm_chunk_adopt(&s->edit, parent_type, parent, &link, created) != MM_CHUNK_OK)
        goto fail;
    tag->number = link.number;
    return 1;
fail:
    if (created)
        mm_chunk_delete(&s->edit, link.type, link.number);
    return 0;
}

int mm_movie_archive_embed_sound(const MMMovieEmbeddedArchive *s, MMU32 scene, MMU32 slot, MMTag *tag)
{
    MMChunkAccess chunks;
    MMChunkChild link = {MM_SOUND_CHUNK, 0, 0};
    MMTag source;
    MMU32 row;
    int status, created = 0;
    if (tag->sid)
        return 1;
    chunks = mm_chunk_edit_access(&s->edit);
    status = mm_chunk_link_number(&chunks, SCENE_TYPE, scene, slot, MM_SOUND_CHUNK, &link.number);
    if (status == MM_CHUNK_NOT_FOUND)
    {
        source = *tag;
        source.type = MM_SOUND_CHUNK;
        if (s->copy_sound)
            status = s->copy_sound(s->context, &source, &link.number, &created);
        if (status == MM_CHUNK_NOT_FOUND)
        {
            link.number = tag->number;
            status = mm_chunk_find(&chunks, MM_SOUND_CHUNK, link.number, &row);
            if (status == MM_CHUNK_NOT_FOUND)
                return 1;
        }
        if (status != MM_CHUNK_OK)
            return 0;
        link.slot = slot;
        if (mm_chunk_adopt(&s->edit, SCENE_TYPE, scene, &link, 1) != MM_CHUNK_OK)
        {
            if (created)
                mm_chunk_delete(&s->edit, link.type, link.number);
            return 0;
        }
    }
    else if (status != MM_CHUNK_OK)
        return 0;
    tag->number = link.number;
    return 1;
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

int mm_movie_archive_preserve_scene_assets(const MMChunkEdit *edit, MMU32 scene)
{
    static const MMU32 types[] = {BACKGROUND_TYPE, MM_MATERIAL_CHUNK};
    const MMChunkAccess access = mm_chunk_edit_access(edit), *chunks = &access;
    SceneAssetGraph source = {NULL, 0};
    MMChunkGraph graph = {NULL, scene_delete_children, scene_delete_child};
    MMCollectionStorage scratch;
    MMChunkWalk walk;
    MMChunkEntry entry;
    MMChunkChild node;
    MMU32 row, flags, bytes, first[2], count = 0, i, *removed;
    int status, skip = 0, ok = 0;
    if (!chunks->entry || !chunks->child || !edit->store)
        return 0;
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
    if (chunks->count > (MM_STORAGE_LIMIT - 1) / sizeof(*removed) ||
        mm_chunk_walk_init(&walk, edit->walk_allocator) != MM_CHUNK_OK)
        return 0;
    if (mm_storage_init(&scratch, edit->walk_allocator) != MM_STORAGE_OK)
    {
        mm_chunk_walk_dispose(&walk);
        return 0;
    }
    bytes = chunks->count * (MMU32)sizeof(*removed);
    if (mm_storage_resize_exact(&scratch, 0, bytes, MM_STORAGE_NORMAL) != MM_STORAGE_OK ||
        mm_chunk_walk_reset(&walk, &graph, SCENE_TYPE, scene) != MM_CHUNK_OK)
        goto done;
    removed = (MMU32 *)scratch.buffers[0].data;
    memset(removed, 0, bytes);
    /* Preview the same last-reference rule as mm_chunk_unlink. Follow
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
                removed[count++] = row;
        }
    }
    for (i = 0; i < count; ++i)
    {
        row = removed[i];
        if (mm_chunk_at(chunks, row, &entry) != MM_CHUNK_OK)
            goto done;
        entry.flags |= MM_CHUNK_LONER;
        edit->store(edit->context, row, &entry, 0);
    }
    ok = 1;
done:
    mm_storage_dispose(&scratch);
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
    chunks = mm_chunk_edit_access(&editor->edit);
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
        chunks = mm_chunk_edit_access(&editor->edit);
        for (scene = 0;; ++scene)
        {
            status = mm_chunk_type_at(&chunks, SCENE_TYPE, scene, &entry, &position);
            if (status == MM_CHUNK_NOT_FOUND)
                break;
            if (status != MM_CHUNK_OK ||
                !adopt_chunk(editor, MOVIE_TYPE, root.number, SCENE_TYPE, entry.number, scene))
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

int mm_movie_archive_append_scene(const MMMovieArchiveEditor *editor, MMU32 root, MMU32 scene,
                                  MMI32 source, MMU32 background)
{
    MMChunkAccess chunks;
    MMSceneState state;
    MMU32 slot, number, position, first, end;
    int status;
    chunks = mm_chunk_edit_access(&editor->edit);
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
        !mm_scene_archive_create(editor, &state, &number))
        return MM_MOVIE_SCENE_ARCHIVE_CREATE;
    if (number != scene && mm_chunk_move(&editor->edit, SCENE_TYPE, number, SCENE_TYPE, scene) != MM_CHUNK_OK)
        return MM_MOVIE_SCENE_ARCHIVE_CREATE;
    if (!mm_scene_archive_opening_background(editor, scene, state.firstFrame, source, background))
        return MM_MOVIE_SCENE_ARCHIVE_LINK;
    if (!adopt_chunk(editor, MOVIE_TYPE, root, SCENE_TYPE, scene, slot))
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
    chunks = mm_chunk_edit_access(&editor->edit);
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
           adopt_chunk(editor, MOVIE_TYPE, root.number, ROLLCALL_TYPE, number, 0);
}
