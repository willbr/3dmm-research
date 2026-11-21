#include "browser_poster.h"
#include "mm/archive_reader.h"
#include "mm/movie_title.h"
#include "mm/scene_thumbnail.h"
#include <SDL3/SDL.h>
#include <string.h>

typedef struct PosterArchive
{
    SDL_IOStream *file;
    MMArchiveReader *reader;
} PosterArchive;

static int read_file(void *p, MMU32 offset, void *bytes, size_t count)
{
    SDL_IOStream *file = p;
    return SDL_SeekIO(file, offset, SDL_IO_SEEK_SET) == offset && SDL_ReadIO(file, bytes, count) == count;
}

static void close_archive(PosterArchive *archive)
{
    mm_archive_reader_close(archive->reader);
    if (archive->file)
        SDL_CloseIO(archive->file);
}

static int text_encoding(int big_endian)
{
#ifdef UNICODE
    return big_endian ? MM_TEXT_UTF16_BE : MM_TEXT_UTF16_LE;
#else
    (void)big_endian;
    return MM_TEXT_WINDOWS;
#endif
}

static int open_archive(PosterArchive *archive, const char *path)
{
    const unsigned short order = 1;
    MMArchiveReaderOptions options = {0};
    MMChunkPolicy policy = {0};
    MMReadStream stream;
    Sint64 size;
    if (!path || !*path || !(archive->file = SDL_IOFromFile(path, "rb")))
        return MM_ARCHIVE_INVALID_PAYLOAD;
    size = SDL_GetIOSize(archive->file);
    if (size < 0 || size > 0x7fffffffL)
        return MM_ARCHIVE_INVALID_PAYLOAD;
    stream.context = archive->file;
    stream.length = (MMU32)size;
    stream.read_at = read_file;
    policy.native_big_endian = *(const unsigned char *)&order == 0;
    policy.max_name_bytes = MM_CHUNK_NAME_WIRE_BYTES;
#ifdef CHUNK_BIG_INDEX
    policy.expanded_runtime = 1;
#endif
    options.max_packed_bytes = 16UL * 1024UL * 1024UL;
    options.max_string_bytes = 1024UL * 1024UL;
    options.encoding = text_encoding(policy.native_big_endian);
    return mm_archive_reader_open_stream(&stream, &policy, &options, &archive->reader);
}

static MMI32 convert_title(void *p, const void *input, MMU32 bytes, int encoding, void *output, MMU32 capacity)
{
#ifdef UNICODE
    return (MMI32)mm_text_utf16(p, input, bytes, encoding, output, capacity);
#else
    return (MMI32)mm_text_ansi(p, input, bytes, encoding, output, capacity);
#endif
}

typedef struct PosterTitle
{
    const MMTextHost *text;
    char *out;
    size_t remaining;
} PosterTitle;

static int append_title(void *p, const char *bytes, size_t count)
{
    PosterTitle *title = p;
    if (count >= title->remaining)
        return 0;
    memcpy(title->out, bytes, count);
    title->out += count;
    title->remaining -= count;
    *title->out = '\0';
    return 1;
}

