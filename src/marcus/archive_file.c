#include "archive_file.h"
#include "player.h"
#include "storyboard_store.h"
#include "mm/actor_archive.h"
#include "mm/collections.h"
#include "mm/movie_scene_journal.h"
#include "mm/movie_title.h"
#include "mm/scene_data.h"
#include "mm/string_ops.h"
#include "mm/text.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

int marcus_archive_stage_working_file(int kind, const char *stamp, int copy_source, const MarcusWorkingFiles *files)
{
    const char *prefix = "marcus-edit-", *leaf = "movie.3mm";
    char directory[64];
    int attempt, length;
    MMMovieStageResult reserved;

    if (kind == MARCUS_WORKING_BOARD)
    {
        prefix = "marcus-board-";
        leaf = "background.3cn";
    }
    else if (kind == MARCUS_WORKING_TIMING)
    {
        prefix = "marcus-timing-";
        leaf = "timing.bin";
    }

    for (attempt = 0; attempt < 100; ++attempt)
    {
        length = SDL_snprintf(directory, sizeof(directory), "%s%s-%d", prefix, stamp, attempt);
        if (length < 0 || (size_t)length >= sizeof(directory)) return 0;
        reserved = files->reserve(files->context, directory, leaf);
        if (reserved == MM_MOVIE_STAGE_OK)
            return !copy_source || files->copy_writable(files->context);
        if (reserved == MM_MOVIE_STAGE_ERROR) return 0;
    }
    return 0;
}

typedef struct ArchiveFile
{
    SDL_IOStream *stream;
    size_t references;
} ArchiveFile;

static void retain(void *context, void *file)
{
    (void)context;
    ++((ArchiveFile *)file)->references;
}

static void release(void *p)
{
    ArchiveFile *file = p;
    if (--file->references == 0)
    {
        SDL_CloseIO(file->stream);
        SDL_free(file);
    }
}

static void release_block(void *context, void *file)
{
    (void)context;
    release(file);
}

static void *create_temp(void *context)
{
    (void)context;
    return NULL;
}

static int read_at(void *p, MMU32 offset, void *data, size_t bytes)
{
    SDL_IOStream *stream = ((ArchiveFile *)p)->stream;
    return !bytes || (SDL_SeekIO(stream, offset, SDL_IO_SEEK_SET) == offset &&
                      SDL_ReadIO(stream, data, bytes) == bytes);
}

static int write_at(void *p, MMU32 offset, const void *data, size_t bytes)
{
    SDL_IOStream *stream = ((ArchiveFile *)p)->stream;
    return !bytes || (SDL_SeekIO(stream, offset, SDL_IO_SEEK_SET) == offset &&
                      SDL_WriteIO(stream, data, bytes) == bytes &&
                      (offset != 0 || SDL_FlushIO(stream)));
}

static MMU32 size(void *p)
{
    const Sint64 bytes = SDL_GetIOSize(((ArchiveFile *)p)->stream);
    return bytes >= 0 && bytes <= 0x7fffffffL ? (MMU32)bytes : 0xffffffffUL;
}

static int extend(void *file, MMU32 bytes)
{
    const MMU32 end = size(file);
    const unsigned char zero = 0;
    return bytes == end || (bytes > end && write_at(file, bytes - 1, &zero, 1));
}

static MMReadStream read_stream(void *file)
{
    MMReadStream stream = {NULL, 0, read_at};
    const MMU32 bytes = size(file);
    stream.context = file;
    if (bytes <= 0x7fffffffUL)
        stream.length = bytes;
    return stream;
}

static int read_block(void *context, void *file, MMU32 offset, void *data, size_t bytes)
{
    (void)context;
    return read_at(file, offset, data, bytes);
}

static int write_block(void *context, void *file, MMU32 offset, const void *data, size_t bytes)
{
    (void)context;
    return write_at(file, offset, data, bytes);
}

static ArchiveFile *open_file(const char *path, const char *mode)
{
    ArchiveFile *file = SDL_malloc(sizeof(*file));
    if (!file)
        return NULL;
    file->stream = SDL_IOFromFile(path, mode);
    if (!file->stream)
    {
        SDL_free(file);
        return NULL;
    }
    file->references = 1;
    return file;
}

MMPrivateArchive *marcus_archive_open(const char *path, int mode, const MMPrivateArchiveProfile *profile)
{
    static const MMChunkOwnerFileIO owner_io = {NULL, create_temp, read_stream, size, extend, NULL,
                                                write_at, release, NULL, NULL, NULL, NULL, NULL};
    static const MMBlockFileServices blocks = {NULL, retain, release_block, create_temp, read_block, write_block};
    const char *open_mode = mode == MM_PRIVATE_ARCHIVE_READ ? "rb" : mode == MM_PRIVATE_ARCHIVE_EDIT ? "r+b" : "w+b";
    ArchiveFile *file = open_file(path, open_mode);
    return file ? mm_private_archive_open(file, mode, &owner_io, &blocks, profile) : NULL;
}

