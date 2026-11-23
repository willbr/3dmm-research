#include "sound_catalog.h"
#include "archive_file.h"
#include "mm/sound_resource.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <string.h>

static int compare_assets(const void *left, const void *right)
{
    const MarcusSoundAsset *a = left, *b = right;
    int order;
    if (a->style != b->style)
        return a->style < b->style ? -1 : 1;
    order = SDL_strcasecmp(a->name, b->name);
    if (order || a->chunk == b->chunk)
        return order;
    /* Equal names retain the original index order (ascending chunk number). */
    return a->chunk < b->chunk ? -1 : 1;
}

static int read_assets(MarcusSoundCatalog *out, MMArchiveReader *reader,
                       const MMPrivateArchiveProfile *profile, const MMTextHost *text)
{
    const unsigned short one = 1;
    const int big_endian = *(const unsigned char *)&one == 0;
    const MMChunkAccess chunks = mm_archive_reader_access(reader);
    const MMMovieSessionGraph graph = mm_archive_reader_graph(reader);
    MMU32 first, end, i;
    size_t capacity;
    if (mm_chunk_find(&chunks, MM_SOUND_TYPE, 0, &first) > MM_CHUNK_NOT_FOUND ||
        mm_chunk_find(&chunks, MM_SOUND_TYPE + 1, 0, &end) > MM_CHUNK_NOT_FOUND)
        return MARCUS_SOUND_CATALOG_OPEN;
    capacity = end - first;
    if (!capacity)
        return MARCUS_SOUND_CATALOG_OK;
    if (capacity > INT_MAX || sizeof(*out->rows) > (size_t)-1 / capacity ||
        !(out->rows = SDL_calloc(capacity, sizeof(*out->rows))))
        return MARCUS_SOUND_CATALOG_MEMORY;
    for (i = first; i < end; ++i)
    {
        MMChunkEntry entry;
        MMI32 style;
        MarcusSoundAsset *asset;
        if (mm_chunk_at(&chunks, i, &entry) != MM_CHUNK_OK)
            return MARCUS_SOUND_CATALOG_OPEN;
        if (!mm_sound_catalog_style(&graph, entry.number, big_endian, &style))
            continue;
        asset = out->rows + out->count;
        asset->style = style;
        asset->chunk = entry.number;
        if (!marcus_archive_name_utf8(reader, i, profile, text, asset->name, sizeof(asset->name), 0) || !asset->name[0])
        {
            memset(asset->name, 0, sizeof(asset->name));
            SDL_snprintf(asset->name, sizeof(asset->name), "Sound %lu", asset->chunk);
        }
        ++out->count;
    }
    SDL_qsort(out->rows, out->count, sizeof(*out->rows), compare_assets);
    return MARCUS_SOUND_CATALOG_OK;
}

void marcus_sound_catalog_clear(MarcusSoundCatalog *catalog)
{
    SDL_free(catalog->rows);
    SDL_free(catalog->directory);
    catalog->rows = NULL;
    catalog->directory = NULL;
    catalog->count = 0;
}

int marcus_sound_catalog_matches(const MarcusSoundCatalog *catalog, const char *directory)
{
    return catalog->count && strcmp(catalog->directory, directory) == 0;
}

const MarcusSoundAsset *marcus_sound_catalog_asset(const MarcusSoundCatalog *catalog, int index)
{
    return index >= 0 && index < catalog->count ? catalog->rows + index : NULL;
}

int marcus_sound_catalog_load(MarcusSoundCatalog *catalog, const char *directory, const char *path,
                              const MMPrivateArchiveProfile *profile, const MMTextHost *text)
{
    MarcusSoundCatalog next = {0};
    MMNativeArchiveInput archive = {0};
    int status;
    status = marcus_archive_input_open_profile(&archive, path, profile);
    if (status != MM_ARCHIVE_READ_OK)
        return status == MM_ARCHIVE_MEMORY ? MARCUS_SOUND_CATALOG_MEMORY : MARCUS_SOUND_CATALOG_OPEN;
    status = read_assets(&next, archive.reader, profile, text);
    mm_native_archive_input_close(&archive);
    if (status == MARCUS_SOUND_CATALOG_OK && !(next.directory = SDL_strdup(directory)))
        status = MARCUS_SOUND_CATALOG_MEMORY;
    if (status == MARCUS_SOUND_CATALOG_OK)
    {
        marcus_sound_catalog_clear(catalog);
        *catalog = next;
    }
    else
        marcus_sound_catalog_clear(&next);
    return status;
}
