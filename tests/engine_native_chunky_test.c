#include "mm/chunky_file.h"
#include "mm/movie_session.h"
#include "mm/native_files.h"
#include <stdio.h>
#include <string.h>

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

static int check_index(void *file, MMU32 type, int movie)
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
    MMU32 type = 0;
    void *file;
    int i, movie, ok;

    movie = argc == 3 && strcmp(argv[2], "--movie") == 0;
    if (argc != 3 || (!movie && strlen(argv[2]) != 4))
    {
        fputs("Usage: engine-native-chunky-test FILE TYPE|--movie\n", stderr);
        return 1;
    }
    if (!movie)
        for (i = 0; i < 4; ++i)
            type = (type << 8) | (unsigned char)argv[2][i];
    mm_native_open_options_init(&options);
    file = mm_native_file_open(argv[1], &options);
    if (!file)
    {
        fprintf(stderr, "Cannot open %s\n", argv[1]);
        return 1;
    }
    ok = check_index(file, type, movie);
    mm_native_file_close(file);
    if (!ok)
        fprintf(stderr, "Cannot read %s from %s\n", movie ? "movie metadata" : argv[2], argv[1]);
    return ok ? 0 : 1;
}