int marcus_archive_input_open(MarcusArchiveInput *input, const char *path, const MMArchiveReaderOptions *options,
                               int expanded)
{
    const unsigned short order = 1;
    MMChunkPolicy policy = {0};
    MarcusArchiveInput next = {0};
    MMReadStream stream;
    int status;
    if (!path || !*path || !(next.file = open_file(path, "rb")))
        return MM_ARCHIVE_INVALID_PAYLOAD;
    stream = read_stream(next.file);
    policy.expanded_runtime = expanded;
    policy.native_big_endian = *(const unsigned char *)&order == 0;
    policy.max_name_bytes = MM_CHUNK_NAME_WIRE_BYTES;
    status = mm_archive_reader_open_stream(&stream, &policy, options, &next.reader);
    if (status != MM_ARCHIVE_READ_OK)
    {
        marcus_archive_input_close(&next);
        return status;
    }
    marcus_archive_input_close(input);
    *input = next;
    return MM_ARCHIVE_READ_OK;
}

void marcus_archive_input_close(MarcusArchiveInput *input)
{
    mm_archive_reader_close(input->reader);
    if (input->file)
        release(input->file);
    input->file = NULL;
    input->reader = NULL;
}

static MMArchiveReaderOptions archive_options(const MMPrivateArchiveProfile *profile)
{
    MMArchiveReaderOptions options = {0};
    options.max_packed_bytes = 0x7fffffffUL;
    options.max_string_bytes = 64UL * 1024UL * 1024UL;
    options.encoding = profile->encoding;
    options.convert = profile->convert;
    options.legacy_name = profile->legacy_name;
    return options;
}

int marcus_archive_input_open_profile(MarcusArchiveInput *input, const char *path, const MMPrivateArchiveProfile *profile)
{
    const MMArchiveReaderOptions options = archive_options(profile);
    return marcus_archive_input_open(input, path, &options, profile->expanded);
}

typedef struct ArchiveName
{
    const MMTextHost *text;
    char *output;
    size_t available;
    int whole_characters;
} ArchiveName;

static MMI32 translate_archive_name(void *p, const void *input, MMU32 bytes, int encoding, void *output, MMU32 capacity)
{
    const MMPrivateArchiveProfile *profile = p;
    return (MMI32)profile->convert(NULL, input, bytes, encoding, output, capacity);
}

static int write_archive_name(void *p, const char *bytes, size_t count)
{
    ArchiveName *name = p;
    if (count > name->available)
    {
        if (name->whole_characters)
            name->available = 0;
        count = name->available;
    }
    memcpy(name->output, bytes, count);
    name->output += count;
    name->available -= count;
    *name->output = '\0';
    return 1;
}

static int encode_archive_text(ArchiveName *name, const void *text, size_t bytes, int encoding)
{
    unsigned short utf16[MM_STRING_MAX];
    const size_t units = mm_text_utf16(name->text, text, bytes, encoding, utf16, sizeof(utf16) / sizeof(*utf16));
    return mm_text_encode_utf8(utf16, units, name, write_archive_name);
}

size_t marcus_archive_name_utf8(MMArchiveReader *reader, MMU32 position, const MMPrivateArchiveProfile *profile,
                                const MMTextHost *text, char *output, size_t capacity, int whole_characters)
{
    ArchiveName name;
    MMMovieTitle title;
    MMChunkTitleIO codec = {0};
    if (!capacity)
        return 0;
    name.text = text;
    name.output = output;
    name.available = capacity - 1;
    name.whole_characters = whole_characters;
    codec.context = (void *)profile;
    codec.encoding = profile->encoding;
    codec.swapped_unicode_encoding = profile->swapped_unicode_encoding;
    codec.convert = translate_archive_name;
    *output = '\0';
    if (!mm_movie_title_init(&title, profile->unit) || !mm_archive_reader_title(reader, position, &title, &codec) ||
        encode_archive_text(&name, mm_movie_title_text(&title), title.count * title.unit, codec.encoding) != 1)
    {
        memset(output, 0, capacity);
        return 0;
    }
    return (size_t)(name.output - output);
}

int marcus_archive_text_utf8(const MMTextHost *host, const void *text, size_t bytes, int encoding,
                             char *output, size_t capacity, size_t *written)
{
    ArchiveName name;
    if (!capacity)
        return 0;
    name.text = host;
    name.output = output;
    name.available = capacity - 1;
    name.whole_characters = 1;
    *output = '\0';
    if (encode_archive_text(&name, text, bytes, encoding) != 1)
        return 0;
    *written = (size_t)(name.output - output);
    return 1;
}

int marcus_archive_source_utf8(const MMMovieSourceName *source, const MMPrivateArchiveProfile *profile,
                               const MMTextHost *text, char output[MM_MOVIE_SOURCE_NAME_BYTES], size_t *written)
{
    unsigned short native[MM_STRING_MAX];
    const MMU32 units = mm_movie_source_name_text(source, profile->encoding, profile->unit, profile->convert,
                                                  NULL, native, MM_STRING_MAX);
    return marcus_archive_text_utf8(text, native, units * profile->unit, profile->encoding,
                                   output, MM_MOVIE_SOURCE_NAME_BYTES, written);
}

