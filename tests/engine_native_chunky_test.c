#include "mm/archive_reader.h"
#include "mm/chunky_file.h"
#include "mm/movie_content_resolver.h"
#include "mm/native_archive_file.h"
#include "mm/native_files.h"
#include "mm/native_named_file.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SeekInput
{
    MMU32 scene;
    MMI32 frame, source;
    const char *path;
    void *file;
} SeekInput;

enum { NO_BODY, BODY_PARTS, BODY_MESHES, FRAME_BODIES };

static int number(const char *text, MMI32 *out)
{
    char *end;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (end == text || *end || errno == ERANGE || value < (-2147483647L - 1) || value > 2147483647L)
        return 0;
    *out = (MMI32)value;
    return 1;
}

static int seek_input(SeekInput *input, int argc, char **argv, int auto_content)
{
    MMI32 scene;
    if ((auto_content ? argc != 6 : argc != 5 && argc != 7) ||
        !number(argv[3], &scene) || scene < 0 || !number(argv[4], &input->frame))
        return 0;
    input->scene = (MMU32)scene;
    if (auto_content)
        input->path = argv[5];
    else if (argc == 7)
    {
        if (!number(argv[5], &input->source) || input->source <= 0)
            return 0;
        input->path = argv[6];
    }
    return 1;
}

static int open_source(void *context, const MMMovieAsset *asset, MMReadStream *stream)
{
    SeekInput *input = (SeekInput *)context;
    MMNativeOpenOptions options;
    if (asset->source != input->source || !input->path)
    {
        fprintf(stderr, "Missing content source %ld for %08lx:%lu\n", (long)asset->source,
                (unsigned long)asset->type, (unsigned long)asset->number);
        return MM_MOVIE_ASSET_NOT_FOUND;
    }
    mm_native_open_options_init(&options);
    mm_native_file_close(input->file);
    input->file = mm_native_file_open(input->path, &options);
    return input->file && mm_native_file_read_stream(input->file, stream) ? MM_MOVIE_OK : MM_MOVIE_IO;
}

static void print_camera(const MMMovieCamera *camera)
{
    unsigned i, j;
    printf("BKGD %ld %lu CAM %lu %lu %ld %ld %u\n", (long)camera->background.source,
           (unsigned long)camera->background.number, (unsigned long)camera->slot, (unsigned long)camera->number,
           (long)camera->data.near_z, (long)camera->data.far_z, (unsigned)camera->data.fov);
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 3; ++j)
            printf("%ld%c", (long)camera->data.matrix.m[i][j], i == 3 && j == 2 ? '\n' : ' ');
}

static int check_camera(const MMMovieSession *movie, SeekInput *input, MMMovieSourceRead source, void *context)
{
    MMMovieCamera camera;
    int status = mm_movie_session_camera(movie, input->scene, input->frame, source, context, &camera);
    if (status != MM_MOVIE_OK)
    {
        fprintf(stderr, "Cannot seek to camera (%d)\n", status);
        return 0;
    }
    print_camera(&camera);
    return 1;
}

static int check_movie(const MMReadStream *stream, const MMChunkPolicy *policy)
{
    MMMovieSession *movie;
    const MMMovieSessionRoot *root;
    MMMovieSessionScene scene;
    MMU32 slot;
    int result = mm_movie_session_open(stream, policy, &movie);
    if (result != MM_MOVIE_OK)
    {
        fprintf(stderr, "Cannot read movie root (%d)\n", result);
        return 0;
    }
    root = mm_movie_session_root(movie);
    printf("MVIE %lu %d %d\n", (unsigned long)root->number, root->header.current_version,
           root->header.backward_version);
    for (slot = 0; (result = mm_movie_session_scene(movie, slot, &scene)) == MM_MOVIE_OK; ++slot)
        printf("SCEN %lu %lu %ld %ld %ld\n", (unsigned long)slot, (unsigned long)scene.number,
               (long)scene.first_frame, (long)scene.last_frame, (long)scene.transition);
    mm_movie_session_close(movie);
    if (result != MM_MOVIE_SCENE_NOT_FOUND)
        fprintf(stderr, "Cannot read scene slot %lu (%d)\n", (unsigned long)slot, result);
    return result == MM_MOVIE_SCENE_NOT_FOUND;
}

