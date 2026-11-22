#include "storyboard_store.h"
#include "mm/background_resource.h"
#include "mm/reference.h"
#include "mm/scene_data.h"
#include <stdlib.h>
#include <string.h>

struct MarcusBoardData
{
    MMReference refs;
    const MMMovieEditFileServices *files;
    void *file;
    MarcusBoardWire wire;
};

static void release_data(void *value)
{
    MarcusBoardData *data = (MarcusBoardData *)value;
    data->files->release(data->file);
    free(data);
}

int marcus_board_store_init(MarcusBoardStore *store, const MMStorageAllocator *allocator,
                            const MMMovieEditFileServices *files)
{
    store->count = 0;
    store->files = files;
    return mm_storage_init(&store->storage, allocator) == MM_STORAGE_OK;
}

const MarcusStoredBoard *marcus_board_store_data(const MarcusBoardStore *store)
{
    return (const MarcusStoredBoard *)store->storage.buffers[0].data;
}

void marcus_board_store_dispose(MarcusBoardStore *store)
{
    const MarcusStoredBoard *boards = marcus_board_store_data(store);
    MMU32 i;
    for (i = 0; i < store->count; ++i)
        mm_reference_release(&boards[i].data->refs, boards[i].data, release_data);
    mm_storage_dispose(&store->storage);
    store->count = 0;
}

int marcus_board_store_copy(MarcusBoardStore *destination, const MarcusBoardStore *source)
{
    MarcusBoardStore copy, old;
    const MarcusStoredBoard *boards;
    MMU32 i;
    if (destination == source)
        return 1;
    if (destination->files != source->files ||
        !marcus_board_store_init(&copy, &destination->storage.allocator, destination->files))
        return 0;
    if (mm_storage_copy(&copy.storage, &source->storage, source->count * sizeof(*boards), 0) != MM_STORAGE_OK)
    {
        marcus_board_store_dispose(&copy);
        return 0;
    }
    boards = marcus_board_store_data(&copy);
    for (i = 0; i < source->count; ++i)
        mm_reference_add(&boards[i].data->refs);
    copy.count = source->count;
    old = *destination;
    *destination = copy;
    marcus_board_store_dispose(&old);
    return 1;
}

static MMU32 board_lower(const MarcusBoardStore *store, MMU32 scene)
{
    const MarcusStoredBoard *boards = marcus_board_store_data(store);
    MMU32 first = 0, last = store->count, middle;
    while (first < last)
    {
        middle = first + (last - first) / 2;
        if (boards[middle].scene < scene)
            first = middle + 1;
        else
            last = middle;
    }
    return first;
}

const MarcusStoredBoard *marcus_board_store_find(const MarcusBoardStore *store, MMU32 scene)
{
    const MarcusStoredBoard *boards = marcus_board_store_data(store);
    MMU32 i = board_lower(store, scene);
    return i < store->count && boards[i].scene == scene ? boards + i : NULL;
}

const MarcusBoardWire *marcus_stored_board_view(const MarcusStoredBoard *board)
{
    return &board->data->wire;
}

void *marcus_stored_board_file(const MarcusStoredBoard *board)
{
    return board->data->file;
}

int marcus_board_store_set(MarcusBoardStore *store, MMU32 scene, MMI32 source, MMU32 background, void *file,
                           const MarcusBoardWire *wire)
{
    MMArray rows = {0};
    MarcusStoredBoard value, *boards;
    MarcusBoardData *data, *old;
    unsigned char *pixels;
    MMU32 pos;
    int i;
    if (!store->files || !file || !marcus_board_wire_size(wire))
        return 0;
    data = (MarcusBoardData *)malloc(sizeof(*data) + (size_t)wire->values.angle_count * MARCUS_BOARD_PIXELS);
    if (!data)
        return 0;
    mm_reference_init(&data->refs);
    data->files = store->files;
    data->file = file;
    data->wire = *wire;
    pixels = (unsigned char *)(data + 1);
    for (i = 0; i < wire->values.angle_count; ++i, pixels += MARCUS_BOARD_PIXELS)
    {
        memcpy(pixels, wire->pixels[i], MARCUS_BOARD_PIXELS);
        data->wire.pixels[i] = pixels;
    }
    for (; i < MARCUS_BOARD_ANGLE_LIMIT; ++i)
        data->wire.pixels[i] = NULL;
    value.scene = scene;
    value.source = source;
    value.background = background;
    value.data = data;
    pos = board_lower(store, scene);
    boards = (MarcusStoredBoard *)store->storage.buffers[0].data;
    store->files->retain(file);
    if (pos < store->count && boards[pos].scene == scene)
    {
        old = boards[pos].data;
        boards[pos] = value;
        mm_reference_release(&old->refs, old, release_data);
        return 1;
    }
    rows.storage = &store->storage;
    rows.width = sizeof(value);
    rows.count = store->count;
    rows.free_count = -1;
    if (mm_array_insert(&rows, pos, &value, 1) != MM_STORAGE_OK)
    {
        mm_reference_release(&data->refs, data, release_data);
        return 0;
    }
    store->count = rows.count;
    return 1;
}

