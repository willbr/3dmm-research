#include "mm/chunky_file.h"
#include "mm/codec.h"
#include "mm/data_stream.h"
#include "mm/movie_session.h"
#include "mm/movie_content_resolver.h"
#include "mm/scene_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int value, int line)
{
    if (!value)
    {
        fprintf(stderr, "C89 chunky check failed at line %d\n", line);
        exit(1);
    }
}
#define CHECK(value) check((value), __LINE__)
static void put16(unsigned char *bytes, unsigned int value, int big)
{
    bytes[big ? 1 : 0] = (unsigned char)value;
    bytes[big ? 0 : 1] = (unsigned char)(value >> 8);
}
static void put32(unsigned char *bytes, MMU32 value, int big)
{
    int i;
    for (i = 0; i < 4; ++i)
        bytes[big ? 3 - i : i] = (unsigned char)(value >> (8 * i));
}

static MMU32 store_record(unsigned char *out, const unsigned char *raw, MMU32 bytes, MMI32 codec)
{
    MMI32 written;
    if (!codec)
    {
        memcpy(out, raw, bytes);
        return bytes;
    }
    put32(out, (MMU32)codec, 1);
    put32(out + 4, bytes, 1);
    CHECK(mm_codec_encode(codec, raw, bytes, out + 8, 56, &written, NULL) == MM_DATA_OK);
    return (MMU32)written + 8;
}

static MMU32 movie_fixture(unsigned char bytes[1024], int big, MMI32 codec, int bad_version, int missing_scene)
{
    const MMU32 movie_type = 0x4d564945UL, scene_type = 0x5343454eUL;
    unsigned char movie[8] = {0}, scene[16] = {0};
    unsigned char *group, *row, *locations, *links;
    MMU32 root_size, scene_size, index_offset, end;
    const MMU32 root_row_bytes = 20 + 3 * 12, row_bytes = root_row_bytes + 20;

    memset(bytes, 0, 1024);
    put16(movie, 1, big);
    put16(movie + 2, 0x0303, big);
    put16(movie + 4, 2, big);
    put16(movie + 6, bad_version ? 3U : 2U, big);
    put16(scene, 1, big);
    put32(scene + 4, 25, big);
    put32(scene + 8, 0xfffffffaUL, big);
    put32(scene + 12, 4, big);
    root_size = store_record(bytes + 128, movie, sizeof(movie), codec);
    scene_size = store_record(bytes + 128 + root_size, scene, sizeof(scene), codec);
    index_offset = 128 + root_size + scene_size;
    end = index_offset + 20 + row_bytes + 16;
    memcpy(bytes, "CHN2", 4);
    put16(bytes + 8, 5, big);
    put16(bytes + 10, 4, big);
    put16(bytes + 12, 1, big);
    put32(bytes + 16, end, big);
    put32(bytes + 20, index_offset, big);
    put32(bytes + 24, end - index_offset, big);
    put32(bytes + 28, end, big);
    group = bytes + index_offset;
    put16(group, 1, big);
    put32(group + 4, 2, big);
    put32(group + 8, row_bytes, big);
    put32(group + 12, 0xffffffffUL, big);
    put32(group + 16, 20, big);
    row = group + 20;
    put32(row, movie_type, big);
    put32(row + 4, 17, big);
    put32(row + 8, 128, big);
    put32(row + 12, (root_size << 8) | MM_CHUNK_LONER | (codec ? MM_CHUNK_PACKED : 0), big);
    put16(row + 16, 3, big);
    links = row + 20;
    put32(links, scene_type, big);
    put32(links + 4, missing_scene ? 37UL : 36UL, big);
    put32(links + 12, 0x5a5a5a5aUL, big); /* Different type at slot 1. */
    put32(links + 20, 1, big);
    put32(links + 24, scene_type, big);
    put32(links + 28, 36, big);
    put32(links + 32, 3, big);
    row += root_row_bytes;
    put32(row, scene_type, big);
    put32(row + 4, 36, big);
    put32(row + 8, 128 + root_size, big);
    put32(row + 12, (scene_size << 8) | (codec ? MM_CHUNK_PACKED : 0), big);
    put16(row + 18, 2, big);
    locations = row + 20;
    put32(locations + 4, root_row_bytes, big);
    put32(locations + 8, root_row_bytes, big);
    put32(locations + 12, 20, big);
    return end;
}

static void movie_sessions(void)
{
    const MMChunkPolicy policy = {0, 0, 516};
    const MMI32 codecs[] = {0, MM_CODEC_KCDC, MM_CODEC_KCD2};
    unsigned char bytes[1024];
    MMMemoryStream input;
    MMMovieSession *movie = NULL;
    const MMMovieSessionRoot *root;
    MMMovieSessionScene scene;
    MMU32 end;
    unsigned i;
    int big, missing;
    for (i = 0; i < sizeof(codecs) / sizeof(codecs[0]); ++i)
        for (big = 0; big <= 1; ++big)
            for (missing = 0; missing <= 1; ++missing)
            {
                end = movie_fixture(bytes, big, codecs[i], 0, missing);
                CHECK(mm_memory_stream_init(&input, bytes, end));
                CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_OK);
                root = mm_movie_session_root(movie);
                CHECK(root->number == 17 && root->header.current_version == 2 && root->header.big_endian == big);
                scene.number = 123;
                CHECK(mm_movie_session_scene(movie, 1, &scene) == MM_MOVIE_SCENE_NOT_FOUND && scene.number == 123);
                CHECK(mm_movie_session_scene(movie, 2, &scene) == MM_MOVIE_SCENE_NOT_FOUND && scene.number == 123);
                if (missing)
                    CHECK(mm_movie_session_scene(movie, 0, &scene) == MM_MOVIE_BAD_FILE && scene.number == 123);
                else
                    CHECK(mm_movie_session_scene(movie, 0, &scene) == MM_MOVIE_OK && scene.number == 36);
                CHECK(mm_movie_session_scene(movie, 3, &scene) == MM_MOVIE_OK && scene.number == 36 &&
                      scene.first_frame == -6 && scene.last_frame == 25 && scene.transition == 4);
                mm_movie_session_close(movie);
            }
    movie = NULL;
    end = movie_fixture(bytes, 0, MM_CODEC_KCD2, 1, 0);
    CHECK(mm_memory_stream_init(&input, bytes, end));
    CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_BAD_VERSION && !movie);
    bytes[128] = 'X'; /* Unknown packed format. */
    CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_BAD_FILE && !movie);
    CHECK(mm_memory_stream_init(&input, bytes, end - 1));
    CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_BAD_FILE && !movie);
}

static void camera_events(unsigned char *group, int big, int start, MMU32 source, int bad_row)
{
    MMU32 count = start ? 1 : 3, row_size = start ? 24 : 12, i;
    unsigned char *row = group + 20;
    unsigned char *locations = row + count * row_size;
    put16(group, 1, big);
    put32(group + 4, count, big);
    put32(group + 8, count * row_size, big);
    put32(group + 12, 0xffffffffUL, big);
    put32(group + 16, 8, big);
    for (i = 0; i < count; ++i)
    {
        put32(locations + i * 8, bad_row ? 0xffffffffUL : i * row_size, big);
        put32(locations + i * 8 + 4, row_size, big);
        put32(row, i == 2 ? 8 : 5, big);
        put32(row + 4, start ? MM_SCENE_SET_BACKGROUND : MM_SCENE_CHANGE_CAMERA, big);
        put32(row + 8, start ? source : i == 1, big);
        if (start)
        {
            put32(row + 16, 0x424b4744UL, big);
            put32(row + 20, 4, big);
        }
        row += row_size;
    }
}

enum { FIXTURE_RECORD_BYTES = 256, FIXTURE_MAX_RECORDS = 24 };

static MMU32 write_fixture(unsigned char *out, size_t capacity, int big, MMU32 count, const MMU32 *types, const MMU32 *numbers,
                           const MMU32 *lengths, const MMI32 *codecs, unsigned char raw[][FIXTURE_RECORD_BYTES],
                           const MMU32 links[][3], MMU32 link_count)
{
    unsigned char *group, *row, *locations, *link;
    MMU32 offsets[FIXTURE_MAX_RECORDS], sizes[FIXTURE_MAX_RECORDS];
    MMU32 parents[FIXTURE_MAX_RECORDS] = {0}, children[FIXTURE_MAX_RECORDS] = {0};
    MMU32 i, j, end, index_offset = 128;
    const MMU32 row_bytes = count * 20 + link_count * 12;
    CHECK(count <= sizeof(offsets) / sizeof(offsets[0]));
    for (i = 0; i < link_count; ++i)
    {
        ++children[links[i][0]];
        ++parents[links[i][1]];
    }
    memset(out, 0, capacity);
    for (i = 0; i < count; ++i)
    {
        offsets[i] = index_offset;
        sizes[i] = store_record(out + index_offset, raw[i], lengths[i], codecs[i]);
        index_offset += sizes[i];
    }
    end = index_offset + 20 + row_bytes + count * 8;
    CHECK(end <= capacity);
    memcpy(out, "CHN2", 4);
    put16(out + 8, 5, big);
    put16(out + 10, 4, big);
    put16(out + 12, 1, big);
    put32(out + 16, end, big);
    put32(out + 20, index_offset, big);
    put32(out + 24, end - index_offset, big);
    put32(out + 28, end, big);
    group = out + index_offset;
    put16(group, 1, big);
    put32(group + 4, count, big);
    put32(group + 8, row_bytes, big);
    put32(group + 12, 0xffffffffUL, big);
    put32(group + 16, 20, big);
    row = group + 20;
    locations = row + row_bytes;
    for (i = 0; i < count; ++i)
    {
        put32(locations + i * 8, (MMU32)(row - (group + 20)), big);
        put32(locations + i * 8 + 4, 20 + 12 * children[i], big);
        put32(row, types[i], big);
        put32(row + 4, numbers[i], big);
        put32(row + 8, offsets[i], big);
        put32(row + 12, (sizes[i] << 8) | (!parents[i] ? MM_CHUNK_LONER : 0) |
                         (codecs[i] ? MM_CHUNK_PACKED : 0), big);
        put16(row + 16, children[i], big);
        put16(row + 18, parents[i], big);
        link = row + 20;
        for (j = 0; j < link_count; ++j)
            if (links[j][0] == i)
            {
                put32(link, types[links[j][1]], big);
                put32(link + 4, numbers[links[j][1]], big);
                put32(link + 8, links[j][2], big);
                link += 12;
            }
        row = link;
    }
    return end;
}