static void print_actor(const MMMovieEvaluatedActor *value)
{
    unsigned i, j;
    printf("ACTR %lu %ld %ld %lu %d %ld %ld %ld %ld %ld %ld\n",
           (unsigned long)value->number, (long)value->header.actor_id, (long)value->header.source_id,
           (unsigned long)value->header.template_number, value->state.onStage, (long)value->state.action,
           (long)value->state.cel, (long)value->state.nextEvent, (long)value->state.position.dxr,
           (long)value->state.position.dyr, (long)value->state.position.dzr);
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 3; ++j)
            printf("%ld ", (long)value->orientation.m[i][j]);
    printf("%ld %ld %ld\n", (long)value->translation.dxr, (long)value->translation.dyr,
           (long)value->translation.dzr);
}

static int check_actors(const MMMovieSession *movie, SeekInput *input, MMMovieSourceRead source, void *context)
{
    MMMovieEvaluatedActor value;
    MMU32 ordinal = 0;
    int status;
    while ((status = mm_movie_session_actor(movie, input->scene, input->frame, ordinal,
                                            source, context, &value)) == MM_MOVIE_OK)
    {
        print_actor(&value);
        ++ordinal;
    }
    if (status != MM_MOVIE_ACTOR_NOT_FOUND)
        fprintf(stderr, "Cannot evaluate actor %lu (%d)\n", (unsigned long)ordinal, status);
    return status == MM_MOVIE_ACTOR_NOT_FOUND;
}

static int check_frame(const MMMovieSession *movie, SeekInput *input, MMMovieContent *content)
{
    MMMovieEvaluatedFrame *frame;
    MMU32 i;
    int status = mm_movie_session_seek(movie, input->scene, input->frame, mm_movie_content_source, content, &frame);
    if (status != MM_MOVIE_OK)
    {
        fprintf(stderr, "Cannot seek to movie frame (%d)\n", status);
        return 0;
    }
    print_camera(&frame->camera);
    for (i = 0; i < frame->actor_count; ++i)
        print_actor(&frame->actors[i]);
    mm_movie_frame_dispose(frame);
    return 1;
}

static MMU32 fold(MMU32 hash, MMU32 value)
{
    return (hash ^ value) * (MMU32)16777619UL;
}

static MMU32 mesh_hash(const MMModelMesh *mesh)
{
    const MMModelDescription *desc = &mesh->description;
    const MMModelVertex *v;
    const MMModelFace *f;
    MMU32 hash = fold((MMU32)2166136261UL, (MMU32)desc->radius);
    MMI32 i;
    unsigned j;
    for (j = 0; j < 3; ++j)
    {
        hash = fold(hash, (MMU32)desc->bounds.min[j]);
        hash = fold(hash, (MMU32)desc->bounds.max[j]);
        hash = fold(hash, (MMU32)desc->pivot[j]);
    }
    for (i = 0; i < desc->vertices; ++i)
    {
        v = &mesh->vertices[i];
        for (j = 0; j < 3; ++j)
        {
            hash = fold(hash, (MMU32)v->position[j]);
            hash = fold(hash, (MMU32)v->normal[j]);
        }
        for (j = 0; j < 2; ++j)
            hash = fold(hash, (MMU32)v->mapping[j]);
        hash = fold(hash, v->reserved);
        hash = fold(hash, v->index);
        hash = fold(hash, v->red);
        hash = fold(hash, v->green);
        hash = fold(hash, v->blue);
    }
    for (i = 0; i < desc->faces; ++i)
    {
        f = &mesh->faces[i];
        for (j = 0; j < 3; ++j)
        {
            hash = fold(hash, f->vertices[j]);
            hash = fold(hash, f->edges[j]);
            hash = fold(hash, (MMU32)f->normal[j]);
        }
        hash = fold(hash, f->smoothing);
        hash = fold(hash, f->flags);
        hash = fold(hash, f->pad);
        hash = fold(hash, (MMU32)f->distance);
    }
    return hash;
}