static int visit_payload(MMArchiveReader *reader, MMU32 position, MMU32 scene,
                         MarcusBoardVisit *visit, void *context)
{
    MMReadStream stream;
    MarcusBoardWire board;
    unsigned char *bytes;
    int status;
    if (mm_archive_reader_payload(reader, position, &stream) != MM_ARCHIVE_READ_OK || stream.length > 12UL * 1024 * 1024)
        return MARCUS_BOARD_READ_PAYLOAD;
    bytes = (unsigned char *)malloc(stream.length ? stream.length : 1);
    if (!bytes)
        return MARCUS_BOARD_READ_PAYLOAD;
    status = mm_read_exact(&stream, 0, bytes, stream.length) ? marcus_board_decode(bytes, stream.length, &board) :
                                                            MARCUS_BOARD_READ_PAYLOAD;
    if (status == MARCUS_BOARD_WIRE_OK && !visit(context, scene, &board))
        status = MARCUS_BOARD_READ_STOPPED;
    free(bytes);
    return status;
}

int marcus_board_read_archive(MMArchiveReader *reader, MarcusBoardVisit *visit, void *context)
{
    const MMChunkAccess chunks = mm_archive_reader_access(reader);
    MMChunkEntry scene;
    MMChunkChild child, metadata, background;
    MMU32 pos, i, graphic, payload;
    int has_metadata, has_background, status;
    status = mm_chunk_find(&chunks, MM_SCENE_CHUNK, 0, &pos);
    if (status != MM_CHUNK_OK && status != MM_CHUNK_NOT_FOUND)
        return MARCUS_BOARD_READ_LINKS;
    for (; pos < chunks.count; ++pos)
    {
        if (mm_chunk_at(&chunks, pos, &scene) != MM_CHUNK_OK)
            return MARCUS_BOARD_READ_LINKS;
        if (scene.type != MM_SCENE_CHUNK)
            break;
        has_metadata = has_background = 0;
        for (i = 0; i < scene.child_count; ++i)
        {
            if (mm_chunk_child_at(&chunks, pos, i, &child) != MM_CHUNK_OK)
                return MARCUS_BOARD_READ_LINKS;
            if (child.slot)
                continue;
            /* Preserve the archive reader's last-link selection for a repeated slot. */
            if (child.type == MM_STORYBOARD_CHUNK)
            {
                metadata = child;
                has_metadata = 1;
            }
            else if (child.type == MM_BACKGROUND)
            {
                background = child;
                has_background = 1;
            }
        }
        if (!has_metadata)
            continue;
        if (!has_background || mm_chunk_find(&chunks, MM_BACKGROUND, background.number, &graphic) != MM_CHUNK_OK ||
            mm_chunk_find(&chunks, MM_STORYBOARD_CHUNK, metadata.number, &payload) != MM_CHUNK_OK)
            return MARCUS_BOARD_READ_LINKS;
        status = visit_payload(reader, payload, scene.number, visit, context);
        if (status != MARCUS_BOARD_WIRE_OK)
            return status;
    }
    return MARCUS_BOARD_WIRE_OK;
}

const char *marcus_board_embed(MMPrivateArchive *target, MMPrivateArchive *source, const MarcusStoredBoard *board)
{
    const MarcusBoardWire *wire = marcus_stored_board_view(board);
    MMMovieArchiveEditor editor;
    MMU32 background;
    size_t size;
    unsigned char *bytes;
    int ok;
    if (!mm_private_archive_copy(source, target, MM_BACKGROUND, board->background, 0, &background))
        return "Cannot embed storyboard resources.";
    size = marcus_board_wire_size(wire);
    bytes = size ? (unsigned char *)malloc(size) : NULL;
    if (!bytes)
        return "Cannot embed storyboard data.";
    editor = mm_private_archive_editor(target);
    ok = marcus_board_encode(wire, bytes, size) &&
         mm_movie_archive_embed_storyboard(&editor, board->scene, background, bytes, (MMU32)size);
    free(bytes);
    return ok ? NULL : "Cannot embed storyboard data.";
}