const char *marcus_archive_directory_error(int status, int payload)
{
    switch (status)
    {
    case MM_ARCHIVE_DIRECTORY_ROOT: return "Invalid or unsupported record header.";
    case MM_ARCHIVE_DIRECTORY_SCENE: return "The movie has an invalid scene link or header.";
    case MM_ARCHIVE_DIRECTORY_SOURCE_LINK: return "The movie has an invalid source table link.";
    case MM_ARCHIVE_DIRECTORY_SOURCE_LIMIT: return "Source table exceeds this session's preview budget.";
    case MM_ARCHIVE_DIRECTORY_SOURCE_TABLE: return "Invalid movie source table.";
    case MM_ARCHIVE_DIRECTORY_SOURCE_ROW: return "Cannot read a movie source name.";
    case MM_ARCHIVE_DIRECTORY_MEMORY: return "Insufficient memory to read the movie directory.";
    case MM_ARCHIVE_DIRECTORY_INDEX: return "Cannot read the movie chunk index.";
    case MM_ARCHIVE_DIRECTORY_NO_SCENES: return "This document contains no movie scenes.";
    case MM_ARCHIVE_DIRECTORY_ROLLCALL: return "The movie roll-call record is invalid.";
    case MM_ARCHIVE_DIRECTORY_ROLLCALL_LIMIT: return "The movie roll-call table has too many entries.";
    case MM_ARCHIVE_DIRECTORY_PAYLOAD:
        switch (payload)
        {
        case MM_ARCHIVE_INVALID_POSITION: return "Chunk index is out of range.";
        case MM_ARCHIVE_MISSING_PAYLOAD: return "Cannot read this chunk.";
        case MM_ARCHIVE_BAD_PACKED_HEADER: return "Invalid compressed payload header.";
        case MM_ARCHIVE_PACKED_LIMIT: return "Packed payload exceeds this session's decode budget.";
        case MM_ARCHIVE_DECODE_FAILED: return "Cannot decode this payload.";
        case MM_ARCHIVE_INVALID_RECORD: return "Invalid or unsupported record header.";
        case MM_ARCHIVE_WRONG_TYPE: return "This chunk is not a General String Table.";
        case MM_ARCHIVE_STRING_LIMIT: return "String table exceeds this session's preview budget.";
        case MM_ARCHIVE_INVALID_STRINGS: return "Invalid or unsupported General String Table.";
        case MM_ARCHIVE_MEMORY: return "Insufficient memory to read the movie directory.";
        default: return "The legacy reader rejected this payload.";
        }
    default: return "Movie source names cannot be decoded.";
    }
}

typedef struct ActorArchives
{
    MMPrivateArchive *source, *destination;
} ActorArchives;

static int copy_actor_asset(void *p, MMU32 type, MMU32 number, MMU32 *copied)
{
    const ActorArchives *archives = p;
    return mm_private_archive_copy(archives->source, archives->destination, type, number, 0, copied);
}

static int clone_actor_track(void *p, MMU32 actor, MMU32 *copied)
{
    const ActorArchives *archives = p;
    return mm_private_archive_copy(archives->source, archives->destination, MM_ACTOR_CHUNK, actor, 1, copied);
}

static MMU32 actor_name(unsigned short *units, unsigned width, const MMTextHost *text, const char *ansi)
{
    MMStringValue name = {0};
    name.buffer = units;
    name.unit = width;
    mm_string_from_ansi(&name, text, ansi, strlen(ansi));
    return width == 1 ? *(const unsigned char *)units : units[0];
}

struct MarcusArchiveClipboard
{
    MMCollectionStorage sources;
    MMU32 source_count, actor, sounds;
    MMI32 frame;
    char name[MM_STRING_MAX + 1];
    int browser_flags;
};

void marcus_archive_clipboard_destroy(MarcusArchiveClipboard *clipboard)
{
    const MMMovieArchiveSource *sources;
    MMU32 i;
    if (!clipboard)
        return;
    sources = (const MMMovieArchiveSource *)clipboard->sources.buffers[0].data;
    for (i = 0; i < clipboard->source_count; ++i)
        free((void *)sources[i].name);
    mm_storage_dispose(&clipboard->sources);
    free(clipboard);
}

static int clipboard_source_append(MarcusArchiveClipboard *clipboard, MMI32 id, const char *name, size_t bytes)
{
    MMArray array = {0};
    MMMovieArchiveSource row = {0};
    char *owned = malloc(bytes + 1);
    if (!owned)
        return 0;
    memcpy(owned, name, bytes + 1);
    row.id = id;
    row.name = owned;
    array.storage = &clipboard->sources;
    array.width = sizeof(row);
    array.count = clipboard->source_count;
    array.free_count = -1;
    clipboard->sources.min_grow[0] = clipboard->sources.buffers[0].capacity / 2;
    if (mm_array_add(&array, &row, NULL) != MM_STORAGE_OK)
    {
        free(owned);
        return 0;
    }
    clipboard->source_count = array.count;
    return 1;
}