static int print_meshes(const MMMovieSession *movie, const MMMovieEvaluatedBody *body, MMMovieContent *content)
{
    MMMovieBodyMeshes *geometry;
    const MMMoviePartMesh *part;
    const MMMovieBodyPart *binding;
    const MMModelMesh *mesh;
    MMU32 i, next = 0;
    unsigned j;
    int status = mm_movie_session_body_meshes(movie, body, mm_movie_content_source, content, &geometry);
    if (status != MM_MOVIE_OK)
        return status;
    for (i = 0; i < geometry->part_count; ++i)
    {
        part = &geometry->parts[i];
        binding = &body->parts[i];
        printf("APPEARANCE %08lx %lu %ld %08lx %lu %08lx %lu\n", (unsigned long)binding->active_model.type,
               (unsigned long)binding->active_model.number, (long)binding->material.source,
               (unsigned long)binding->material.type, (unsigned long)binding->material.number,
               (unsigned long)binding->material_owner.type, (unsigned long)binding->material_owner.number);
        printf("WORLD %ld", (long)part->model_index);
        for (j = 0; j < 12; ++j)
            printf(" %ld", (long)part->world.m[j / 3][j % 3]);
        putchar('\n');
        if (part->model_index == (MMI32)next)
        {
            mesh = &geometry->models[next++];
            printf("MESH %ld %08lx %lu %lu %ld %ld %08lx\n", (long)body->model_owner.source,
                   (unsigned long)body->model_owner.type, (unsigned long)body->model_owner.number,
                   (unsigned long)binding->active_model.number, (long)mesh->description.vertices,
                   (long)mesh->description.faces, (unsigned long)mesh_hash(mesh));
        }
    }
    mm_movie_body_meshes_dispose(geometry);
    return status;
}

static void print_body(const MMMovieEvaluatedBody *body)
{
    const MMMovieBodyPart *part;
    MMU32 i;
    unsigned j;
    print_actor(&body->actor);
    printf("BODY %lu\n", (unsigned long)body->part_count);
    for (i = 0; i < body->part_count; ++i)
    {
        part = &body->parts[i];
        printf("PART %ld %ld %ld %08lx %lu", (long)part->parent, (long)part->set, (long)part->model.source,
               (unsigned long)part->model.type, (unsigned long)part->model.number);
        for (j = 0; j < 12; ++j)
            printf(" %ld", (long)part->matrix.m[j / 3][j % 3]);
        putchar('\n');
    }
}

static int check_frame_bodies(const MMMovieSession *movie, SeekInput *input, MMMovieContent *content)
{
    MMMovieEvaluatedFrame *frame;
    MMU32 i;
    int status = mm_movie_session_seek_with_bodies(movie, input->scene, input->frame,
                                                  mm_movie_content_source, content, &frame);
    if (status != MM_MOVIE_OK)
    {
        fprintf(stderr, "Cannot seek to movie frame with bodies (%d)\n", status);
        return 0;
    }
    print_camera(&frame->camera);
    for (i = 0; i < frame->actor_count; ++i)
    {
        print_body(frame->bodies[i]);
        status = print_meshes(movie, frame->bodies[i], content);
        if (status != MM_MOVIE_OK)
        {
            fprintf(stderr, "Cannot load body meshes for actor %lu (%d)\n", (unsigned long)i, status);
            break;
        }
    }
    mm_movie_frame_dispose(frame);
    return status == MM_MOVIE_OK;
}

