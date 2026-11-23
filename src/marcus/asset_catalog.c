#include "asset_catalog.h"
#include "archive_file.h"
#include "mm/background_resource.h"
#include "mm/font_resource.h"
#include "mm/material_resource.h"
#include "mm/template_create.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <string.h>

typedef struct AssetKind
{
    const char *file, *label;
    MMU32 type;
} AssetKind;

static const AssetKind kinds[MARCUS_ASSET_KIND_COUNT] = {
    {"tmpls.3cn", "Prop", MM_TMPL},
    {"tmpls.3cn", "Actor", MM_TMPL},
    {"tdfs.3cn", "3D font", MM_FONT_TYPE},
    {"mtrls.3cn", "Material", MM_MATERIAL_CHUNK},
    {"bkgds.3cn", "Background", MM_BACKGROUND}
};

const char *marcus_asset_catalog_file(MarcusAssetKind kind)
{
    return kinds[kind].file;
}

static int compare_assets(const void *left, const void *right)
{
    const MarcusProp *a = left, *b = right;
    const int order = strcmp(a->name, b->name);
    if (order || a->chunk == b->chunk)
        return order;
    return a->chunk < b->chunk ? -1 : 1;
}

static int match_template(MMArchiveReader *reader, MMU32 position, int prop, int *matches)
{
    const unsigned short one = 1;
    const int big = *(const unsigned char *)&one == 0;
    unsigned char bytes[MM_TEMPLATE_HEADER_BYTES];
    MMTemplateState tmpl;
    MMReadStream stream;
    const int status = mm_archive_reader_payload(reader, position, &stream);
    if (status != MM_ARCHIVE_READ_OK)
        return status;
    *matches = 0;
    if (stream.length < sizeof(bytes))
        return MM_ARCHIVE_READ_OK;
    if (!mm_read_exact(&stream, 0, bytes, sizeof(bytes)))
        return MM_ARCHIVE_MISSING_PAYLOAD;
    if (mm_template_decode_header(bytes, big, &tmpl))
        *matches = !(tmpl.flags & MM_TEMPLATE_TEXT) && ((tmpl.flags & MM_TEMPLATE_PROP) != 0) == prop;
    return MM_ARCHIVE_READ_OK;
}

static int read_assets(MarcusAssetCatalog *catalog, MarcusAssetKind kind, MMArchiveReader *reader,
                       const MMPrivateArchiveProfile *profile, const MMTextHost *text)
{
    const AssetKind *info = kinds + kind;
    const MMChunkAccess chunks = mm_archive_reader_access(reader);
    MMU32 first, end, i;
    size_t capacity;
    if (mm_chunk_find(&chunks, info->type, 0, &first) > MM_CHUNK_NOT_FOUND ||
        mm_chunk_find(&chunks, info->type + 1, 0, &end) > MM_CHUNK_NOT_FOUND)
        return MM_ARCHIVE_INVALID_PAYLOAD;
    capacity = end - first;
    if (!capacity)
        return MM_ARCHIVE_READ_OK;
    if (capacity > INT_MAX || sizeof(*catalog->rows) > (size_t)-1 / capacity ||
        !(catalog->rows = SDL_calloc(capacity, sizeof(*catalog->rows))))
        return MM_ARCHIVE_MEMORY;
    for (i = first; i < end; ++i)
    {
        MMChunkEntry entry;
        MarcusProp *asset;
        if (mm_chunk_at(&chunks, i, &entry) != MM_CHUNK_OK)
            return MM_ARCHIVE_INVALID_PAYLOAD;
        if (info->type == MM_TMPL)
        {
            int matches;
            const int status = match_template(reader, i, kind == MARCUS_ASSET_PROP, &matches);
            if (status != MM_ARCHIVE_READ_OK)
                return status;
            if (!matches)
                continue;
        }
        asset = catalog->rows + catalog->count++;
        asset->chunk = entry.number;
        if (!marcus_archive_name_utf8(reader, i, profile, text, asset->name, sizeof(asset->name), 1))
            SDL_snprintf(asset->name, sizeof(asset->name), "%s %lu", info->label, asset->chunk);
    }
    if (kind != MARCUS_ASSET_BACKGROUND && catalog->count > 1)
        SDL_qsort(catalog->rows, catalog->count, sizeof(*catalog->rows), compare_assets);
    return MM_ARCHIVE_READ_OK;
}

int marcus_asset_catalog_load(MarcusAssetCatalog *catalog, MarcusAssetKind kind, const char *directory, const char *path,
                              const MMPrivateArchiveProfile *profile, const MMTextHost *text)
{
    MarcusAssetCatalog next = {0};
    MMNativeArchiveInput input = {0};
    int status = marcus_archive_input_open_profile(&input, path, profile);
    if (status != MM_ARCHIVE_READ_OK)
        return status;
    status = read_assets(&next, kind, input.reader, profile, text);
    mm_native_archive_input_close(&input);
    if (status == MM_ARCHIVE_READ_OK && !(next.directory = SDL_strdup(directory)))
        status = MM_ARCHIVE_MEMORY;
    if (status == MM_ARCHIVE_READ_OK)
    {
        marcus_asset_catalog_clear(catalog);
        *catalog = next;
    }
    else
        marcus_asset_catalog_clear(&next);
    return status;
}

void marcus_asset_catalog_clear(MarcusAssetCatalog *catalog)
{
    SDL_free(catalog->rows);
    SDL_free(catalog->directory);
    memset(catalog, 0, sizeof(*catalog));
}

int marcus_asset_catalog_matches(const MarcusAssetCatalog *catalog, const char *directory)
{
    return catalog->count && strcmp(catalog->directory, directory) == 0;
}

const MarcusProp *marcus_asset_catalog_row(const MarcusAssetCatalog *catalog, int index)
{
    return index >= 0 && index < catalog->count ? catalog->rows + index : NULL;
}

const MarcusProp *marcus_asset_catalog_find(const MarcusAssetCatalog *catalog, unsigned long chunk)
{
    int i;
    for (i = 0; i < catalog->count; ++i)
        if (catalog->rows[i].chunk == chunk)
            return catalog->rows + i;
    return NULL;
}