typedef struct ClipboardSourceReader
{
    MarcusArchiveClipboard *clipboard;
    const MMPrivateArchiveProfile *profile;
    const MMTextHost *text;
    int out_of_memory;
} ClipboardSourceReader;

static int clipboard_root(void *p, MMU32 number)
{
    (void)p;
    (void)number;
    return 1;
}

static int clipboard_scene(void *p, MMU32 slot, const MMMovieSessionScene *scene)
{
    (void)p;
    (void)slot;
    (void)scene;
    return 1;
}

static int clipboard_source(void *p, const MMMovieSourceName *source)
{
    ClipboardSourceReader *input = p;
    char name[MM_MOVIE_SOURCE_NAME_BYTES];
    size_t count;
    if (!marcus_archive_source_utf8(source, input->profile, input->text, name, &count))
        return 0;
    if (!clipboard_source_append(input->clipboard, source->id, name, count))
    {
        input->out_of_memory = 1;
        return 0;
    }
    return 1;
}

static const char *read_clipboard_sources(MarcusArchiveClipboard *clipboard, MMPrivateArchive *archive,
                                         const MMPrivateArchiveProfile *profile, const MMTextHost *text)
{
    static const char rootless_source[] = "3D Movie Maker/3DMOVIE";
    const MMArchiveReaderOptions options = archive_options(profile);
    ClipboardSourceReader input;
    MMArchiveDirectorySink sink = {NULL, clipboard_root, clipboard_scene, clipboard_source};
    MMArchiveDirectoryError detail;
    MMArchiveReader *reader;
    int status = mm_archive_reader_open_owner(mm_private_archive_reader(archive), &options, &reader);
    if (status != MM_ARCHIVE_READ_OK)
        return status == MM_ARCHIVE_MEMORY ? "Insufficient memory to open archive." : "Cannot read the chunk index.";
    input.clipboard = clipboard;
    input.profile = profile;
    input.text = text;
    input.out_of_memory = 0;
    sink.context = &input;
    status = mm_archive_reader_directory(reader, &sink, &detail);
    if (status == MM_ARCHIVE_DIRECTORY_OK && !mm_archive_reader_movie(reader) &&
        !clipboard_source_append(clipboard, 1, rootless_source, sizeof(rootless_source) - 1))
        input.out_of_memory = 1;
    mm_archive_reader_close(reader);
    if (input.out_of_memory)
        status = MM_ARCHIVE_DIRECTORY_MEMORY;
    return status == MM_ARCHIVE_DIRECTORY_OK ? NULL : marcus_archive_directory_error(status, detail.payload_status);
}

static MMActorArchiveHost clipboard_host(ActorArchives *archives, const MMMovieArchiveEditor *source,
    const MMMovieArchiveEditor *destination, MMMovieArchiveSourceMap *map)
{
    MMActorArchiveHost host = {NULL, NULL, NULL, clone_actor_track, copy_actor_asset, NULL, NULL};
    host.context = archives;
    host.source = source;
    host.destination = destination;
    host.source_map = map;
    host.map_source = map ? mm_movie_archive_map_source : NULL;
    return host;
}

static const char *copy_clipboard_track(MarcusArchiveClipboard *clipboard, ActorArchives *archives, MMU32 scene,
                                         const MarcusObject *object, MMU32 creator, int *transfer)
{
    const MMMovieArchiveEditor source = mm_private_archive_editor(archives->source);
    const MMMovieArchiveEditor destination = mm_private_archive_editor(archives->destination);
    const MMActorArchiveHost host = clipboard_host(archives, &source, &destination, NULL);
    MMActorArchiveRequest request = {0};
    unsigned short units[sizeof(object->name)];
    size_t count, bytes;
    if (mm_text_read_utf8(object->name, strlen(object->name), units, sizeof(units) / sizeof(*units), &count) != 1)
        return "Cannot decode the object name.";
    bytes = mm_text_ansi_from_units(NULL, units, count, clipboard->name, sizeof(object->name), 0);
    if (count && !bytes)
        return "Cannot encode the object name.";
    clipboard->name[bytes] = '\0';
    clipboard->browser_flags = object->text ? 2 : object->prop ? 1 : 0;
    if (!destination.add(destination.context, MM_SCENE_CHUNK, 0, &clipboard->sounds, NULL))
        return "Cannot create the object clipboard.";
    request.source_scene = scene;
    request.destination_scene = clipboard->sounds;
    request.actor_id = (MMI32)object->actor_id;
    if (!mm_actor_archive_find(&source, scene, request.actor_id, &request.actor))
        return "Cannot find the selected object in the movie.";
    *transfer = mm_actor_archive_transfer(&request, &host, &clipboard->actor, NULL);
    if (*transfer == MM_ACTOR_ARCHIVE_OK && !mm_private_archive_save(archives->destination, creator))
        return "Cannot save the object clipboard.";
    return NULL;
}