static int check_bodies(const MMMovieSession *movie, SeekInput *input, MMMovieContent *content, int bodies)
{
    MMMovieEvaluatedBody *body;
    MMU32 ordinal = 0;
    int status;
    while ((status = mm_movie_session_body(movie, input->scene, input->frame, ordinal,
                                           mm_movie_content_source, content, &body)) == MM_MOVIE_OK)
    {
        print_body(body);
        if (bodies == BODY_MESHES)
            status = print_meshes(movie, body, content);
        mm_movie_body_dispose(body);
        if (status != MM_MOVIE_OK)
        {
            fprintf(stderr, "Cannot load body meshes for actor %lu (%d)\n", (unsigned long)ordinal, status);
            return 0;
        }
        ++ordinal;
    }
    if (status != MM_MOVIE_ACTOR_NOT_FOUND)
        fprintf(stderr, "Cannot evaluate body %lu (%d)\n", (unsigned long)ordinal, status);
    return status == MM_MOVIE_ACTOR_NOT_FOUND;
}

static int open_archive(void *unused, const char *location, void **file, MMReadStream *stream)
{
    MMNativeOpenOptions options;
    void *handle;
    (void)unused;
    mm_native_open_options_init(&options);
    handle = mm_native_file_open(location, &options);
    if (!handle || !mm_native_file_read_stream(handle, stream))
    {
        mm_native_file_close(handle);
        return MM_MOVIE_IO;
    }
    *file = handle;
    return MM_MOVIE_OK;
}

static void close_archive(void *unused, void *file)
{
    (void)unused;
    mm_native_file_close(file);
}

static int mount_retail(MMVfs *vfs, const char *directory)
{
    void *iterator;
    char name[1024], path[2048];
    const char *extension;
    size_t length = strlen(directory);
    MMNativePathInfo info;
    MMVfsMount file;
    int status, result = MM_VFS_OK;
    if (!length || length > sizeof(path) - sizeof(name) - 1)
        return 0;
    memcpy(path, directory, length);
    if (!mm_native_is_separator(path[length - 1]))
        path[length++] = mm_native_separator();
    path[length] = '*';
    path[length + 1] = 0;
    status = mm_native_directory_first(path, &iterator, name, &info);
    while (status == MM_NATIVE_ENTRY && result == MM_VFS_OK)
    {
        extension = strrchr(name, '.');
        if (!info.directory && extension && strlen(extension) == 4 && extension[1] == '3' &&
            (extension[2] == 'c' || extension[2] == 'C') && (extension[3] == 'n' || extension[3] == 'N'))
        {
            memcpy(path + length, name, strlen(name) + 1);
            file.name = name;
            file.location = path;
            result = mm_vfs_mount(vfs, "3dmovie", &file, 1, 0);
        }
        status = mm_native_directory_next(iterator, name, &info);
    }
    mm_native_directory_close(iterator);
    return status == MM_NATIVE_END && result == MM_VFS_OK;
}

static int check_seek(const MMReadStream *stream, const MMChunkPolicy *policy, SeekInput *input,
                      int actors, int auto_content, int bodies)
{
    const MMMovieArchiveProvider provider = {NULL, open_archive, close_archive};
    MMMovieSession *movie;
    MMVfs *vfs = NULL;
    MMMovieContent *content = NULL;
    int ok = 0, status;
    status = mm_movie_session_open(stream, policy, &movie);
    if (status != MM_MOVIE_OK)
    {
        fprintf(stderr, "Cannot read movie root (%d)\n", status);
        return 0;
    }
    if (auto_content)
    {
        vfs = mm_vfs_create(NULL);
        if (vfs && mount_retail(vfs, input->path))
            content = mm_movie_content_open(movie, vfs, &provider);
        if (content && bodies == FRAME_BODIES)
            ok = check_frame_bodies(movie, input, content);
        else if (content)
            ok = bodies != NO_BODY ? check_bodies(movie, input, content, bodies) : check_frame(movie, input, content);
        else
            fprintf(stderr, "Cannot mount retail content from %s\n", input->path);
    }
    else
        ok = actors ? check_actors(movie, input, open_source, input) : check_camera(movie, input, open_source, input);
    mm_movie_content_close(content);
    mm_vfs_destroy(vfs);
    mm_movie_session_close(movie);
    mm_native_file_close(input->file);
    return ok;
}