static MMU32 camera_fixture(unsigned char *out, int big, MMI32 codec, MMU32 source, int bad_row)
{
    enum { BKGD, CAM1, CAM0, FRAMES, STARTS, ROOT, SCENE, COUNT };
    static const MMU32 types[] = {0x424b4744UL, 0x43414d20UL, 0x43414d20UL, 0x47474652UL, 0x47475354UL,
                                  0x4d564945UL, 0x5343454eUL};
    static const MMU32 numbers[] = {4, 80, 90, 13, 14, 17, 36};
    static const MMU32 links[][3] = {{BKGD, CAM0, 0}, {BKGD, CAM1, 1}, {ROOT, SCENE, 0},
                                    {SCENE, FRAMES, 0}, {SCENE, STARTS, 1}};
    static const MMU32 lengths[] = {8, 76, 76, 80, 52, 8, 16};
    unsigned char raw[COUNT][FIXTURE_RECORD_BYTES];
    MMI32 codecs[COUNT] = {0};
    MMU32 i;

    memset(raw, 0, sizeof(raw));
    for (i = 0; i < COUNT; ++i)
        put16(raw[i], 1, big);
    put32(raw[CAM0] + 4, 123, big);
    put32(raw[CAM1] + 4, 456, big);
    put32(raw[CAM1] + 72, 0xfffffffeUL, big);
    camera_events(raw[FRAMES], big, 0, 0, bad_row);
    camera_events(raw[STARTS], big, 1, source, 0);
    put16(raw[ROOT] + 4, 2, big);
    put16(raw[ROOT] + 6, 2, big);
    put32(raw[SCENE] + 4, 10, big);
    put32(raw[SCENE] + 8, 1, big);
    codecs[FRAMES] = codecs[STARTS] = codec;
    return write_fixture(out, 1024, big, COUNT, types, numbers, lengths, codecs, raw, links,
                         sizeof(links) / sizeof(links[0]));
}

static MMU32 source_movie(unsigned char *out, int big, int wide)
{
    static const MMU32 types[] = {0x47535420UL, 0x4d564945UL};
    static const MMU32 numbers[] = {5, 17};
    static const MMU32 links[][3] = {{1, 0, 1}};
    static const unsigned char title[] = "D\xe9" "cor/3DMOVIE";
    const MMI32 codecs[] = {0, 0};
    unsigned char raw[2][FIXTURE_RECORD_BYTES], *table;
    MMU32 lengths[2], i, unit = wide ? 2 : 1, name_bytes = (sizeof(title) + 1) * unit;
    memset(raw, 0, sizeof(raw));
    put16(raw[0], 1, big);
    put16(raw[0] + 2, wide ? 0x0505 : 0x0303, big);
    put32(raw[0] + 4, 8, big);
    put32(raw[0] + 8, 2, big);
    put32(raw[0] + 12, name_bytes, big);
    put32(raw[0] + 16, 0xffffffffUL, big);
    raw[0][20] = sizeof(title) - 1;
    for (i = 0; i < sizeof(title) - 1; ++i)
        raw[0][20 + (i + 1) * unit] = title[i];
    raw[0][20 + sizeof(title) * unit] = 0; /* Second row's empty name. */
    table = raw[0] + 20 + name_bytes;
    put32(table + 4, 9, big);
    put32(table + 8, sizeof(title) * unit, big);
    put32(table + 12, 9, big);
    lengths[0] = 20 + name_bytes + 16;
    lengths[1] = 8;
    put16(raw[1], 1, big);
    put16(raw[1] + 4, 2, big);
    put16(raw[1] + 6, 2, big);
    return write_fixture(out, 1024, big, 2, types, numbers, lengths, codecs, raw, links, 1);
}

static void group_header(unsigned char *bytes, MMU32 count, MMU32 data, MMU32 width, int big)
{
    put32(bytes + 4, count, big);
    put32(bytes + 8, data, big);
    put32(bytes + 12, 0xffffffffUL, big);
    put32(bytes + 16, width, big);
}

static int view_record(void *context, MMU32 position, const MMChunkEntry *entry, void *bytes, MMU32 size)
{
    (void)position;
    if (size != entry->bytes)
        return MM_MOVIE_BAD_FILE;
    return mm_read_exact((MMReadStream *)context, entry->offset, bytes, size) ? MM_MOVIE_OK : MM_MOVIE_IO;
}

static void graph_access(void *context, MMChunkAccess *out)
{
    *out = ((const MMMovieChunkView *)context)->chunks;
}

static int graph_record(void *context, void *request, MMU32 position, const MMChunkEntry *entry,
                        void *bytes, MMU32 size)
{
    const MMMovieChunkView *view = (const MMMovieChunkView *)context;
    if (request)
        ++*(int *)request;
    return view->read(view->context, position, entry, bytes, size);
}

static void movie_directory(void)
{
    enum { OTHER, MVIE_A, MVIE_B, SCENE_A, SCENE_B, SCENE_C, COUNT };
    const MMU32 types[] = {0x47535420UL, 0x4d564945UL, 0x4d564945UL, 0x5343454eUL, 0x5343454eUL, 0x5343454eUL};
    const MMU32 numbers[] = {0, 2, 7, 14, 15, 16};
    const MMU32 lengths[] = {4, 8, 8, 16, 16, 16};
    const MMI32 codecs[COUNT] = {0};
    const MMU32 links[][3] = {{MVIE_A, OTHER, 0}, {MVIE_A, SCENE_B, 2}, {MVIE_A, SCENE_A, 5}, {MVIE_B, SCENE_C, 0}};
    const MMU32 shifted_links[][3] = {{MVIE_A - 1, SCENE_B - 1, 2}, {MVIE_A - 1, SCENE_A - 1, 5},
                                     {MVIE_B - 1, SCENE_C - 1, 0}};
    const MMChunkPolicy policy = {0, 0, 516};
    unsigned char raw[COUNT][FIXTURE_RECORD_BYTES], bytes[1024];
    MMMemoryStream memory;
    MMChunkIndex *index;
    MMMovieChunkView view;
    MMMovieSessionGraph graph;
    MMMovieSession *session = NULL, *original;
    MMMovieHeader header;
    MMMovieSessionScene scene;
    MMU32 size, cursor, slot, root, number, i;
    int reads = 0;
    memset(raw, 0, sizeof(raw));
    for (i = 0; i < COUNT; ++i)
        put16(raw[i], 1, 1);
    put16(raw[MVIE_A] + 4, 3, 1);
    put16(raw[MVIE_A] + 6, 2, 1);
    put16(raw[MVIE_B] + 4, 2, 1);
    put16(raw[MVIE_B] + 6, 2, 1);
    for (i = SCENE_A; i <= SCENE_C; ++i)
    {
        put32(raw[i] + 4, 27 + i, 1);
        put32(raw[i] + 8, i, 1);
        put32(raw[i] + 12, 1, 1);
    }
    size = write_fixture(bytes, sizeof(bytes), 1, COUNT, types, numbers, lengths, codecs, raw, links,
                         sizeof(links) / sizeof(links[0]));
    CHECK(mm_memory_stream_init(&memory, bytes, size));
    CHECK(mm_chunk_index_open(&memory.stream, &policy, &index) == MM_CHUNK_OK);
    view.chunks = mm_chunk_index_access(index);
    view.context = &memory.stream;
    view.read = view_record;
    root = 0xffffffffUL;
    CHECK(mm_movie_view_root(&view, &root, &header) == MM_MOVIE_OK && root == 2 && header.current_version == 3);
    CHECK(mm_movie_view_scene_number(&view.chunks, root, 0, &number) == MM_MOVIE_SCENE_NOT_FOUND);
    CHECK(mm_movie_view_scene_number(&view.chunks, root, 2, &number) == MM_MOVIE_OK && number == 15);
    CHECK(mm_movie_view_scene_number(&view.chunks, root, 3, &number) == MM_MOVIE_SCENE_NOT_FOUND && number == 15);
    cursor = 0;
    CHECK(mm_movie_view_next_scene(&view, root, &cursor, &scene, &slot) == MM_MOVIE_OK &&
          scene.number == 15 && slot == 2 && scene.first_frame == SCENE_B && scene.last_frame == 27 + SCENE_B);
    CHECK(mm_movie_view_next_scene(&view, root, &cursor, &scene, &slot) == MM_MOVIE_OK &&
          scene.number == 14 && slot == 5 && scene.transition == 1);
    CHECK(mm_movie_view_next_scene(&view, root, &cursor, &scene, &slot) == MM_MOVIE_SCENE_NOT_FOUND &&
          scene.number == 14 && slot == 5);
    root = 7;
    CHECK(mm_movie_view_root(&view, &root, &header) == MM_MOVIE_OK && header.current_version == 2);
    CHECK(mm_movie_view_scene_number(&view.chunks, root, 0, &number) == MM_MOVIE_OK && number == 16);
    cursor = 0;
    memory.stream.length = 1;
    CHECK(mm_movie_view_next_scene(&view, root, &cursor, &scene, &slot) == MM_MOVIE_IO &&
          !cursor && scene.number == 14 && slot == 5);
    root = 0xffffffffUL;
    CHECK(mm_movie_view_root(&view, &root, &header) == MM_MOVIE_IO && root == 2 && header.current_version == 2);
    root = 3;
    CHECK(mm_movie_view_root(&view, &root, &header) == MM_MOVIE_ROOT_NOT_FOUND && root == 3);

    graph.context = &view;
    graph.access = graph_access;
    graph.read = graph_record;
    memory.stream.length = size;
    root = 7;
    CHECK(mm_movie_session_open_graph(&graph, &reads, &root, &session) == MM_MOVIE_OK && reads == 1);
    CHECK(mm_movie_session_root(session)->number == 7 && mm_movie_session_root(session)->header.current_version == 2);
    mm_chunk_index_close(index);
    size = write_fixture(bytes, sizeof(bytes), 1, COUNT - 1, types + 1, numbers + 1, lengths + 1,
                         codecs + 1, raw + 1, shifted_links, sizeof(shifted_links) / sizeof(shifted_links[0]));
    CHECK(mm_memory_stream_init(&memory, bytes, size));
    CHECK(mm_chunk_index_open(&memory.stream, &policy, &index) == MM_CHUNK_OK);
    view.chunks = mm_chunk_index_access(index);
    CHECK(mm_movie_session_scene_number(session, 0, &number) == MM_MOVIE_OK && number == 16 && reads == 1);
    CHECK(mm_movie_session_scene(session, 0, &scene) == MM_MOVIE_OK && scene.number == 16);
    cursor = 0;
    CHECK(mm_movie_session_next_scene(session, &reads, &cursor, &scene, &slot) == MM_MOVIE_OK &&
          reads == 2 && scene.number == 16 && slot == 0);
    original = session;
    memory.stream.length = 1;
    root = 0xffffffffUL;
    CHECK(mm_movie_session_open_graph(&graph, &reads, &root, &session) == MM_MOVIE_IO &&
          root == 2 && session == original && reads == 3 && mm_movie_session_root(session)->number == 7);
    mm_movie_session_close(session);
    mm_chunk_index_close(index);
}