MarcusArchiveClipboard *marcus_archive_copy_object(const char *movie, const char *candidate,
    MMU32 scene, MMI32 frame, const MarcusObject *object, const MMPrivateArchiveProfile *profile,
    const MMTextHost *text, MMU32 creator, MarcusClipboardError *error)
{
    MarcusArchiveClipboard *clipboard;
    ActorArchives archives = {NULL, NULL};
    memset(error, 0, sizeof(*error));
    clipboard = calloc(1, sizeof(*clipboard));
    if (!clipboard)
    {
        error->message = "Insufficient memory to read the movie directory.";
        return NULL;
    }
    mm_storage_init(&clipboard->sources, NULL);
    clipboard->frame = frame;
    archives.source = marcus_archive_open(movie, MM_PRIVATE_ARCHIVE_READ, profile);
    if (!archives.source)
        error->message = "Cannot open this file as a chunky archive.";
    else
        error->message = read_clipboard_sources(clipboard, archives.source, profile, text);
    if (!error->message)
    {
        archives.destination = marcus_archive_open(candidate, MM_PRIVATE_ARCHIVE_CREATE, profile);
        error->message = archives.destination ?
            copy_clipboard_track(clipboard, &archives, scene, object, creator, &error->actor) :
            "Cannot create the object clipboard.";
    }
    mm_private_archive_close(archives.destination);
    mm_private_archive_close(archives.source);
    if (error->message || error->actor != MM_ACTOR_ARCHIVE_OK)
    {
        marcus_archive_clipboard_destroy(clipboard);
        return NULL;
    }
    return clipboard;
}

static int paste_error(MarcusClipboardError *error, const char *message)
{
    error->message = message;
    return 0;
}

static int paste_clipboard_track(const MarcusArchiveClipboard *clipboard, ActorArchives *archives,
    MMU32 scene, MMI32 frame, MMI32 id, const MMTextHost *text, MarcusClipboardError *error)
{
    const MMMovieArchiveEditor source = mm_private_archive_editor(archives->source);
    const MMMovieArchiveEditor destination = mm_private_archive_editor(archives->destination);
    MMActorArchiveRequest request;
    MMMovieArchiveSourceMap map = {0};
    const MMActorArchiveHost host = clipboard_host(archives, &source, &destination, &map);
    MMSceneState scene_state;
    MMMovieArchiveRoot root;
    MMActorHeader header;
    MMActorArchivePlacement placement;
    MMActorArchiveName name = {0};
    unsigned short units[MM_STRING_MAX + 2];
    MMU32 actor;
    if (!mm_scene_archive_read_header(&destination, scene, &scene_state) ||
        frame < scene_state.firstFrame || frame > scene_state.lastFrame)
        return paste_error(error, "The paste frame is outside the scene.");
    error->scene = mm_movie_archive_ensure_root(&destination, &root);
    if (error->scene != MM_MOVIE_SCENE_ARCHIVE_OK)
        return 0;
    map.fallback_retail = MM_MOVIE_RETAIL_SOURCE_ID;
    if (root.created)
    {
        MMI32 assigned;
        error->source = mm_movie_archive_register_source(&destination, text, root.number, 1, NULL, &assigned);
        if (error->source != MM_MOVIE_SOURCE_EDIT_OK)
            return 0;
        map.fallback_retail = 1;
    }
    map.destination = &destination;
    map.text = text;
    map.movie = root.number;
    map.count = clipboard->source_count;
    if (map.count)
    {
        const size_t bytes = map.count * sizeof(*map.sources);
        map.sources = malloc(bytes);
        if (!map.sources)
            return paste_error(error, "Cannot allocate the movie content sources.");
        memcpy(map.sources, clipboard->sources.buffers[0].data, bytes);
    }
    mm_movie_archive_prepare_source_map(&map);
    request.actor = clipboard->actor;
    request.source_scene = clipboard->sounds;
    request.destination_scene = scene;
    request.actor_id = id;
    request.source_frame = clipboard->frame;
    request.destination_frame = frame;
    error->actor = mm_actor_archive_transfer(&request, &host, &actor, &header);
    error->source = map.result;
    free(map.sources);
    if (error->actor != MM_ACTOR_ARCHIVE_OK || error->source != MM_MOVIE_SOURCE_EDIT_OK)
        return 0;
    placement.scene = scene;
    placement.actor = actor;
    placement.first_frame = scene_state.firstFrame;
    placement.invalidate_thumbnail = 1;
    name.actor.actor_id = header.actor_id;
    name.actor.reference_count = 1;
    name.actor.source_id = header.source_id;
    name.actor.browser_flags = (MMU32)clipboard->browser_flags;
    name.actor.template_type = header.template_type;
    name.actor.template_number = header.template_number;
    name.text = (const unsigned char *)units + destination.text_unit;
    name.characters = actor_name(units, destination.text_unit, text, clipboard->name);
    error->registration = mm_actor_archive_register(&destination, root.number, &placement, &name);
    return error->registration == MM_ACTOR_REGISTER_OK;
}