typedef struct NativeArchiveTest
{
    const char *path;
    int live_calls, stored_calls, select_live, prefer_working;
} NativeArchiveTest;

static MMResourceCache *test_live_source(void *p, int working, int *selected, MMChunkOwnerRead **reader)
{
    NativeArchiveTest *test = p;
    ++test->live_calls;
    test->prefer_working = working;
    *selected = test->select_live;
    *reader = NULL;
    return NULL;
}

static int test_stored_source(void *p, MMNativeArchiveSource *source)
{
    NativeArchiveTest *test = p;
    ++test->stored_calls;
    return mm_native_archive_source_open(source, test->path, 0, MM_CHUNK_NAME_WIRE_BYTES);
}

static int check_index(void *file, const char *path, MMU32 type, int movie, SeekInput *seek, int actors,
                       int auto_content, int bodies)
{
    const unsigned short native_order = 1;
    MMChunkPolicy policy = {0, 0, 516}; /* Legacy counted name plus terminator and encoding. */
    const MMArchiveReaderOptions options = {0x7fffffffUL, 0x7fffffffUL, 0x0303, NULL, NULL, NULL, NULL, NULL};
    MMReadStream stream;
    MMNativeArchiveInput input = {0};
    NativeArchiveTest source = {0};
    MMNativeArchiveInputHost host = {NULL, test_live_source, test_stored_source};
    MMArchiveReader *reader;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMReadStream payload;
    MMU32 position, count;
    unsigned char magic[4], prefix[4];
    int ok;

    policy.native_big_endian = *(const unsigned char *)&native_order == 0;
    stream.length = 123;
    if (mm_native_file_read_stream(NULL, &stream) || stream.length != 123 ||
        !mm_native_file_read_stream(file, &stream) ||
        !mm_read_exact(&stream, 0, magic, sizeof(magic)) ||
        mm_read_exact(&stream, stream.length, prefix, 1))
        return 0;
    if (movie)
        return check_movie(&stream, &policy);
    if (seek)
        return check_seek(&stream, &policy, seek, actors, auto_content, bodies);
    source.path = path;
    host.context = &source;
    if (mm_native_archive_input_open(&input, MM_NATIVE_ARCHIVE_STORED, &options, &host) !=
            MM_NATIVE_ARCHIVE_INPUT_OK || source.live_calls || source.stored_calls != 1)
    {
        mm_native_archive_input_close(&input);
        return 0;
    }
    reader = input.reader;
    source.select_live = 1;
    ok = mm_native_archive_input_open(&input, MM_NATIVE_ARCHIVE_LIVE, &options, &host) ==
            MM_NATIVE_ARCHIVE_INPUT_OPEN && source.live_calls == 1 && source.prefer_working &&
         source.stored_calls == 1 && input.reader == reader;
    source.select_live = 0;
    source.path = "";
    ok = ok && mm_native_archive_input_open(&input, MM_NATIVE_ARCHIVE_PREVIEW, &options, &host) ==
            MM_NATIVE_ARCHIVE_INPUT_OPEN && source.live_calls == 2 && !source.prefer_working &&
         source.stored_calls == 2 && input.reader == reader;

    source.select_live = 1;
    ok = ok && !mm_native_archive_open_cache(&host, 0, 1, 0, NULL, NULL, NULL) &&
         source.live_calls == 3 && source.prefer_working && source.stored_calls == 2;
    source.select_live = 0;
    ok = ok && !mm_native_archive_open_cache(&host, 0, 0, 0, NULL, NULL, NULL) &&
         source.live_calls == 4 && !source.prefer_working && source.stored_calls == 3;

    access = mm_archive_reader_access(reader);
    count = access.count;
    ok = ok && mm_archive_reader_catalog(reader, NULL, NULL, NULL) == MM_ARCHIVE_CATALOG_OK &&
         mm_chunk_type_at(&access, type, 0, &entry, &position) == MM_CHUNK_OK &&
         mm_archive_reader_read_stored(reader, &entry, prefix, sizeof(prefix)) &&
         mm_archive_reader_payload(reader, position, &payload) == MM_ARCHIVE_READ_OK &&
         mm_read_exact(&payload, 0, prefix, sizeof(prefix)) &&
         mm_read_exact(&stream, 0, prefix, sizeof(prefix)) &&
         memcmp(magic, prefix, sizeof(magic)) == 0;
    ok = ok && mm_native_archive_input_open_utf8(&input, "", 0, MM_CHUNK_NAME_WIRE_BYTES, &options) ==
                   MM_ARCHIVE_INVALID_PAYLOAD && input.reader == reader &&
         mm_native_archive_input_open_utf8(&input, path, 0, MM_CHUNK_NAME_WIRE_BYTES, &options) == MM_ARCHIVE_READ_OK;
    if (ok)
    {
        access = mm_archive_reader_access(input.reader);
        ok = access.count == count && mm_archive_reader_read_stored(input.reader, &entry, prefix, sizeof(prefix));
    }
    if (ok)
        printf("%lu indexed chunks, first requested type at chunk 0x%08lx (%lu stored bytes)\n",
               (unsigned long)count, (unsigned long)entry.number, (unsigned long)entry.bytes);
    mm_native_archive_input_close(&input);
    return ok;
}