enum
{
    ACTOR_FIXTURE_OK, ACTOR_FIXTURE_MISSING_PAINT, ACTOR_FIXTURE_BAD_MATRIX,
    ACTOR_FIXTURE_CUSTOM_DEFAULT, ACTOR_FIXTURE_MISSING_DEFAULT
};

static MMU32 actor_fixture(unsigned char *out, int big, MMU32 source, int issue)
{
    enum { ACTN, ACTOR, MODEL, HAT, DEFAULT, CUSTOM, EVENTS, CELS, DEFAULTS, STARTS, SETS, PARENTS, MATRICES,
           MAT_DEFAULT, MAT_CUSTOM, MAT_PAINT, ROOT, PATH, SCENE, TMPL, COUNT };
    static const MMU32 types[] = {0x4143544eUL, 0x41435452UL, 0x424d444cUL, 0x424d444cUL, 0x434d544cUL,
                                  0x434d544cUL, 0x47474145UL, 0x4747434cUL, 0x4747434dUL, 0x47475354UL,
                                  0x474c4253UL, 0x474c5049UL, 0x474c5846UL, 0x4d54524cUL, 0x4d54524cUL,
                                  0x4d54524cUL, 0x4d564945UL, 0x50415448UL, 0x5343454eUL, 0x544d504cUL};
    static const MMU32 numbers[] = {3, 12, 70, 71, 80, 81, 4, 5, 15, 6, 1, 2, 3, 90, 91, 92, 17, 7, 36, 13};
    static const MMU32 lengths[] = {8, 44, 112, 112, 8, 8, 248, 60, 36, 40, 14, 14, 108, 20, 20, 20, 8, 44, 16, 16};
    static const MMI32 codecs[COUNT] = {0};
    MMU32 links[][3] = {{ACTN, CELS, 0}, {ACTN, MATRICES, 0}, {ACTOR, EVENTS, 0},
                                    {ACTOR, PATH, 0}, {ACTOR, TMPL, 0}, {DEFAULT, MAT_DEFAULT, 0},
                                    {CUSTOM, HAT, 0}, {CUSTOM, MAT_CUSTOM, 0}, {ROOT, SCENE, 0}, {SCENE, STARTS, 1},
                                    {SCENE, ACTOR, 5}, {TMPL, ACTN, 0}, {TMPL, MODEL, 0}, {TMPL, DEFAULT, 0},
                                    {TMPL, DEFAULTS, 0}, {TMPL, SETS, 0}, {TMPL, PARENTS, 0}, {TMPL, CUSTOM, 1}};
    unsigned char raw[COUNT][FIXTURE_RECORD_BYTES];
    unsigned char *row;
    MMU32 i, link_count = sizeof(links) / sizeof(links[0]);
    memset(raw, 0, sizeof(raw));
    for (i = 0; i < COUNT; ++i)
        put16(raw[i], 1, big);
    put32(raw[ACTN] + 4, 2, big);
    put32(raw[ACTOR] + 4, 65536, big);
    put32(raw[ACTOR] + 16, 8, big);
    put32(raw[ACTOR] + 20, 1, big);
    put32(raw[ACTOR] + 24, 10, big);
    put32(raw[ACTOR] + 28, source, big);
    put32(raw[ACTOR] + 36, types[TMPL], big);
    put32(raw[ACTOR] + 40, numbers[TMPL], big);
    put16(raw[MODEL] + 4, 1, big);
    put16(raw[MODEL] + 6, 1, big);
    raw[MODEL][69] = 42;
    memcpy(raw[HAT], raw[MODEL], lengths[MODEL]);
    raw[HAT][69] = 73;
    group_header(raw[EVENTS], 5, 188, MM_ACTOR_EVENT_BYTES, big);
    row = raw[EVENTS] + 20;
    put32(row + 4, 1, big);
    put32(row + 40, MM_ACTOR_ACTION, big);
    put32(row + 68, MM_ACTOR_STEP, big);
    put32(row + 88, 0xffff0000UL, big);
    put32(row + 92, MM_ACTOR_COSTUME, big);
    put32(row + 96, 2, big);
    put32(row + 104, 1, big);
    put32(row + 116, 1, big);
    put32(row + 120, 1, big);
    put32(row + 140, MM_ACTOR_COSTUME, big);
    put32(row + 144, 3, big);
    put32(row + 152, 90000, big);
    put32(row + 180, types[MAT_PAINT], big);
    put32(row + 184, numbers[MAT_PAINT] + (issue == ACTOR_FIXTURE_MISSING_PAINT), big);
    put32(row + 192, 40, big);
    put32(row + 196, 40, big);
    put32(row + 200, 28, big);
    put32(row + 204, 68, big);
    put32(row + 208, 24, big);
    put32(row + 212, 92, big);
    put32(row + 216, 48, big);
    put32(row + 220, 140, big);
    put32(row + 224, 48, big);
    group_header(raw[DEFAULTS], 1, 8, 4, big);
    put32(raw[DEFAULTS] + 20, 1, big);
    if (issue == ACTOR_FIXTURE_CUSTOM_DEFAULT)
        put32(raw[DEFAULTS] + 24, 1, big);
    else if (issue == ACTOR_FIXTURE_MISSING_DEFAULT)
        put32(raw[DEFAULTS] + 24, 2, big);
    put32(raw[DEFAULTS] + 32, 8, big);
    group_header(raw[CELS], 2, 24, 8, big);
    put32(raw[CELS] + 24, 65536, big);
    put32(raw[CELS] + 36, 131072, big);
    put16(raw[CELS] + 40, 0xffffU, big); /* Second cel retains the first model. */
    put16(raw[CELS] + 42, issue == ACTOR_FIXTURE_BAD_MATRIX ? 2 : 1, big);
    put32(raw[CELS] + 48, 12, big);
    put32(raw[CELS] + 52, 12, big);
    put32(raw[CELS] + 56, 12, big);
    put32(raw[SETS] + 4, 2, big);
    put32(raw[SETS] + 8, 1, big);
    put32(raw[PARENTS] + 4, 2, big);
    put32(raw[PARENTS] + 8, 1, big);
    put16(raw[PARENTS] + 12, 0xffffU, big);
    put32(raw[MATRICES] + 4, 48, big);
    put32(raw[MATRICES] + 8, 2, big);
    for (i = 0; i < 3; ++i)
    {
        put32(raw[MATRICES] + 12 + i * 16, 65536, big);
        put32(raw[MATRICES] + 60 + i * 16, 65536, big);
    }
    put32(raw[MATRICES] + 48, 123, big);
    put32(raw[MATRICES] + 96, 456, big);
    group_header(raw[STARTS], 1, 12, 8, big);
    put32(raw[STARTS] + 28, 5, big);
    put32(raw[STARTS] + 36, 12, big);
    put16(raw[ROOT] + 4, 2, big);
    put16(raw[ROOT] + 6, 2, big);
    put32(raw[PATH] + 4, MM_ACTOR_ROUTE_BYTES, big);
    put32(raw[PATH] + 8, 2, big);
    put32(raw[PATH] + 24, 655360, big);
    put32(raw[PATH] + 28, 655360, big);
    put32(raw[SCENE] + 4, 10, big);
    put32(raw[SCENE] + 8, 1, big);
    if (big)
    {
        /* Exercise the template reader for archives without a costume group. */
        for (i = 0; i < link_count && links[i][1] != DEFAULTS; ++i)
            ;
        CHECK(i < link_count);
        --link_count;
        memmove(links + i, links + i + 1, (link_count - i) * sizeof(links[0]));
    }
    return write_fixture(out, 2048, big, COUNT, types, numbers, lengths, codecs, raw,
                         (const MMU32 (*)[3])links, link_count);
}

