#include "sound_catalog.h"
#include "archive_file.h"
#include "mm/movie_title.h"
#include "mm/sound_resource.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <string.h>

typedef struct CatalogName
{
    const MMPrivateArchiveProfile *profile;
    const MMTextHost *text;
    char *output;
    size_t available;
} CatalogName;

static MMI32 convert_name(void *p, const void *input, MMU32 bytes, int encoding, void *output, MMU32 capacity)
{
    const MMPrivateArchiveProfile *profile = p;
    return (MMI32)profile->convert(NULL, input, bytes, encoding, output, capacity);
}

static int write_name(void *p, const char *bytes, size_t count)
{
    CatalogName *name = p;
    /* Keep the same byte limit used by the sound asset C API. */
    if (count > name->available)
        count = name->available;
    memcpy(name->output, bytes, count);
    name->output += count;
    name->available -= count;
    *name->output = '\0';
    return 1;
}

static int read_name(void *p, const MMReadStream *stream)
{
    CatalogName *name = p;
    const MMPrivateArchiveProfile *profile = name->profile;
    unsigned char wire[MM_CHUNK_NAME_WIRE_BYTES];
    unsigned short utf16[MM_STRING_MAX];
    MMMovieTitle title;
    MMChunkTitleIO io = {0};
    size_t units;
    if (stream->length > sizeof(wire) || !mm_read_exact(stream, 0, wire, stream->length))
        return MM_MOVIE_BAD_FILE;
    io.context = (void *)profile;
    io.encoding = profile->encoding;
    io.swapped_unicode_encoding = profile->swapped_unicode_encoding;
    io.convert = convert_name;
    mm_movie_title_init(&title, profile->unit);
    mm_movie_title_read_wire(&title, wire, stream->length, &io);
    units = mm_text_utf16(name->text, mm_movie_title_text(&title), title.count * title.unit,
                         io.encoding, utf16, sizeof(utf16) / sizeof(*utf16));
    return mm_text_encode_utf8(utf16, units, name, write_name) == 1 ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

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
    CatalogName name;
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
    name.profile = profile;
    name.text = text;
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
        name.output = asset->name;
        name.available = sizeof(asset->name) - 1;
        asset->style = style;
        asset->chunk = entry.number;
        if (mm_archive_reader_name(reader, i, read_name, &name) != MM_MOVIE_OK || !asset->name[0])
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
    MarcusArchiveInput archive = {0};
    MMArchiveReaderOptions options = {0};
    int status;
    options.max_packed_bytes = 0x7fffffffUL;
    options.max_string_bytes = 64UL * 1024UL * 1024UL;
    options.encoding = profile->encoding;
    options.convert = profile->convert;
    options.legacy_name = profile->legacy_name;
    status = marcus_archive_input_open(&archive, path, &options, profile->expanded);
    if (status != MM_ARCHIVE_READ_OK)
        return status == MM_ARCHIVE_MEMORY ? MARCUS_SOUND_CATALOG_MEMORY : MARCUS_SOUND_CATALOG_OPEN;
    status = read_assets(&next, archive.reader, profile, text);
    marcus_archive_input_close(&archive);
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