int marcus_archive_paste_object(const MarcusArchiveClipboard *clipboard, const char *track, const char *candidate,
    MMU32 scene, MMI32 frame, MMI32 id, const MMPrivateArchiveProfile *profile, const MMTextHost *text,
    MMU32 creator, MarcusClipboardError *error)
{
    ActorArchives archives;
    int pasted = 0;
    memset(error, 0, sizeof(*error));
    archives.source = marcus_archive_open(track, MM_PRIVATE_ARCHIVE_READ, profile);
    archives.destination = marcus_archive_open(candidate, MM_PRIVATE_ARCHIVE_EDIT, profile);
    if (!archives.source || !archives.destination)
        error->message = "Cannot open the movie or object working copy.";
    else
    {
        pasted = paste_clipboard_track(clipboard, &archives, scene, frame, id, text, error);
        if (pasted && !mm_private_archive_save(archives.destination, creator))
            pasted = paste_error(error, "Cannot save the pasted object.");
    }
    mm_private_archive_close(archives.destination);
    mm_private_archive_close(archives.source);
    return pasted;
}

static int replace_text(MMPrivateArchive *file, MMU32 actor, const MMMovieActorTextEdit *edit,
                        const MarcusArchiveDrafts *drafts, int *detail)
{
    ActorArchives archives;
    MMMovieArchiveEditor editor;
    MMActorAuthorHost host = {0};
    MMActorTextReplacement request;
    unsigned short units[257];
    int status;
    *detail = 0;
    archives.source = drafts->open(drafts->context, edit->file);
    if (!archives.source)
        return MARCUS_ARCHIVE_TEXT_OPEN;
    archives.destination = file;
    editor = mm_private_archive_editor(file);
    request.actor = actor;
    request.name = (const unsigned char *)units + editor.text_unit;
    request.characters = actor_name(units, editor.text_unit, drafts->text, edit->name.legacy);
    request.retail_font = edit->retail_font;
    request.custom_material = edit->custom_material;
    host.destination = &editor;
    host.text = drafts->text;
    host.context = &archives;
    host.copy = copy_actor_asset;
    status = mm_actor_archive_replace_text(&host, &request, detail);
    mm_private_archive_close(archives.source);
    return status;
}

typedef struct ActorAppearance
{
    MMPrivateArchive *file;
    const MMMovieActorTextEdit *text;
    const MMMovieActorTrackEdit *costume;
    const MarcusArchiveDrafts *drafts;
    MarcusArchiveActorResult *result;
} ActorAppearance;

static int apply_appearance(void *p, MMU32 actor)
{
    const ActorAppearance *appearance = p;
    const MarcusArchiveDrafts *drafts = appearance->drafts;
    MarcusArchiveActorResult *result = appearance->result;
    if (appearance->text)
    {
        result->text = replace_text(appearance->file, actor, appearance->text, drafts, &result->text_detail);
        if (result->text != MM_ACTOR_AUTHOR_OK)
            return 0;
    }
    if (appearance->costume)
    {
        MMPrivateArchive *track = drafts->open(drafts->context, appearance->costume->file);
        result->costume = track ? mm_actor_archive_import_events(appearance->file, actor, track)
                                : MM_ACTOR_EDIT_ARCHIVE_COPY;
        mm_private_archive_close(track);
    }
    return result->costume == MM_ACTOR_EDIT_ARCHIVE_OK;
}

static int place_edits(MMPrivateArchive *file, const MarcusArchiveMovieEdits *edits,
                       const MarcusArchiveDrafts *drafts, MarcusArchiveActorResult *out)
{
    const MMMovieArchiveEditor editor = mm_private_archive_editor(file);
    MMMoviePoseJournal merged;
    const MMMovieActorPoseEdit *rows;
    MMU32 i;
    int ready;
    mm_movie_pose_journal_init(&merged, NULL);
    ready = mm_movie_pose_journal_export(&merged, edits->poses, edits->texts, edits->costumes);
    if (ready)
    {
        rows = mm_movie_pose_journal_data(&merged);
        for (i = 0; i < merged.count; ++i)
        {
            const MMMovieActorPoseEdit *row = rows + i;
            ActorAppearance appearance;
            MMActorArchivePose pose;
            appearance.file = file;
            appearance.text = mm_movie_text_journal_find(edits->texts, row->scene, row->actor);
            appearance.costume = mm_movie_track_journal_find(edits->costumes, row->scene, row->actor);
            appearance.drafts = drafts;
            appearance.result = out;
            pose.scene = row->scene;
            pose.actor = row->actor;
            pose.translation = row->offset;
            pose.rotation = row->shape.rotation;
            pose.scale = row->shape.scale;
            pose.context = &appearance;
            pose.appearance = apply_appearance;
            out->status = mm_actor_archive_place(&editor, &pose);
            if (out->status != MM_ACTOR_EDIT_ARCHIVE_OK)
                break;
        }
    }
    mm_movie_pose_journal_dispose(&merged);
    return ready;
}