static void movie_meshes(const MMMovieSession *movie, const MMMovieEvaluatedBody *body)
{
    MMMovieBodyMeshes *geometry = NULL, *previous;
    MMMovieEvaluatedBody extended = *body;
    MMMovieBodyPart parts[2];
    CHECK(mm_movie_session_body_meshes(movie, body, NULL, NULL, &geometry) == MM_MOVIE_OK);
    CHECK(geometry->part_count == 1 && geometry->model_count == 1 && geometry->parts[0].model_index == 0);
    CHECK(geometry->models[0].description.vertices == 1 && geometry->models[0].description.faces == 1);
    CHECK(geometry->models[0].vertices[0].red == 42 && geometry->models[0].faces[0].vertices[2] == 0);
    mm_movie_body_meshes_dispose(geometry);
    extended.parts = parts;
    extended.part_count = 2;
    mm_matrix_identity(&extended.actor.orientation);
    extended.actor.orientation.m[0][0] = extended.actor.orientation.m[1][1] = 0;
    extended.actor.orientation.m[0][1] = 65536;
    extended.actor.orientation.m[1][0] = -65536;
    extended.actor.orientation.m[3][0] = 9;
    extended.actor.translation.dxr = 65536;
    extended.actor.translation.dyr = 200;
    parts[0] = body->parts[0];
    parts[1] = parts[0];
    parts[0].parent = 1;
    CHECK(mm_movie_session_body_meshes(movie, &extended, NULL, NULL, &geometry) == MM_MOVIE_OK);
    CHECK(geometry->model_count == 1 && geometry->parts[0].model_index == geometry->parts[1].model_index);
    CHECK(geometry->parts[0].world.m[3][0] == 65536 + 9 && geometry->parts[0].world.m[3][1] == 200 + 246 &&
          geometry->parts[1].world.m[3][1] == 200 + 123);
    previous = geometry;
    parts[1].parent = 0;
    CHECK(mm_movie_session_body_meshes(movie, &extended, NULL, NULL, &geometry) == MM_MOVIE_BAD_FILE);
    CHECK(geometry == previous);
    mm_movie_body_meshes_dispose(geometry);
}

static void movie_actors(void)
{
    const MMChunkPolicy policy = {0, 0, 516};
    unsigned char bytes[2048];
    MMMemoryStream input;
    MMMovieSession *movie;
    MMMovieEvaluatedActor actor;
    MMMovieEvaluatedBody *body = NULL, *previous;
    MMMovieBodyMeshes *geometry = NULL;
    MMU32 size;
    int big;
    for (big = 0; big <= 1; ++big)
    {
        size = actor_fixture(bytes, big, 0, ACTOR_FIXTURE_OK);
        CHECK(mm_memory_stream_init(&input, bytes, size));
        CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_OK);
        CHECK(mm_movie_session_actor(movie, 0, 3, 0, NULL, NULL, &actor) == MM_MOVIE_OK);
        CHECK(actor.header.actor_id == 8 && actor.number == 12 && actor.state.onStage && actor.state.action == 0);
        CHECK(actor.state.cel == 2 && actor.state.nextEvent == 5 && actor.state.position.dxr == 196600);
        CHECK(mm_movie_session_actor(movie, 0, 1, 0, NULL, NULL, &actor) == MM_MOVIE_OK);
        CHECK(actor.state.cel == 0 && actor.translation.dxr == 65536);
        CHECK(mm_movie_session_body(movie, 0, 1, 0, NULL, NULL, &body) == MM_MOVIE_OK);
        CHECK(body->part_count == 1 && body->parts[0].model.number == 70 && body->parts[0].parent == -1 &&
              body->parts[0].set == 0 && body->parts[0].matrix.m[3][0] == 123);
        CHECK(body->parts[0].active_model.number == 70 && body->parts[0].material.number == 90);
        CHECK(body->model_owner.type == 0x544d504cUL && body->model_owner.number == 13);
        movie_meshes(movie, body);
        mm_movie_body_dispose(body);
        CHECK(mm_movie_session_body(movie, 0, 2, 0, NULL, NULL, &body) == MM_MOVIE_OK);
        CHECK(body->part_count == 1 && !body->parts[0].model.type && body->parts[0].matrix.m[3][0] == 456);
        CHECK(mm_movie_session_body(movie, 0, 1, 1, NULL, NULL, &body) == MM_MOVIE_ACTOR_NOT_FOUND);
        CHECK(body->parts[0].matrix.m[3][0] == 456);
        CHECK(mm_movie_session_body_meshes(movie, body, NULL, NULL, &geometry) == MM_MOVIE_OK);
        CHECK(body->parts[0].active_model.number == 71 && body->parts[0].material.number == 91 &&
              body->parts[0].material_owner.type == 0x544d504cUL);
        CHECK(geometry->part_count == 1 && geometry->model_count == 1 && geometry->parts[0].model_index == 0 &&
              geometry->models[0].vertices[0].red == 73);
        mm_movie_body_meshes_dispose(geometry);
        mm_movie_body_dispose(body);
        CHECK(mm_movie_session_body(movie, 0, 3, 0, NULL, NULL, &body) == MM_MOVIE_OK);
        CHECK(body->parts[0].active_model.number == 70 && body->parts[0].material.number == 92 &&
              body->parts[0].material_owner.number == 92);
        mm_movie_body_dispose(body);
        CHECK(mm_movie_session_actor(movie, 0, 1, 1, NULL, NULL, &actor) == MM_MOVIE_ACTOR_NOT_FOUND &&
              actor.number == 12);
        CHECK(mm_movie_session_actor(movie, 0, 11, 0, NULL, NULL, &actor) == MM_MOVIE_FRAME_OUT_OF_RANGE);
        mm_movie_session_close(movie);
    }
    size = actor_fixture(bytes, 0, 2, ACTOR_FIXTURE_OK);
    CHECK(mm_memory_stream_init(&input, bytes, size));
    CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_OK);
    CHECK(mm_movie_session_actor(movie, 0, 1, 0, NULL, NULL, &actor) == MM_MOVIE_ASSET_NOT_FOUND);
    mm_movie_session_close(movie);
    size = actor_fixture(bytes, 0, 0, ACTOR_FIXTURE_MISSING_PAINT);
    CHECK(mm_memory_stream_init(&input, bytes, size));
    CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_OK);
    CHECK(mm_movie_session_body(movie, 0, 1, 0, NULL, NULL, &body) == MM_MOVIE_OK);
    previous = body;
    CHECK(mm_movie_session_body(movie, 0, 3, 0, NULL, NULL, &body) == MM_MOVIE_ASSET_NOT_FOUND && body == previous);
    CHECK(mm_movie_session_actor(movie, 0, 3, 0, NULL, NULL, &actor) == MM_MOVIE_OK);
    mm_movie_body_dispose(body);
    mm_movie_session_close(movie);
    size = actor_fixture(bytes, 0, 0, ACTOR_FIXTURE_CUSTOM_DEFAULT);
    CHECK(mm_memory_stream_init(&input, bytes, size));
    CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_OK);
    CHECK(mm_movie_session_body(movie, 0, 1, 0, NULL, NULL, &body) == MM_MOVIE_OK);
    CHECK(body->parts[0].material.number == 91);
    previous = body;
    mm_movie_session_close(movie);

    size = actor_fixture(bytes, 0, 0, ACTOR_FIXTURE_MISSING_DEFAULT);
    CHECK(mm_memory_stream_init(&input, bytes, size));
    CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_OK);
    CHECK(mm_movie_session_body(movie, 0, 1, 0, NULL, NULL, &body) == MM_MOVIE_ASSET_NOT_FOUND && body == previous);
    CHECK(body->parts[0].material.number == 91);
    mm_movie_body_dispose(body);
    mm_movie_session_close(movie);
    size = actor_fixture(bytes, 0, 0, ACTOR_FIXTURE_BAD_MATRIX);
    CHECK(mm_memory_stream_init(&input, bytes, size));
    CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_OK);
    CHECK(mm_movie_session_body(movie, 0, 1, 0, NULL, NULL, &body) == MM_MOVIE_OK);
    previous = body;
    CHECK(mm_movie_session_body(movie, 0, 2, 0, NULL, NULL, &body) == MM_MOVIE_BAD_FILE && body == previous);
    CHECK(body->parts[0].active_model.number == 70 && body->parts[0].matrix.m[3][0] == 123);
    mm_movie_body_dispose(body);
    mm_movie_session_close(movie);
}

