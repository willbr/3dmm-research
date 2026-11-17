#include "mm/chunky_file.h"
#include "mm/movie_session.h"
#include "mm/native_files.h"
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

static int seek_input(SeekInput *input, int argc, char **argv)
{
    MMI32 scene;
    if ((argc != 5 && argc != 7) || !number(argv[3], &scene) || scene < 0 || !number(argv[4], &input->frame))
        return 0;
    input->scene = (MMU32)scene;
    if (argc == 7)
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

static int check_camera(const MMReadStream *stream, const MMChunkPolicy *policy, SeekInput *input)
{
    MMMovieSession *movie = NULL;
    MMMovieCamera camera;
    int status = mm_movie_session_open(stream, policy, &movie);
    unsigned i, j;
    if (status == MM_MOVIE_OK)
        status = mm_movie_session_camera(movie, input->scene, input->frame, open_source, input, &camera);
    mm_movie_session_close(movie);
    mm_native_file_close(input->file);
    if (status != MM_MOVIE_OK)
    {
        fprintf(stderr, "Cannot seek to camera (%d)\n", status);
        return 0;
    }
    printf("BKGD %ld %lu CAM %lu %lu %ld %ld %u\n", (long)camera.background.source,
           (unsigned long)camera.background.number, (unsigned long)camera.slot, (unsigned long)camera.number,
           (long)camera.data.near_z, (long)camera.data.far_z, (unsigned)camera.data.fov);
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 3; ++j)
            printf("%ld%c", (long)camera.data.matrix.m[i][j], i == 3 && j == 2 ? '\n' : ' ');
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

static int check_actors(const MMReadStream *stream, const MMChunkPolicy *policy, SeekInput *input)
{
    MMMovieSession *movie = NULL;
    MMMovieEvaluatedActor value;
    MMU32 ordinal = 0;
    unsigned i, j;
    int status = mm_movie_session_open(stream, policy, &movie);
    if (status == MM_MOVIE_OK)
        while ((status = mm_movie_session_actor(movie, input->scene, input->frame, ordinal,
                                                open_source, input, &value)) == MM_MOVIE_OK)
        {
            printf("ACTR %lu %ld %ld %lu %d %ld %ld %ld %ld %ld %ld\n",
                   (unsigned long)value.number, (long)value.header.actor_id, (long)value.header.source_id,
                   (unsigned long)value.header.template_number, value.state.onStage, (long)value.state.action,
                   (long)value.state.cel, (long)value.state.nextEvent, (long)value.state.position.dxr,
                   (long)value.state.position.dyr, (long)value.state.position.dzr);
            for (i = 0; i < 4; ++i)
                for (j = 0; j < 3; ++j)
                    printf("%ld ", (long)value.orientation.m[i][j]);
            printf("%ld %ld %ld\n", (long)value.translation.dxr, (long)value.translation.dyr,
                   (long)value.translation.dzr);
            ++ordinal;
        }
    mm_movie_session_close(movie);
    mm_native_file_close(input->file);
    if (status != MM_MOVIE_ACTOR_NOT_FOUND)
        fprintf(stderr, "Cannot evaluate actor %lu (%d)\n", (unsigned long)ordinal, status);
    return status == MM_MOVIE_ACTOR_NOT_FOUND;
}

static int check_index(void *file, MMU32 type, int movie, SeekInput *seek, int actors)
{
    const unsigned short native_order = 1;
    MMChunkPolicy policy = {0, 0, 516}; /* Legacy counted name plus terminator and encoding. */
    MMReadStream stream;
    MMChunkIndex *index = NULL;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMReadSlice payload;
    MMU32 position;
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
        return actors ? check_actors(&stream, &policy, seek) : check_camera(&stream, &policy, seek);
    if (mm_chunk_index_open(&stream, &policy, &index) != MM_CHUNK_OK)
        return 0;

    access = mm_chunk_index_access(index);
    ok = mm_chunk_type_at(&access, type, 0, &entry, &position) == MM_CHUNK_OK &&
         mm_read_slice_init(&payload, &stream, entry.offset, entry.bytes) &&
         mm_read_exact(&payload.stream, 0, prefix, sizeof(prefix)) &&
         mm_read_exact(&stream, 0, prefix, sizeof(prefix)) &&
         memcmp(magic, prefix, sizeof(magic)) == 0;
    if (ok)
        printf("%lu indexed chunks, first requested type at chunk 0x%08lx (%lu stored bytes)\n",
               (unsigned long)access.count, (unsigned long)entry.number, (unsigned long)entry.bytes);
    mm_chunk_index_close(index);
    return ok;
}

int main(int argc, char **argv)
{
    MMNativeOpenOptions options;
    SeekInput input = {0};
    MMU32 type = 0;
    void *file;
    int i, movie, seek, actors, ok;

    movie = argc == 3 && strcmp(argv[2], "--movie") == 0;
    actors = argc >= 3 && strcmp(argv[2], "--actors") == 0;
    seek = actors || (argc >= 3 && strcmp(argv[2], "--camera") == 0);
    if (seek ? !seek_input(&input, argc, argv) : argc != 3 || (!movie && strlen(argv[2]) != 4))
    {
        fputs("Usage: engine-native-chunky-test FILE TYPE|--movie|(--camera|--actors) SCENE FRAME [SID CONTENT]\n",
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
    ok = check_index(file, type, movie, seek ? &input : NULL, actors);
    mm_native_file_close(file);
    if (!ok)
        fprintf(stderr, "Cannot read %s from %s\n", movie ? "movie metadata" : argv[2], argv[1]);
    return ok ? 0 : 1;
}