static int check_legacy_paths(void)
{
    char path[MM_STRING_MAX + 1], out[MM_STRING_MAX + 1];
    memset(path, 'a', sizeof(path));
    return mm_native_archive_legacy_path("", 0, 1, NULL, out) == MM_ARCHIVE_PATH_EMPTY &&
           mm_native_archive_legacy_path("a\0b", 3, 0, NULL, out) == MM_ARCHIVE_PATH_EMPTY &&
           mm_native_archive_legacy_path(path, sizeof(path), 0, NULL, out) == MM_ARCHIVE_PATH_TOO_LONG &&
           mm_native_archive_legacy_path(path, sizeof(path), 1, NULL, out) == MM_ARCHIVE_PATH_TOO_LONG &&
           mm_native_archive_legacy_path(path, MM_STRING_MAX, 1, NULL, out) == MM_ARCHIVE_PATH_OK &&
           !memcmp(out, path, MM_STRING_MAX) && !out[MM_STRING_MAX] &&
           mm_native_archive_legacy_path("caf\xc3\xa9", 5, 1, NULL, out) == MM_ARCHIVE_PATH_OK &&
           !strcmp(out, "caf\xe9") &&
           mm_native_archive_legacy_path("\xf0\x9f\x8e\xac", 4, 1, NULL, out) == MM_ARCHIVE_PATH_LOSSY &&
           mm_native_archive_legacy_path("a\xe2\x82", 3, 1, NULL, out) == MM_ARCHIVE_PATH_BAD_UTF8 &&
           mm_native_archive_legacy_path("a\xe2\x82", 3, 0, NULL, out) == MM_ARCHIVE_PATH_OK &&
           !strcmp(out, "a\xe2\x82");
}