typedef struct MountedArchives
{
    MMMemoryStream backdrops, templates, duplicate;
    unsigned opens, closes, failures;
} MountedArchives;

static int fixture_archive_open(void *p, const char *path, void **handle, MMReadStream *stream)
{
    MountedArchives *archives = (MountedArchives *)p;
    MMMemoryStream *file;
    if (!strcmp(path, "unreadable"))
    {
        ++archives->failures;
        return MM_MOVIE_IO;
    }
    CHECK(!strcmp(path, "backgrounds") || !strcmp(path, "templates") || !strcmp(path, "duplicate"));
    if (!strcmp(path, "backgrounds"))
        file = &archives->backdrops;
    else if (!strcmp(path, "templates"))
        file = &archives->templates;
    else
        file = &archives->duplicate;
    ++archives->opens;
    *stream = file->stream;
    *handle = file;
    return MM_MOVIE_OK;
}

static void fixture_archive_close(void *p, void *handle)
{
    MountedArchives *archives = (MountedArchives *)p;
    CHECK(handle == &archives->backdrops || handle == &archives->templates || handle == &archives->duplicate);
    ++archives->closes;
}

static void movie_content(void)
{
    const MMChunkPolicy policy = {0, 0, 516};
    const MMVfsMount mounts[] = {{"z.3CN", "templates"}, {"a.3cn", "backgrounds"}, {"0.3th", "thumbnail"},
                                 {"zz.3cn", "duplicate"}};
    const MMVfsMount unreadable[] = {{"z.3cn", "templates"}, {"a.3cn", "unreadable"}};
    unsigned char bytes[1024], backgrounds[1024], templates[2048];
    char name[MM_MOVIE_SOURCE_NAME_BYTES];
    MMMovieArchiveProvider provider;
    MountedArchives archives;
    MMMemoryStream input;
    MMMovieSession *movie;
    MMMovieContent *content;
    MMMovieSourceTable sources;
    MMMovieSourceName row;
    MMChunkIndex *index;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMReadSlice table;
    MMVfs *vfs;
    MMMovieAsset asset = {9, 0x544d504cUL, 13};
    MMReadStream resolved;
    MMU32 size, position;
    int wide;

    memset(&archives, 0, sizeof(archives));
    size = camera_fixture(backgrounds, 0, 0, 2, 0);
    CHECK(mm_memory_stream_init(&archives.backdrops, backgrounds, size));
    size = actor_fixture(templates, 0, 2, ACTOR_FIXTURE_OK);
    CHECK(mm_memory_stream_init(&archives.templates, templates, size));
    CHECK(mm_memory_stream_init(&archives.duplicate, templates, size));
    provider.context = &archives;
    provider.open = fixture_archive_open;
    provider.close = fixture_archive_close;
    for (wide = 0; wide <= 1; ++wide)
    {
        size = source_movie(bytes, wide, wide);
        CHECK(mm_memory_stream_init(&input, bytes, size));
        CHECK(mm_chunk_index_open(&input.stream, &policy, &index) == MM_CHUNK_OK);
        access = mm_chunk_index_access(index);
        CHECK(mm_movie_view_source_table(&access, 17, &entry, &position) == MM_MOVIE_OK &&
              entry.number == 5 && position == 0);
        CHECK(mm_read_slice_init(&table, &input.stream, entry.offset, entry.bytes));
        CHECK(mm_movie_source_table_open(&table.stream, entry.bytes, &sources) == MM_MOVIE_OK &&
              sources.count == 2 && sources.big_endian == wide);
        CHECK(mm_movie_source_table_at(&sources, 0, &row) == MM_MOVIE_OK && row.id == 9 &&
              row.size == 13U * (wide ? 2 : 1) && row.bytes[wide ? 2 : 1] == 0xe9);
        CHECK(mm_movie_source_table_at(&sources, 1, &row) == MM_MOVIE_OK && row.id == 9 && !row.size);
        CHECK(mm_movie_source_table_at(&sources, 2, &row) == MM_MOVIE_ASSET_NOT_FOUND && row.id == 9 && !row.size);
        CHECK(mm_movie_source_table_open(&table.stream, entry.bytes - 1, &sources) == MM_MOVIE_BAD_FILE &&
              sources.count == 2);
        mm_chunk_index_close(index);
        CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_OK);
        CHECK(mm_movie_session_source_name(movie, 9, name) == MM_MOVIE_OK);
        CHECK(!strcmp(name, "D\xc3\xa9" "cor/3DMOVIE"));
        CHECK(mm_movie_session_source_name(movie, 2, name) == MM_MOVIE_ASSET_NOT_FOUND && name[0] == 'D');
        vfs = mm_vfs_create(NULL);
        CHECK(vfs && mm_vfs_mount(vfs, "3dmovie", mounts, sizeof(mounts) / sizeof(mounts[0]), 0) == MM_VFS_OK);
        content = mm_movie_content_open(movie, vfs, &provider);
        CHECK(content != NULL);
        asset.type = 0x544d504cUL;
        asset.number = 13;
        CHECK(mm_movie_content_source(content, &asset, &resolved) == MM_MOVIE_OK);
        CHECK(resolved.context == &archives.templates && archives.opens == (unsigned)wide * 3 + 2);
        asset.type = 0x424b4744UL;
        asset.number = 4;
        CHECK(mm_movie_content_source(content, &asset, &resolved) == MM_MOVIE_OK);
        CHECK(resolved.context == &archives.backdrops);
        asset.number = 13;
        CHECK(mm_movie_content_source(content, &asset, &resolved) == MM_MOVIE_ASSET_NOT_FOUND);
        CHECK(resolved.context == &archives.backdrops && archives.opens == (unsigned)(wide + 1) * 3);
        mm_movie_content_close(content);
        CHECK(archives.closes == archives.opens);
        mm_vfs_destroy(vfs);
        vfs = mm_vfs_create(NULL);
        CHECK(vfs && mm_vfs_mount(vfs, "3dmovie", unreadable, sizeof(unreadable) / sizeof(unreadable[0]), 0) ==
                         MM_VFS_OK);
        content = mm_movie_content_open(movie, vfs, &provider);
        CHECK(content != NULL);
        asset.type = 0x544d504cUL;
        CHECK(mm_movie_content_source(content, &asset, &resolved) == MM_MOVIE_IO);
        CHECK(resolved.context == &archives.backdrops && archives.opens == archives.closes &&
              archives.failures == (unsigned)wide + 1);
        mm_movie_content_close(content);
        mm_vfs_destroy(vfs);
        mm_movie_session_close(movie);
    }
    CHECK(mm_movie_session_open(&archives.backdrops.stream, &policy, &movie) == MM_MOVIE_OK);
    CHECK(mm_movie_session_source_name(movie, 2, name) == MM_MOVIE_OK && !strcmp(name, "3dmovie"));
    mm_movie_session_close(movie);
}

static int camera_source(void *context, const MMMovieAsset *asset, MMReadStream *out)
{
    CHECK(asset->source == 2 && asset->type == 0x424b4744UL && asset->number == 4);
    *out = *(const MMReadStream *)context;
    return MM_MOVIE_OK;
}