static int read_title(void *p, const MMReadStream *stream)
{
    const unsigned short order = 1;
    const int big_endian = *(const unsigned char *)&order == 0;
    const int encoding = text_encoding(big_endian);
    PosterTitle *title = p;
    unsigned char wire[MM_CHUNK_NAME_WIRE_BYTES];
    unsigned short utf16[MM_STRING_MAX + 1];
    MMMovieTitle decoded;
    MMChunkTitleIO io = {0};
    size_t count;
    if (stream->length > sizeof(wire) || !mm_read_exact(stream, 0, wire, stream->length))
        return MM_MOVIE_BAD_FILE;
    io.context = (void *)title->text;
    io.encoding = encoding;
    io.swapped_unicode_encoding = big_endian ? MM_TEXT_UTF16_LE : MM_TEXT_UTF16_BE;
    io.convert = convert_title;
    mm_movie_title_init(&decoded, encoding == MM_TEXT_WINDOWS ? 1 : 2);
    mm_movie_title_read_wire(&decoded, wire, stream->length, &io);
    count = mm_text_utf16(title->text, mm_movie_title_text(&decoded), decoded.count * decoded.unit,
                          encoding, utf16, sizeof(utf16) / sizeof(*utf16));
    return mm_text_encode_utf8(utf16, count, title, append_title) == 1 ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

static int earlier_child(const MMChunkChild *a, const MMChunkChild *b)
{
    return a->slot < b->slot || (a->slot == b->slot && a->number < b->number);
}

static int find_thumbnail(const MMChunkAccess *chunks, const MMChunkEntry *scene, MMU32 position, MMU32 *number)
{
    MMChunkChild child, first = {0};
    MMU32 i, thumbnail;
    int found = 0;
    for (i = 0; i < scene->child_count; ++i)
        if (mm_chunk_child_at(chunks, position, i, &child) == MM_CHUNK_OK &&
            child.type == MM_SCENE_THUMBNAIL_TYPE &&
            (!found || earlier_child(&child, &first)) &&
            mm_chunk_find(chunks, child.type, child.number, &thumbnail) == MM_CHUNK_OK)
        {
            first = child;
            found = 1;
        }
    if (found)
        *number = first.number;
    return found;
}

void marcus_browser_poster(const char *movie, const char *resources, const MMTextHost *text,
                          MarcusBrowserPoster *poster, void *image_context,
                          unsigned char *(*image)(void *, MMU32, MMU32))
{
    enum { frames_per_second = 6 };
    PosterArchive document = {0}, colours = {0};
    MMChunkAccess chunks;
    MMChunkEntry root, scene;
    MMChunkChild child, first_scene = {0};
    MMArchiveRecord record;
    MMSceneThumbnailPreview preview = {0};
    PosterTitle title;
    MMU32 position, scene_position, i, scenes = 0, thumbnail = 0;
    Sint64 frames = 0, seconds;
    char duration[48];
    int has_thumbnail = 0, status;
    memset(poster, 0, sizeof(*poster));
    status = open_archive(&document, movie);
    if (status != MM_ARCHIVE_READ_OK)
    {
        poster->error = !movie || !*movie ? "Enter a file path." :
                        status == MM_ARCHIVE_MEMORY ? "Insufficient memory to open archive." :
                        "Cannot open this file as a chunky archive.";
        goto done;
    }
    chunks = mm_archive_reader_access(document.reader);
    if (mm_chunk_type_at(&chunks, MM_MOVIE_CHUNK, 0, &root, &position) == MM_CHUNK_OK)
    {
        title.text = text;
        title.out = poster->title;
        title.remaining = sizeof(poster->title);
        if (mm_archive_reader_name(document.reader, position, read_title, &title) != MM_MOVIE_OK)
            poster->title[0] = '\0';
        for (i = 0; i < root.child_count; ++i)
        {
            if (mm_chunk_child_at(&chunks, position, i, &child) != MM_CHUNK_OK ||
                child.type != MM_SCENE_CHUNK ||
                mm_chunk_find(&chunks, child.type, child.number, &scene_position) != MM_CHUNK_OK ||
                mm_chunk_at(&chunks, scene_position, &scene) != MM_CHUNK_OK)
                continue;
            ++scenes;
            if (mm_archive_reader_record(document.reader, scene_position, &record) == MM_ARCHIVE_READ_OK &&
                record.kind == MM_ARCHIVE_SCENE && record.value.scene.first <= record.value.scene.last)
                frames += (Sint64)record.value.scene.last - record.value.scene.first + 1;
            /* Some archives store links out of order. The browser uses slot order. */
            if ((!has_thumbnail || earlier_child(&child, &first_scene)) &&
                find_thumbnail(&chunks, &scene, scene_position, &thumbnail))
            {
                first_scene = child;
                has_thumbnail = 1;
            }
        }
    }
    seconds = (frames + frames_per_second / 2) / frames_per_second;
    if (seconds >= 3600)
        SDL_snprintf(duration, sizeof(duration), "%" SDL_PRIs64 ":%02d:%02d", seconds / 3600,
                     (int)(seconds / 60 % 60), (int)(seconds % 60));
    else
        SDL_snprintf(duration, sizeof(duration), "%d:%02d", (int)(seconds / 60), (int)(seconds % 60));
    SDL_snprintf(poster->details, sizeof(poster->details),
                 "%lu scenes / %" SDL_PRIs64 " scene frames\nEst. %s (frames only)",
                 (unsigned long)scenes, frames, duration);
    if (!scenes)
        poster->error = "No movie scenes found.";
    else if (has_thumbnail && open_archive(&colours, resources) == MM_ARCHIVE_READ_OK)
    {
        preview.archive = document.reader;
        preview.resources = colours.reader;
        preview.context = image_context;
        preview.image = image;
        mm_scene_thumbnail_poster(thumbnail, &preview);
    }
done:
    close_archive(&colours);
    close_archive(&document);
}