static int append_scenes(const MMMovieArchiveEditor *editor, const MMMovieSceneDefinitions *definitions,
                         int empty_movie, const MMTextHost *text, int *status)
{
    const MMPreviewLiveScene *rows = mm_movie_scene_definitions_data(definitions);
    MMU32 i;
    for (i = 0; i < definitions->count; ++i)
    {
        MMMovieArchiveRoot root;
        MMI32 source;
        *status = mm_movie_archive_ensure_root(editor, &root);
        if (*status != MM_MOVIE_SCENE_ARCHIVE_OK)
            return MARCUS_MATERIALIZE_SCENE;
        *status = mm_movie_archive_register_source(editor, text, root.number, root.created && !empty_movie ? 1 : 2,
                                                  NULL, &source);
        if (*status != MM_MOVIE_SOURCE_EDIT_OK)
            return MARCUS_MATERIALIZE_SOURCE;
        *status = mm_movie_archive_append_scene(editor, root.number, rows[i].number, source, rows[i].background);
        if (*status != MM_MOVIE_SCENE_ARCHIVE_OK)
            return MARCUS_MATERIALIZE_SCENE;
    }
    return MARCUS_MATERIALIZE_OK;
}

static const char *embed_boards(MMPrivateArchive *file, const MarcusBoardStore *boards,
                               const MarcusArchiveDrafts *drafts)
{
    const MarcusStoredBoard *rows = marcus_board_store_data(boards);
    MMU32 i;
    for (i = 0; i < boards->count; ++i)
    {
        MMPrivateArchive *source = drafts->open(drafts->context, marcus_stored_board_file(rows + i));
        const char *error = source ? marcus_board_embed(file, source, rows + i) : "Cannot embed storyboard resources.";
        mm_private_archive_close(source);
        if (error)
            return error;
    }
    return NULL;
}

static int insert_prop(const MMMovieArchiveEditor *editor, MMPrivateArchive *file, MMPrivateArchive *library,
                       const MMMoviePropEdit *prop, const MarcusArchiveDrafts *drafts, MarcusArchiveMovieResult *out)
{
    const MMPreviewLiveProp *definition = &prop->definition;
    const char *ansi = prop->name;
    MMTextAuthorName text_name;
    unsigned short units[257];
    ActorArchives archives;
    MMMovieArchiveEditor source;
    MMActorAuthorHost host;
    MMActorInsert request;
    if (prop->file)
    {
        out->status = mm_text_author_prepare_name(prop->name, &text_name);
        if (out->status != MM_TEXT_NAME_OK)
            return MARCUS_MATERIALIZE_TEXT_NAME;
        ansi = text_name.legacy;
    }
    archives.source = library;
    archives.destination = file;
    source = mm_private_archive_editor(library);
    host.source = &source;
    host.destination = editor;
    host.text = drafts->text;
    host.context = &archives;
    host.copy = copy_actor_asset;
    request.scene = definition->scene;
    request.number = definition->number;
    request.template_number = definition->template_number;
    request.actor_id = definition->actor_id;
    request.first = definition->first;
    request.last = definition->last;
    request.point = definition->point;
    request.name = (const unsigned char *)units + editor->text_unit;
    request.characters = actor_name(units, editor->text_unit, drafts->text, ansi);
    request.embedded_text = prop->file != NULL;
    out->detail = 0;
    out->status = mm_actor_archive_insert(&host, &request, &out->detail);
    return out->status == MM_ACTOR_AUTHOR_OK ? MARCUS_MATERIALIZE_OK : MARCUS_MATERIALIZE_INSERT;
}

static int insert_props(const MMMovieArchiveEditor *editor, MMPrivateArchive *file, const char *templates,
                        const MMMoviePropJournal *props, const MarcusArchiveDrafts *drafts,
                        const MMPrivateArchiveProfile *profile, MarcusArchiveMovieResult *out)
{
    const MMMoviePropEdit *rows = mm_movie_prop_journal_data(props);
    MMPrivateArchive *library;
    MMU32 i;
    int result = MARCUS_MATERIALIZE_OK;
    if (!props->count)
        return result;
    library = marcus_archive_open(templates, MM_PRIVATE_ARCHIVE_READ, profile);
    if (!library)
        return MARCUS_MATERIALIZE_LIBRARY;
    for (i = 0; i < props->count; ++i)
    {
        const MMMoviePropEdit *prop = rows + i;
        MMPrivateArchive *source = prop->file ? drafts->open(drafts->context, prop->file) : library;
        result = source ? insert_prop(editor, file, source, prop, drafts, out) : MARCUS_MATERIALIZE_TEXT_OPEN;
        if (prop->file)
            mm_private_archive_close(source);
        if (result != MARCUS_MATERIALIZE_OK)
            break;
    }
    mm_private_archive_close(library);
    return result;
}