static void movie_camera(void)
{
    const MMChunkPolicy policy = {0, 0, 516};
    unsigned char bytes[1024];
    MMMemoryStream input;
    MMMovieSession *movie;
    MMMovieCamera camera;
    MMMovieEvaluatedFrame *frame = NULL, *prior;
    MMU32 size;
    int big;
    for (big = 0; big <= 1; ++big)
    {
        size = camera_fixture(bytes, big, big ? MM_CODEC_KCDC : MM_CODEC_KCD2, 0, 0);
        CHECK(mm_memory_stream_init(&input, bytes, size));
        CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_OK);
        CHECK(mm_movie_session_camera(movie, 0, 5, NULL, NULL, &camera) == MM_MOVIE_OK);
        CHECK(camera.background.source == 0 && camera.slot == 1 && camera.number == 80 && camera.data.near_z == 456);
        CHECK(camera.data.matrix.m[3][2] == -2);
        CHECK(mm_movie_session_camera(movie, 0, 8, NULL, NULL, &camera) == MM_MOVIE_OK && camera.slot == 0);
        CHECK(mm_movie_session_camera(movie, 0, 4, NULL, NULL, &camera) == MM_MOVIE_OK && camera.number == 90);
        CHECK(mm_movie_session_camera(movie, 0, 0, NULL, NULL, &camera) == MM_MOVIE_FRAME_OUT_OF_RANGE &&
              camera.number == 90);
        CHECK(mm_movie_session_seek(movie, 0, 5, NULL, NULL, &frame) == MM_MOVIE_OK &&
              frame->frame == 5 && frame->scene.number == 36 && !frame->actor_count && !frame->actors &&
              !frame->bodies && frame->camera.number == 80 && frame->camera.data.near_z == 456);
        prior = frame;
        CHECK(mm_movie_session_seek_with_bodies(movie, 0, 0, NULL, NULL, &frame) == MM_MOVIE_FRAME_OUT_OF_RANGE &&
              frame == prior);
        CHECK(mm_movie_session_seek_with_bodies(movie, 0, 4, NULL, NULL, &frame) == MM_MOVIE_OK &&
              frame->frame == 4 && !frame->actor_count && !frame->actors && !frame->bodies);
        mm_movie_session_close(movie);
        CHECK(frame->camera.number == 90 && prior->camera.number == 80);
        mm_movie_frame_dispose(prior);
        mm_movie_frame_dispose(frame);
        frame = NULL;
    }
    size = camera_fixture(bytes, 0, 0, 2, 0);
    CHECK(mm_memory_stream_init(&input, bytes, size));
    CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_OK);
    CHECK(mm_movie_session_camera(movie, 0, 7, NULL, NULL, &camera) == MM_MOVIE_ASSET_NOT_FOUND && camera.number == 90);
    CHECK(mm_movie_session_camera(movie, 0, 7, camera_source, &input.stream, &camera) == MM_MOVIE_OK &&
          camera.background.source == 2 && camera.number == 80);
    CHECK(mm_movie_session_seek(movie, 0, 7, NULL, NULL, &frame) == MM_MOVIE_ASSET_NOT_FOUND && !frame);
    CHECK(mm_movie_session_seek(movie, 0, 7, camera_source, &input.stream, &frame) == MM_MOVIE_OK &&
          frame->camera.background.source == 2 && frame->camera.number == 80);
    prior = frame;
    CHECK(mm_movie_session_seek(movie, 0, 9, NULL, NULL, &frame) == MM_MOVIE_ASSET_NOT_FOUND && frame == prior);
    mm_movie_frame_dispose(frame);
    frame = NULL;
    mm_movie_session_close(movie);
    size = camera_fixture(bytes, 0, 0, 0, 1);
    CHECK(mm_memory_stream_init(&input, bytes, size));
    CHECK(mm_movie_session_open(&input.stream, &policy, &movie) == MM_MOVIE_OK);
    CHECK(mm_movie_session_camera(movie, 0, 7, NULL, NULL, &camera) == MM_MOVIE_BAD_FILE && camera.number == 80);
    CHECK(mm_movie_session_seek(movie, 0, 7, NULL, NULL, &frame) == MM_MOVIE_BAD_FILE && !frame);
    mm_movie_session_close(movie);
}
/* Three independently encoded records. Root owns the first child twice and
 * the second once; two different targets share a slot and type. */
static size_t fixture(unsigned char *bytes, int prefix_big, int big, int stride, int version)
{
    MMU32 sizes[3], offsets[3], data, total, i;
    unsigned char *group, *row, *child;
    sizes[0] = stride + 40;
    sizes[1] = sizes[2] = stride;
    offsets[0] = 0;
    offsets[1] = sizes[0];
    offsets[2] = sizes[0] + sizes[1];
    data = sizes[0] + sizes[1] + sizes[2];
    total = 136 + 20 + data + 24;
    memset(bytes, 0, 1024);
    memcpy(bytes, "CHN2", 4);
    put32(bytes + 4, 0x54455354UL, prefix_big);
    put16(bytes + 8, version, prefix_big);
    put16(bytes + 10, version < 4 ? 1 : 4, prefix_big);
    put16(bytes + 12, 1, prefix_big);
    put16(bytes + 14, 0x0303, prefix_big);
    put32(bytes + 16, total, prefix_big);
    put32(bytes + 20, 136, prefix_big);
    put32(bytes + 24, total - 136, prefix_big);
    put32(bytes + 28, total, prefix_big);
    group = bytes + 136;
    put16(group, 1, big);
    put16(group + 2, 0x0303, big);
    put32(group + 4, 3, big);
    put32(group + 8, data, big);
    put32(group + 12, 0xffffffffUL, big);
    put32(group + 16, stride, big);
    for (i = 0; i < 3; ++i)
    {
        row = group + 20 + offsets[i];
        put32(row, i ? 0x42424242UL : 0x41414141UL, big);
        put32(row + 4, i == 2 ? 0xffffffffUL : i + 1, big);
        put32(row + 8, i ? 0 : 128, big);
        if (stride == 20)
        {
            put32(row + 12, i ? 16 : (8U << 8) | 2U, big);
            put16(row + 16, i ? 0 : 3, big);
            put16(row + 18, i ? (i == 1 ? 2 : 1) : 0, big);
        }
        else
        {
            put32(row + 12, i ? 0 : 8, big);
            put32(row + 16, i ? 0 : 3, big);
            put32(row + 20, i ? (i == 1 ? 2 : 1) : 0, big);
            put32(row + 24, 1234, big); /* Runtime ID must not become persistent state. */
            if (version < 4)
            {
                row[28] = 1; /* On-extra is never retained when opening disk indexes. */
                row[29] = i == 0;
                row[30] = i != 0;
            }
            else
                put32(row + 28, i ? 17 : 3, big);
        }
        put32(group + 20 + data + i * 8, offsets[i], big);
        put32(group + 24 + data + i * 8, sizes[i], big);
    }
    row = group + 20;
    child = row + stride;
    for (i = 0; i < 3; ++i)
    {
        put32(child + i * 12, 0x42424242UL, big);
        put32(child + i * 12 + 4, i == 1 ? 0xffffffffUL : 2, big);
        put32(child + i * 12 + 8, i == 2 ? 1 : 0, big);
    }
    memcpy(child + 36, "name", 4);
    return (size_t)total;
}
static void wire_indexes(void)
{
    unsigned char bytes[1024];
    MMMemoryStream memory;
    MMChunkPolicy policy;
    MMChunkIndex *index, *saved;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMChunkChild child;
    MMChunkHeader header;
    const unsigned char *name;
    MMU32 count, position;
    size_t length;
    int prefix_big, big, stride, version;
    policy.expanded_runtime = 1;
    policy.native_big_endian = 0;
    policy.max_name_bytes = 516;
    for (prefix_big = 0; prefix_big <= 1; ++prefix_big)
        for (big = 0; big <= 1; ++big)
            for (stride = 20; stride <= 32; stride += 12)
                for (version = 1; version <= 5; ++version)
                {
                    length = fixture(bytes, prefix_big, big, stride, version);
                    CHECK(mm_memory_stream_init(&memory, bytes, length));
                    if (version < 4 && stride == 20)
                    {
                        CHECK(mm_chunk_index_open(&memory.stream, &policy, &index) == MM_CHUNK_BAD_FILE);
                        continue;
                    }
                    CHECK(mm_chunk_index_open(&memory.stream, &policy, &index) == MM_CHUNK_OK);
                    access = mm_chunk_index_access(index);
                    CHECK(access.count == 3 && mm_chunk_index_header(index)->big_endian == prefix_big);
                    CHECK(mm_chunk_find(&access, 0x41414141UL, 1, &position) == MM_CHUNK_OK &&
                          position == 0);
                    CHECK(mm_chunk_at(&access, 0, &entry) == MM_CHUNK_OK && entry.bytes == 8 &&
                          entry.offset == 128 && entry.flags == MM_CHUNK_LONER && entry.child_count == 3);
                    CHECK(mm_chunk_find(&access, 0x42424242UL, 3, &position) == MM_CHUNK_NOT_FOUND &&
                          position == 2);
                    CHECK(mm_chunk_find(&access, 0xffffffffUL, 0, &position) == MM_CHUNK_NOT_FOUND &&
                          position == 3);
                    CHECK(mm_chunk_find_child(&access, 0, 0, 0x42424242UL, 3, &position) ==
                              MM_CHUNK_NOT_FOUND &&
                          position == 1);
                    CHECK(mm_chunk_child_type(&access, 0, 0, 0x42424242UL, &child) == MM_CHUNK_OK &&
                          child.number == 2);
                    CHECK(mm_chunk_child_at(&access, 0, 1, &child) == MM_CHUNK_OK &&
                          child.number == 0xffffffffUL);
                    child.slot = 123;
                    CHECK(mm_chunk_child_type(&access, 0, 2, 0, &child) == MM_CHUNK_NOT_FOUND &&
                          child.slot == 123);
                    CHECK(mm_chunk_validate_graph(&access, 65535) == MM_CHUNK_OK);
                    CHECK(mm_chunk_type_at(&access, 0x42424242UL, 1, &entry, &position) == MM_CHUNK_OK &&
                          position == 2 && entry.number == 0xffffffffUL);
                    position = 123;
                    CHECK(mm_chunk_type_at(&access, 0x42424242UL, 0xffffffffUL, &entry, &position) ==
                              MM_CHUNK_NOT_FOUND &&
                          position == 123);
                    CHECK(mm_chunk_index_name(index, 0, &name, &count) == MM_CHUNK_OK && count == 4 &&
                          !memcmp(name, "name", 4));
                    /* Input is no longer borrowed once open returns. Failed opens
                     * leave the prior owning handle and its records intact. */
                    bytes[0] = 'X';
                    saved = index;
                    CHECK(mm_chunk_index_open(&memory.stream, &policy, &index) == MM_CHUNK_BAD_FILE &&
                          index == saved);
                    CHECK(mm_chunk_at(&access, 0, &entry) == MM_CHUNK_OK && entry.number == 1);
                    mm_chunk_index_close(index);
                }
    length = fixture(bytes, 0, 1, 32, 5);
    CHECK(mm_memory_stream_init(&memory, bytes, length));
    policy.expanded_runtime = 0;
    /* Existing expanded-to-compact pre-swap check rejects this opposite-endian
     * row. A port must not silently repair the original reader's decision. */
    CHECK(mm_chunk_index_open(&memory.stream, &policy, &index) == MM_CHUNK_BAD_FILE);
    length = fixture(bytes, 0, 0, 20, 5);
    CHECK(mm_memory_stream_init(&memory, bytes, length));
    header.creator = 123;
    memory.stream.length = (MMU32)length - 1;
    CHECK(mm_chunk_read_header(&memory.stream, &header) == MM_CHUNK_BAD_FILE && header.creator == 123);
    memory.stream.length = (MMU32)length;
    put32(bytes + 136 + 20 + 100, 0xffffffffUL, 0); /* Location points before the GG. */
    CHECK(mm_chunk_index_open(&memory.stream, &policy, &index) == MM_CHUNK_BAD_FILE);
    fixture(bytes, 0, 0, 20, 5);
    put32(bytes + 140, 0xffffffffUL, 0); /* Count arithmetic must not wrap. */
    CHECK(mm_chunk_index_open(&memory.stream, &policy, &index) == MM_CHUNK_BAD_FILE);
    fixture(bytes, 0, 0, 20, 5);
    put32(bytes + 24, 0xffffffffUL, 0); /* Index extent arithmetic must not wrap. */
    CHECK(mm_chunk_index_open(&memory.stream, &policy, &index) == MM_CHUNK_BAD_FILE);
}