static int check_archive_profiles(void)
{
    static const int encodings[] = {MM_TEXT_MAC, MM_TEXT_WINDOWS, MM_TEXT_UTF16_BE, MM_TEXT_UTF16_LE};
    static const unsigned char utf16[] = {'A', 0, 0, 0, 'Z', 0};
    unsigned char wire[24];
    unsigned short converted[3];
    size_t i;
    MMU32 written, size;
    for (i = 0; i < sizeof(encodings) / sizeof(*encodings); ++i)
    {
        const int encoding = encodings[i];
        const unsigned unit = i < 2 ? 1 : 2;
        const MMPrivateArchiveProfile profile = mm_native_archive_profile(encoding, MM_TEXT_UTF16_BE, 1, 1);
        MMChunkTitleIO codec = {0};
        MMMovieTitle title;
        codec.encoding = profile.encoding;
        codec.swapped_unicode_encoding = profile.swapped_unicode_encoding;
        written = 99;
        size = 2 + 4 * unit;
        memset(wire, 0xa5, sizeof(wire));
        if (profile.encoding != encoding || profile.unit != unit || !profile.expanded || !profile.trash_unused ||
            codec.swapped_unicode_encoding != MM_TEXT_UTF16_BE ||
            profile.legacy_name(NULL, "Az", wire, size - 1, &written) || written != 99 || wire[0] != 0xa5 ||
            !profile.legacy_name(NULL, "Az", wire, size, &written) || written != size ||
            !mm_movie_title_init(&title, unit) || !mm_movie_title_read_wire(&title, wire, written, &codec) ||
            title.count != 2 || (unit == 1 ? memcmp(title.text, "Az", 2) : title.text[0] != 'A' || title.text[1] != 'z') ||
            profile.convert(NULL, utf16, sizeof(utf16), MM_TEXT_UTF16_LE, converted, 0) != 3 ||
            profile.convert(NULL, utf16, sizeof(utf16), MM_TEXT_UTF16_LE, converted, 3) != 3)
            return 0;
        if (unit == 1 ? memcmp(converted, "A\0Z", 3) : converted[0] != 'A' || converted[1] || converted[2] != 'Z')
            return 0;
    }
    return 1;
}

static int check_temporary_archive(void)
{
    const MMU32 type = 0x54455354UL;
    const char payload[] = "snapshot chunk";
    MMPrivateArchiveProfile profile = mm_native_archive_profile(MM_TEXT_WINDOWS, MM_TEXT_UTF16_LE, 0, 0);
    MMPrivateArchive *archive = mm_native_private_archive_temporary(&profile);
    MMChunkOwnerRead *reader;
    MMArchiveWriter writer;
    MMChunkAccess chunks;
    MMChunkLocation cached;
    MMOwnedBlock block;
    MMBlockIO io;
    MMU32 number, position;
    char copy[sizeof(payload)];
    int ok;
    if (!archive)
        return 0;
    reader = mm_private_archive_reader(archive);
    writer = mm_private_archive_writer(archive);
    if (mm_owned_block_init(&block, reader->payloads.handles, reader->payloads.files) != MM_DATA_OK)
    {
        mm_private_archive_close(archive);
        return 0;
    }
    ok = mm_chunk_add_bytes(&writer.edit, writer.payloads->files, type, payload, sizeof(payload), &number) ==
         MM_CHUNK_OK;
    chunks = mm_chunk_owner_access(reader->storage);
    ok = ok && mm_chunk_find(&chunks, type, number, &position) == MM_CHUNK_OK &&
         mm_chunk_owner_cache(reader->storage, position, &cached) == MM_CHUNK_OK &&
         cached.file != reader->storage->owner->files.main_file &&
         mm_chunk_payload_bind_block(&reader->payloads, reader, type, number, &block) == MM_CHUNK_PAYLOAD_OK &&
         block.file == cached.file;
    mm_private_archive_close(archive);
    ok = ok && mm_owned_block_io(&block, &io) == MM_DATA_OK &&
         mm_block_read(&io, 0, copy, sizeof(copy), 0) == MM_DATA_OK && !memcmp(payload, copy, sizeof(copy));
    mm_owned_block_clear(&block);
    return ok;
}

static int equal_native_paths(void *context, void *a, void *b)
{
    (void)context;
    return mm_native_paths_equal(a, b);
}