static int publish_movie_edits(MMPrivateArchive *file, int empty_movie, const char *templates,
                               const MarcusArchiveMovieEdits *edits, const MarcusArchiveDrafts *drafts,
                               const MMPrivateArchiveProfile *profile, MarcusArchiveMovieResult *out)
{
    const MMMovieArchiveEditor editor = mm_private_archive_editor(file);
    const MMMovieSceneRange *ranges = mm_movie_range_journal_data(edits->ranges);
    const MMMovieCameraEdit *cameras = mm_movie_camera_journal_data(edits->cameras);
    MMU32 i;
    int result = append_scenes(&editor, edits->scenes, empty_movie, drafts->text, &out->status);
    if (result != MARCUS_MATERIALIZE_OK)
        return result;
    out->board_error = embed_boards(file, edits->boards, drafts);
    if (out->board_error)
        return MARCUS_MATERIALIZE_BOARD;
    for (i = 0; i < edits->ranges->count; ++i)
    {
        out->status = mm_scene_archive_set_range(&editor, ranges[i].scene, ranges[i].first, ranges[i].last);
        if (out->status != MM_SCENE_ARCHIVE_EDIT_OK)
            return MARCUS_MATERIALIZE_FRAME;
    }
    result = insert_props(&editor, file, templates, edits->props, drafts, profile, out);
    if (result != MARCUS_MATERIALIZE_OK)
        return result;
    if (!place_edits(file, edits, drafts, &out->actor))
        return MARCUS_MATERIALIZE_MEMORY;
    if (out->actor.status != MM_ACTOR_EDIT_ARCHIVE_OK)
        return MARCUS_MATERIALIZE_ACTOR;
    for (i = 0; i < edits->cameras->count; ++i)
    {
        out->status = mm_scene_archive_set_camera(&editor, cameras[i].scene, cameras[i].frame, cameras[i].camera);
        if (out->status != MM_SCENE_ARCHIVE_EDIT_OK)
            return MARCUS_MATERIALIZE_FRAME;
    }
    return mm_movie_archive_ensure_empty_rollcall(&editor) ? MARCUS_MATERIALIZE_OK : MARCUS_MATERIALIZE_ROLLCALL;
}

int marcus_archive_materialize(const char *path, int empty_movie, const char *templates,
                               const MarcusArchiveMovieEdits *edits, const MarcusArchiveDrafts *drafts,
                               const MMPrivateArchiveProfile *profile, MMU32 creator, MarcusArchiveMovieResult *out)
{
    MMPrivateArchive *file = marcus_archive_open(path, empty_movie ? MM_PRIVATE_ARCHIVE_CREATE : MM_PRIVATE_ARCHIVE_EDIT,
                                                 profile);
    int result = MARCUS_MATERIALIZE_OPEN;
    memset(out, 0, sizeof(*out));
    if (file)
    {
        result = publish_movie_edits(file, empty_movie, templates, edits, drafts, profile, out);
        if (result == MARCUS_MATERIALIZE_OK && !mm_private_archive_save(file, creator))
            result = MARCUS_MATERIALIZE_SAVE;
    }
    mm_private_archive_close(file);
    return result;
}

static int apply_animation_text(void *p, MMPrivateArchive *file, MMU32 actor)
{
    ActorAppearance *appearance = p;
    appearance->file = file;
    return apply_appearance(appearance, actor);
}

int marcus_archive_animate(const char *path, const char *track, const MMPrivateArchiveProfile *profile,
                           const MMActorArchiveAnimation *animation, const MMMovieActorTextEdit *text,
                           const MarcusArchiveDrafts *drafts,
                           MMU32 creator, MarcusArchiveAnimationResult *out)
{
    MMPrivateArchive *candidate = marcus_archive_open(path, MM_PRIVATE_ARCHIVE_EDIT, profile);
    MMPrivateArchive *source = marcus_archive_open(track, MM_PRIVATE_ARCHIVE_READ, profile);
    MMActorArchiveAnimation request = *animation;
    ActorAppearance appearance = {0};
    int result = MARCUS_ARCHIVE_ANIMATION_OPEN;
    memset(out, 0, sizeof(*out));
    if (text)
    {
        appearance.text = text;
        appearance.drafts = drafts;
        appearance.result = &out->actor;
        request.context = &appearance;
        request.appearance = apply_animation_text;
    }
    if (candidate && source)
    {
        out->actor.status = mm_actor_archive_animate(candidate, source, &request, &out->scene);
        if (out->actor.status != MM_ACTOR_EDIT_ARCHIVE_OK || out->scene != MM_SCENE_ARCHIVE_EDIT_OK)
            result = MARCUS_ARCHIVE_ANIMATION_EDIT;
        else
            result = mm_private_archive_save(candidate, creator)
                ? MARCUS_ARCHIVE_ANIMATION_OK : MARCUS_ARCHIVE_ANIMATION_SAVE;
    }
    mm_private_archive_close(source);
    mm_private_archive_close(candidate);
    return result;
}