typedef struct LiveGraph
{
    MMChunkEntry entries[4];
    MMChunkChild children[4][2];
    int fail;
} LiveGraph;

typedef struct FailingInput
{
    unsigned char bytes[1024];
    int calls, fail_at;
} FailingInput;
static int failing_read(void *context, MMU32 offset, void *bytes, size_t count)
{
    FailingInput *input = (FailingInput *)context;
    if (++input->calls == input->fail_at)
        return 0;
    memcpy(bytes, input->bytes + offset, count);
    return 1;
}
static void io_failures(void)
{
    FailingInput input;
    MMReadStream stream;
    MMChunkPolicy policy = {0, 0, 516};
    MMChunkIndex *index, *saved;
    MMChunkAccess access;
    MMChunkEntry entry;
    int step;
    stream.length = (MMU32)fixture(input.bytes, 0, 0, 20, 5);
    stream.context = &input;
    stream.read_at = failing_read;
    input.calls = input.fail_at = 0;
    CHECK(mm_chunk_index_open(&stream, &policy, &index) == MM_CHUNK_OK);
    saved = index;
    access = mm_chunk_index_access(index);
    for (step = 1; step <= 4; ++step)
    {
        input.calls = 0;
        input.fail_at = step;
        CHECK(mm_chunk_index_open(&stream, &policy, &index) == MM_CHUNK_IO && index == saved);
        CHECK(mm_chunk_at(&access, 0, &entry) == MM_CHUNK_OK && entry.number == 1);
    }
    mm_chunk_index_close(index);
}
static int live_entry(void *context, MMU32 position, MMChunkEntry *entry)
{
    LiveGraph *graph = (LiveGraph *)context;
    if (graph->fail)
        return 0;
    *entry = graph->entries[position];
    return 1;
}
static int live_child(void *context, MMU32 parent, MMU32 position, MMChunkChild *child)
{
    LiveGraph *graph = (LiveGraph *)context;
    *child = graph->children[parent][position];
    return 1;
}
static void diamond(LiveGraph *graph)
{
    int i;
    memset(graph, 0, sizeof(*graph));
    for (i = 0; i < 4; ++i)
    {
        graph->entries[i].type = 10;
        graph->entries[i].number = i + 1;
        graph->entries[i].parent_count = i ? (i == 3 ? 2 : 1) : 0;
        graph->entries[i].child_count = i == 0 ? 2 : i == 3 ? 0 : 1;
        graph->entries[i].variable_bytes = graph->entries[i].child_count * 12;
        graph->children[i][0].type = graph->children[i][1].type = 10;
        graph->children[i][0].number = i == 0 ? 2 : 4;
    }
    graph->children[0][1].number = 3;
    graph->children[0][1].slot = 1;
}
static void graphs(void)
{
    LiveGraph graph;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMU32 position;
    diamond(&graph);
    access.context = &graph;
    access.count = 4;
    access.entry = live_entry;
    access.child = live_child;
    CHECK(mm_chunk_validate_graph(&access, 65535) == MM_CHUNK_OK);
    graph.entries[1].parent_count = 2;
    graph.entries[3].parent_count = 1;
    CHECK(mm_chunk_validate_graph(&access, 65535) == MM_CHUNK_OK); /* Retail checks aggregate totals. */
    diamond(&graph);
    graph.entries[3].child_count = 1;
    graph.entries[3].variable_bytes = 12;
    graph.entries[1].parent_count = 2;
    graph.children[3][0].number = 2;
    CHECK(mm_chunk_validate_graph(&access, 65535) == MM_CHUNK_BAD_FILE); /* Reachable cycle. */
    diamond(&graph);
    graph.children[1][0].number = 99;
    CHECK(mm_chunk_validate_graph(&access, 65535) == MM_CHUNK_BAD_FILE); /* Missing target. */
    diamond(&graph);
    graph.entries[0].child_count = 0;
    graph.entries[3].parent_count = 0;
    CHECK(mm_chunk_validate_graph(&access, 65535) == MM_CHUNK_BAD_FILE); /* Orphan subgraph. */
    diamond(&graph);
    graph.entries[0].child_count = 0;
    graph.entries[3].parent_count = 0;
    graph.children[1][0].number = 3;
    graph.children[2][0].number = 2;
    CHECK(mm_chunk_validate_graph(&access, 65535) == MM_CHUNK_BAD_FILE); /* Unrooted cycle. */
    diamond(&graph);
    graph.entries[3].number = 9;
    CHECK(mm_chunk_find(&access, 10, 9, &position) == MM_CHUNK_OK && position == 3);
    CHECK(mm_chunk_find(&access, 10, 4, &position) == MM_CHUNK_NOT_FOUND && position == 3);
    graph.fail = 1;
    entry.number = 123;
    position = 123;
    CHECK(mm_chunk_at(&access, 0, &entry) == MM_CHUNK_IO && entry.number == 123);
    CHECK(mm_chunk_find(&access, 10, 9, &position) == MM_CHUNK_IO && position == 123);
    access.count = 0;
    CHECK(mm_chunk_validate_graph(&access, 65535) == MM_CHUNK_OK);
}