static int check_named_stream(const MMNativePathChar *path, void *reference)
{
    MMNativeNamedFile file;
    MMFileRegistry registry;
    MMRegisteredFile record;
    MMReference references;
    MMFileOpsHost ops = {0};
    MMFileOwnerHost host;
    MMReadStream expected, stream;
    char header[MM_CHUNK_PREFIX_BYTES], copy[MM_CHUNK_PREFIX_BYTES];
    int ok;
    if (!mm_native_file_registry_init(&registry, NULL))
        return 0;
    if (!mm_native_named_file_init(&file, path, MM_FILE_DENY_WRITE, NULL, NULL, NULL))
    {
        mm_native_file_registry_dispose(&registry);
        return 0;
    }
    mm_reference_init(&references);
    mm_native_named_file_register(&file, &registry, &record, &file, (void *)path, &references);
    ops.registry = &registry;
    ops.equal_names = equal_native_paths;
    host = mm_native_named_file_host(&file);
    stream = mm_native_named_file_stream(&file);
    ok = mm_file_ops_find(&ops, (void *)path) == &record &&
         mm_native_file_read_stream(reference, &expected) && stream.length == expected.length &&
         mm_read_exact(&expected, 0, header, sizeof(header));
    mm_file_ops_shutdown(&ops);
    ok = ok && !file.state.open && mm_read_exact(&stream, 0, copy, sizeof(copy)) &&
         file.state.open && !memcmp(header, copy, sizeof(copy));
    mm_file_owner_close(&file.state, &host, 1);
    mm_file_registry_detach(&registry, &record);
    ok = ok && !mm_file_ops_find(&ops, (void *)path);
    mm_native_named_file_dispose(&file);
    mm_native_file_registry_dispose(&registry);
    return ok;
}

int main(int argc, char **argv)
{
    MMNativeOpenOptions options;
    SeekInput input = {0};
    MMU32 type = 0;
    void *file;
    int i, movie, seek, actors, auto_content, bodies, ok;

    if (!check_legacy_paths())
    {
        fputs("Legacy archive path conversion failed\n", stderr);
        return 1;
    }
    if (!check_archive_profiles())
    {
        fputs("Native archive text profile failed\n", stderr);
        return 1;
    }
    if (!check_temporary_archive())
    {
        fputs("Native temporary archive ownership failed\n", stderr);
        return 1;
    }

    movie = argc == 3 && strcmp(argv[2], "--movie") == 0;
    actors = argc >= 3 && strcmp(argv[2], "--actors") == 0;
    bodies = NO_BODY;
    if (argc >= 3 && strcmp(argv[2], "--body") == 0)
        bodies = BODY_PARTS;
    if (argc >= 3 && strcmp(argv[2], "--mesh") == 0)
        bodies = BODY_MESHES;
    if (argc >= 3 && strcmp(argv[2], "--frame-bodies") == 0)
        bodies = FRAME_BODIES;
    auto_content = bodies != NO_BODY || (argc >= 3 && strcmp(argv[2], "--seek") == 0);
    seek = actors || auto_content || (argc >= 3 && strcmp(argv[2], "--camera") == 0);
    if (seek ? !seek_input(&input, argc, argv, auto_content) : argc != 3 || (!movie && strlen(argv[2]) != 4))
    {
        fputs("Usage: engine-native-chunky-test FILE TYPE|--movie|(--camera|--actors) SCENE FRAME [SID CONTENT]\n",
              stderr);
        fputs("       engine-native-chunky-test FILE (--seek|--body|--mesh|--frame-bodies) SCENE FRAME RETAIL_CONTENT_DIRECTORY\n",
              stderr);
        return 1;
    }
    if (!movie && !seek)
        for (i = 0; i < 4; ++i)
            type = (type << 8) | (unsigned char)argv[2][i];
    mm_native_open_options_init(&options);
    file = mm_native_file_open(argv[1], &options);
    if (!file)
    {
        fprintf(stderr, "Cannot open %s\n", argv[1]);
        return 1;
    }
    if (!check_named_stream(argv[1], file))
    {
        fputs("Native named file stream failed\n", stderr);
        mm_native_file_close(file);
        return 1;
    }
    ok = check_index(file, argv[1], type, movie, seek ? &input : NULL, actors, auto_content, bodies);
    mm_native_file_close(file);
    if (!ok)
        fprintf(stderr, "Cannot read %s from %s\n", movie ? "movie metadata" : argv[2], argv[1]);
    return ok ? 0 : 1;
}