static void frame_resource_links(void)
{
    const MMU32 actor = 0x41435452UL, background = 0x424b4744UL, camera = 0x43414d20UL;
    const MMU32 tmplt = 0x544d504cUL, bitmap = 0x4d424d50UL, depth = 0x5a424d50UL;
    LiveGraph graph;
    MMChunkAccess access;
    MMU32 number = 90;
    memset(&graph, 0, sizeof(graph));
    access.context = &graph;
    access.count = 4;
    access.entry = live_entry;
    access.child = live_child;
    graph.entries[0].type = actor;
    graph.entries[0].number = 1;
    graph.entries[0].child_count = 1;
    graph.entries[1].type = background;
    graph.entries[1].number = 2;
    graph.entries[1].child_count = 2;
    graph.entries[2].type = camera;
    graph.entries[2].number = 3;
    graph.entries[2].child_count = 2;
    graph.entries[3].type = tmplt;
    graph.entries[3].number = 4;
    graph.children[0][0].type = tmplt;
    graph.children[0][0].number = 4;
    graph.children[1][0].type = graph.children[1][1].type = camera;
    graph.children[1][0].number = 3;
    graph.children[1][1].number = 6;
    graph.children[2][0].type = bitmap;
    graph.children[2][0].number = 7;
    graph.children[2][1].type = depth;
    graph.children[2][1].number = 8;

    CHECK(mm_actor_template_link(&access, 1, 29, 1, &number) == MM_CHUNK_OK && number == 90);
    CHECK(mm_actor_template_link(&access, 1, 0, 1, &number) == MM_CHUNK_OK && number == 4);
    graph.children[0][0].number = 5;
    CHECK(mm_actor_template_link(&access, 1, 0, 1, &number) == MM_CHUNK_NOT_FOUND && number == 4);
    CHECK(mm_actor_template_link(&access, 1, 0, 0, &number) == MM_CHUNK_OK && number == 5);
    graph.children[0][0].slot = 1;
    CHECK(mm_actor_template_link(&access, 1, 0, 0, &number) == MM_CHUNK_NOT_FOUND && number == 5);
    CHECK(mm_chunk_link_number(&access, background, 2, 0, camera, &number) == MM_CHUNK_OK && number == 3);
    CHECK(mm_chunk_link_number(&access, background, 2, 1, camera, &number) == MM_CHUNK_NOT_FOUND && number == 3);
    CHECK(mm_background_camera_image_link(&access, 3, 0, &number) == MM_CHUNK_OK && number == 7);
    CHECK(mm_background_camera_image_link(&access, 3, 1, &number) == MM_CHUNK_OK && number == 8);
    graph.children[2][0].type = bitmap + 1;
    CHECK(mm_background_camera_image_link(&access, 3, 0, &number) == MM_CHUNK_NOT_FOUND && number == 8);
    graph.fail = 1;
    CHECK(mm_background_camera_image_link(&access, 3, 1, &number) == MM_CHUNK_IO && number == 8);
}
static int chain_entry(void *context, MMU32 position, MMChunkEntry *entry)
{
    MMU32 count = *(MMU32 *)context;
    memset(entry, 0, sizeof(*entry));
    entry->number = position;
    entry->parent_count = position ? 1 : 0;
    entry->child_count = position + 1 < count ? 1 : 0;
    entry->variable_bytes = entry->child_count * 12;
    return 1;
}
static int chain_child(void *context, MMU32 parent, MMU32 position, MMChunkChild *child)
{
    (void)context;
    (void)position;
    child->type = child->slot = 0;
    child->number = parent + 1;
    return 1;
}
static void deep_graph(void)
{
    MMU32 count = 10000;
    MMChunkAccess access;
    access.context = &count;
    access.count = count;
    access.entry = chain_entry;
    access.child = chain_child;
    CHECK(mm_chunk_validate_graph(&access, 65535) == MM_CHUNK_OK);
}
typedef struct WriteProbe
{
    unsigned char source[1024], output[1024];
    MMU32 index_offset, index_bytes, map_bytes;
    int calls, fail, dry;
} WriteProbe;
static int write_index(void *context, MMU32 offset, MMU32 bytes)
{
    WriteProbe *p = (WriteProbe *)context;
    CHECK(++p->calls == 1 && offset == p->index_offset && bytes == p->index_bytes);
    if (!p->dry)
        memcpy(p->output + offset, p->source + offset, bytes);
    return p->fail != p->calls;
}
static int write_map(void *context, MMU32 offset, MMU32 bytes)
{
    WriteProbe *p = (WriteProbe *)context;
    CHECK(++p->calls == 2 && offset == p->index_offset + p->index_bytes && bytes == p->map_bytes);
    if (!p->dry)
        memcpy(p->output + offset, p->source + offset, bytes);
    return p->fail != p->calls;
}
static int write_prefix(void *context, const void *bytes, size_t count)
{
    WriteProbe *p = (WriteProbe *)context;
    CHECK(++p->calls == (p->map_bytes ? 3 : 2) && count == 128);
    /* Failure may already have changed bytes; no atomic-write guarantee. */
    memcpy(p->output, bytes, p->fail == p->calls ? 7 : count);
    return p->fail != p->calls;
}
static void init_write(WriteProbe *p, const MMChunkSave *save)
{
    memset(p, 0, sizeof(*p));
    memset(p->output, 0xa5, sizeof(p->output));
    p->index_offset = save->index_offset;
    p->index_bytes = save->index_bytes;
    p->map_bytes = save->map_bytes;
}
static void writes(void)
{
    WriteProbe probe;
    MMChunkWriter writer;
    MMChunkSave save;
    MMChunkHeader header, readback, sentinel;
    MMMemoryStream stream;
    MMChunkIndex *index;
    MMChunkPolicy policy = {0, 0, 516};
    MMU32 total, hash = 2166136261UL;
    int big, map, fail, i, j;
    static const MMU32 lengths[] = {0, 1, 3, 257, 1024};
    static const MMU32 creators[] = {0x53415645UL, 0x4e455854UL, 0x434f5059UL, 0x41532020UL};
    writer.context = &probe;
    writer.index = write_index;
    writer.map = write_map;
    writer.prefix = write_prefix;
    memset(&sentinel, 0x6b, sizeof(sentinel));
    for (big = 0; big < 2; ++big)
        for (map = 0; map < 2; ++map)
        {
            memset(&save, 0, sizeof(save));
            save.creator = 0x54455354UL;
            save.index_offset = 136;
            save.big_endian = big;
            save.system = 0x0303;
            total = (MMU32)fixture(probe.source, big, big, 20, 5);
            save.index_bytes = total - save.index_offset;
            save.map_bytes = map ? 20 : 0;
            for (fail = 0; fail <= (map ? 3 : 2); ++fail)
            {
                init_write(&probe, &save);
                total = (MMU32)fixture(probe.source, big, big, 20, 5);
                if (map)
                {
                    put32(probe.source + 16, total + 20, big);
                    put32(probe.source + 32, 20, big);
                    memset(probe.source + total, 0x3c, 20);
                }
                probe.fail = fail;
                header = sentinel;
                CHECK(mm_chunk_write_index(&save, &writer, &header) ==
                      (fail ? MM_CHUNK_IO : MM_CHUNK_OK));
                CHECK(probe.calls == (fail ? fail : (map ? 3 : 2)));
                if (fail)
                {
                    CHECK(!memcmp(&header, &sentinel, sizeof(header)));
                    if (fail < (map ? 3 : 2))
                        for (i = 0; i < 128; ++i)
                            CHECK(probe.output[i] == 0xa5);
                    continue;
                }
                CHECK(!memcmp(probe.output, probe.source, 128));
                CHECK(!memcmp(probe.output + 136, probe.source + 136, total + save.map_bytes - 136));
                for (i = 128; i < 136; ++i)
                    CHECK(probe.output[i] == 0xa5);
                for (i = (int)(total + save.map_bytes); i < 1024; ++i)
                    CHECK(probe.output[i] == 0xa5); /* No truncation/tail clearing. */
                CHECK(mm_memory_stream_init(&stream, probe.output, sizeof(probe.output)));
                CHECK(mm_chunk_read_header(&stream.stream, &readback) == MM_CHUNK_OK);
                CHECK(readback.logical_end == header.logical_end && readback.creator == save.creator &&
                      readback.big_endian == big && readback.map_bytes == save.map_bytes);
                index = NULL;
                CHECK(mm_chunk_index_open(&stream.stream, &policy, &index) == MM_CHUNK_OK);
                mm_chunk_index_close(index);
            }
        }
    /* Frozen prefixes from twenty native pre-conversion save outputs, including
     * free maps, compacted copies, save-as and shortened physical files. */
    for (i = 0; i < 5; ++i)
        for (j = 0; j < 4; ++j)
        {
            save.creator = creators[j];
            save.index_offset = lengths[i] + (j < 2 ? 176 : 159);
            save.index_bytes = j == 0 ? 104 : 76;
            save.map_bytes = j < 2 ? 20 : 0;
            save.big_endian = 0;
            save.system = 0x0303;
            init_write(&probe, &save);
            probe.dry = 1;
            CHECK(mm_chunk_write_index(&save, &writer, NULL) == MM_CHUNK_OK);
            for (big = 0; big < 128; ++big)
                hash = (hash ^ probe.output[big]) * 16777619UL;
        }
    CHECK(hash == 0x012f9b07UL);
    /* Validate all extents and required callbacks before touching the sink. */
    for (i = 0; i < 12; ++i)
    {
        save.creator = 0xffffffffUL;
        save.index_offset = 128;
        save.index_bytes = 20;
        save.map_bytes = 20;
        save.big_endian = 0;
        save.system = -32768;
        writer.index = write_index;
        writer.map = write_map;
        writer.prefix = write_prefix;
        switch (i)
        {
        case 0:
            save.index_offset = 127;
            break;
        case 1:
            save.index_offset = 0x80000000UL;
            break;
        case 2:
            save.index_bytes = 0;
            break;
        case 3:
            save.index_bytes = 0x80000000UL;
            break;
        case 4:
            save.map_bytes = 0x80000000UL;
            break;
        case 5:
            save.index_offset = 0x7ffffff0UL;
            break;
        case 6:
            save.index_offset = 0x7fffffe0UL;
            break;
        case 7:
            save.big_endian = 2;
            break;
        case 8:
            save.system = 32768;
            break;
        case 9:
            writer.index = NULL;
            break;
        case 10:
            writer.map = NULL;
            break;
        case 11:
            writer.prefix = NULL;
            break;
        }
        init_write(&probe, &save);
        header = sentinel;
        CHECK(mm_chunk_write_index(&save, &writer, &header) == MM_CHUNK_BAD_FILE);
        CHECK(!probe.calls && !memcmp(&header, &sentinel, sizeof(header)));
    }
    writer.index = write_index;
    writer.map = NULL;
    writer.prefix = write_prefix;
    save.index_offset = 0x7fffffffUL - 20;
    save.index_bytes = 20;
    save.map_bytes = 0;
    init_write(&probe, &save);
    probe.dry = 1;
    CHECK(mm_chunk_write_index(&save, &writer, &header) == MM_CHUNK_OK);
    CHECK(header.logical_end == 0x7fffffffUL && header.creator == 0xffffffffUL && header.system == -32768);
    /* Collection sections are individually bounded; a serialized index can
     * exceed one section's 128-MiB limit. Commit extents use signed file bounds. */
    writer.map = write_map;
    save.index_offset = 128;
    save.index_bytes = 0x08000014UL;
    save.map_bytes = 0x0800000cUL;
    init_write(&probe, &save);
    probe.dry = 1;
    CHECK(mm_chunk_write_index(&save, &writer, &header) == MM_CHUNK_OK);
    CHECK(header.logical_end == 128 + save.index_bytes + save.map_bytes);
    CHECK(mm_chunk_write_index(NULL, &writer, NULL) == MM_CHUNK_BAD_FILE);
    CHECK(mm_chunk_write_index(&save, NULL, NULL) == MM_CHUNK_BAD_FILE);
}
int main(void)
{
    wire_indexes();
    movie_sessions();
    movie_camera();
    movie_directory();
    movie_actors();
    movie_content();
    graphs();
    frame_resource_links();
    io_failures();
    deep_graph();
    writes();
    return 0;
}
