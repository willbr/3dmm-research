#include "mm/scene_data.h"
#include "mm/collections.h"
#include "mm/movie_archive_edit.h"
#include "mm/movie_data.h"
#include "mm/movie_sound.h"
#include "mm/movie_sound_archive.h"
#include "mm/scene_edit.h"
#include "mm/text.h"
#include "mm/text_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "scene data failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
static void put32(unsigned char *p, MMU32 n, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[big ? 3 - i : i] = (unsigned char)(n >> (i * 8));
}
static void set_text(MMMovieTitle *t, const char *text)
{
    unsigned short wide[256];
    size_t n = strlen(text), i;
    for (i = 0; i < n; ++i)
        wide[i] = (unsigned char)text[i];
    CHECK(mm_movie_title_set(t, t->unit == 1 ? (const void *)text : wide, (MMU32)n));
}
typedef struct Host Host;
typedef struct Group
{
    Host *host;
    int start;
    MMU32 fixed_width, count;
    MMI32 sizes[4];
    unsigned char fixed[4][8], payload[4][64];
} Group;
struct Host
{
    char log[128];
    int used, stage, active, have_old, old[2], created[2], linked[2], loner[2];
    int group_calls, group_fail, groups, root_fail, root, read_fail, io_fail;
    MMU32 *number, length;
    unsigned char header[16];
    Group *read_group;
    int read_order, read_fail_group, read_releases;
};
static void event(Host *h, char c)
{
    CHECK(h->used < 127);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static void reset(Host *h)
{
    memset(h, 0, sizeof(*h));
    h->have_old = h->old[0] = h->old[1] = 1;
    h->length = 16;
}
static MMU32 fixed_width(void *g)
{
    return ((Group *)g)->fixed_width;
}
static MMU32 count(void *g)
{
    return ((Group *)g)->count;
}
static void fixed(void *g, MMU32 i, unsigned char *out)
{
    CHECK(i < ((Group *)g)->count);
    memcpy(out, ((Group *)g)->fixed[i], 8);
}
static MMI32 size(void *g, MMU32 i)
{
    CHECK(i < ((Group *)g)->count);
    return ((Group *)g)->sizes[i];
}
static const void *data(void *g, MMU32 i)
{
    CHECK(i < ((Group *)g)->count);
    CHECK(((Group *)g)->sizes[i] >= 16);
    return ((Group *)g)->payload[i];
}
static MMSceneGroupServices group_services(void)
{
    MMSceneGroupServices s = {fixed_width, count, fixed, size, data};
    return s;
}
static void init_group(Group *g, Host *h, int start, int type, MMU32 bytes, int big)
{
    memset(g, 0, sizeof(*g));
    g->host = h;
    g->start = start;
    g->fixed_width = 8;
    g->count = 1;
    g->sizes[0] = bytes;
    put32(g->fixed[0], 0x80000000UL, big);
    put32(g->fixed[0] + 4, (MMU32)type, big);
}
static void validation(void)
{
    Group g;
    Host h;
    MMSceneGroupServices s;
    int big, type, start, expected;
    MMU32 sizes[] = {4, 36, 4, 4, 16, 8};
    s = group_services();
    reset(&h);
    for (big = 0; big < 2; ++big)
        for (type = 0; type < 6; ++type)
            for (start = 0; start < 2; ++start)
            {
                init_group(&g, &h, start, type, sizes[type], big);
                put32(g.payload[0] + 12, 1, big);
                expected = type == 3 || (start ? (type == 0 || type == 2 || type == 4) : (type == 1 || type == 5));
                CHECK(mm_scene_valid_events(&g, big, start, &s) == expected);
                ++g.sizes[0];
                CHECK(!mm_scene_valid_events(&g, big, start, &s));
            }
    for (big = 0; big < 2; ++big)
    {
        init_group(&g, &h, 0, 1, 36, big);
        put32(g.payload[0] + 12, 0, big);
        CHECK(!mm_scene_valid_events(&g, big, 0, &s));
        put32(g.payload[0] + 12, 0xffffffffUL, big);
        CHECK(!mm_scene_valid_events(&g, big, 0, &s));
        put32(g.payload[0] + 12, 0x7fffffffUL, big);
        CHECK(!mm_scene_valid_events(&g, big, 0, &s));
        put32(g.payload[0] + 12, 2, big);
        g.sizes[0] = 56;
        CHECK(mm_scene_valid_events(&g, big, 0, &s));
        g.sizes[0] = 16;
        CHECK(!mm_scene_valid_events(&g, big, 0, &s));
        g.sizes[0] = -1;
        CHECK(!mm_scene_valid_events(&g, big, 0, &s));
        init_group(&g, &h, 0, 99, 0, big);
        CHECK(!mm_scene_valid_events(&g, big, 0, &s));
        g.count = 0;
        CHECK(mm_scene_valid_events(&g, big, 0, &s));
        g.fixed_width = 4;
        CHECK(!mm_scene_valid_events(&g, big, 0, &s));
    }
}
static MMU32 wire_size(void *g)
{
    (void)g;
    return 32;
}
static int previous(void *c, MMU32 scene, int start, MMU32 *out)
{
    Host *h = (Host *)c;
    CHECK(scene == 7);
    event(h, start ? 'q' : 'p');
    *out = 10 + start;
    return h->have_old;
}
static int add(void *c, int start, MMU32 bytes, MMU32 *out)
{
    Host *h = (Host *)c;
    CHECK(bytes == 32);
    event(h, start ? 'b' : 'a');
    *out = 20 + start;
    h->active = start;
    if (h->stage == (start ? 3 : 1))
        return 0;
    h->created[start] = 1;
    return 1;
}
static int write_group(void *c, void *g)
{
    Host *h = (Host *)c;
    CHECK(((Group *)g)->start == h->active);
    event(h, h->active ? 'x' : 'w');
    return h->stage != (h->active ? 4 : 2);
}
static int adopt(void *c, MMU32 scene, int start, MMU32 n)
{
    Host *h = (Host *)c;
    CHECK(scene == 7 && n == (MMU32)(20 + start) && h->created[start]);
    event(h, start ? 'B' : 'A');
    if (h->stage == (start ? 6 : 5))
        return 0;
    h->linked[start] = 1;
    return 1;
}
static void remove_child(void *c, MMU32 scene, int start, MMU32 n, int linked)
{
    Host *h = (Host *)c;
    CHECK(scene == 7);
    if (n == (MMU32)(10 + start))
    {
        CHECK(linked && h->linked[0] && h->linked[1]);
        event(h, start ? 'O' : 'o');
        h->old[start] = 0;
        return;
    }
    CHECK(n == (MMU32)(20 + start) && h->created[start] && linked == h->linked[start]);
    event(h, start ? (linked ? 'E' : 'e') : (linked ? 'D' : 'd'));
    h->created[start] = h->linked[start] = 0;
}
static void clear_loner(void *c, int start, MMU32 n)
{
    Host *h = (Host *)c;
    CHECK(n == (MMU32)(20 + start) && h->linked[start]);
    event(h, start ? 'L' : 'l');
    h->loner[start] = 1;
}
static MMSceneEventIO events(Host *h)
{
    MMSceneEventIO s;
    s.context = h;
    s.groups = group_services();
    s.wire_size = wire_size;
    s.previous = previous;
    s.add = add;
    s.write = write_group;
    s.adopt = adopt;
    s.remove = remove_child;
    s.clear_loner = clear_loner;
    return s;
}
static void transactions(void)
{
    Host h;
    Group frames, starts;
    MMSceneEventIO s;
    int stage;
    const unsigned short one = 1;
    int big = *(const unsigned char *)&one == 0;
    const char *logs[] = {"pqawbxABoOlL", "pqa", "pqawd", "pqawbd", "pqawbxde", "pqawbxAde", "pqawbxABDe"};
    reset(&h);
    s = events(&h);
    init_group(&frames, &h, 0, 3, 4, big);
    init_group(&starts, &h, 1, 3, 4, big);
    CHECK(mm_scene_write_events(7, &frames, &starts, &s) && !strcmp(h.log, logs[0]) && !h.old[0] && !h.old[1] &&
          h.loner[0] && h.loner[1]);
    for (stage = 1; stage <= 6; ++stage)
    {
        reset(&h);
        h.stage = stage;
        CHECK(!mm_scene_write_events(7, &frames, &starts, &s));
        CHECK(!strcmp(h.log, logs[stage]) && h.old[0] && h.old[1] && !h.created[0] && !h.created[1] && !h.linked[0] &&
              !h.linked[1] && !h.loner[0] && !h.loner[1]);
    }
    reset(&h);
    h.have_old = 0;
    CHECK(mm_scene_write_events(7, &frames, &starts, &s) && !strcmp(h.log, "pqawbxABlL"));
    reset(&h);
    frames.sizes[0] = 5;
    CHECK(!mm_scene_write_events(7, &frames, &starts, &s) && !h.used);
}
static int read_at(void *c, MMU32 offset, void *out, size_t bytes)
{
    Host *h = (Host *)c;
    if (h->io_fail)
        return 0;
    CHECK(offset + bytes <= 16);
    memcpy(out, h->header + offset, bytes);
    return 1;
}
static int read_scene(void *c, MMU32 n, MMReadReceiver receive, void *out)
{
    Host *h = (Host *)c;
    MMReadStream stream;
    CHECK(n == 7);
    event(h, 'R');
    stream.context = h;
    stream.length = h->length;
    stream.read_at = read_at;
    return !h->read_fail && receive(out, &stream);
}
static void load_name(void *c, MMU32 n, MMMovieTitle *name)
{
    Host *h = (Host *)c;
    CHECK(n == 7 && !name->count);
    event(h, 'N');
    set_text(name, "Loaded");
}
static int put_header(void *c, MMU32 n, const unsigned char *bytes)
{
    Host *h = (Host *)c;
    CHECK(n == 7);
    event(h, 'h');
    if (h->root_fail == 2)
        return 0;
    memcpy(h->header, bytes, 16);
    return 1;
}
static int set_name(void *c, MMU32 n, const MMMovieTitle *name)
{
    Host *h = (Host *)c;
    CHECK(n == 7 && name->count == 4);
    event(h, 'n');
    return h->root_fail != 3;
}
static MMSceneIO scene_io(Host *h)
{
    MMSceneIO io = {NULL, read_scene, load_name, put_header, set_name};
    io.context = h;
    return io;
}
static void headers(void)
{
    Host h;
    MMSceneIO io;
    MMSceneState state, old, loaded;
    MMReadStream stream;
    unsigned char bytes[16];
    int big;
    unsigned unit;
    reset(&h);
    io = scene_io(&h);
    stream.context = &h;
    stream.length = 16;
    stream.read_at = read_at;
    for (unit = 1; unit <= 2; ++unit)
        for (big = 0; big < 2; ++big)
        {
            CHECK(mm_scene_init(&state, unit));
            state.firstFrame = -3;
            state.lastFrame = 18;
            state.currentFrame = 99;
            state.transition = 3;
            set_text(&state.name, "Name");
            CHECK(mm_scene_write(&state, 7, 0x0303, big, &io));
            CHECK(mm_scene_init(&loaded, unit) && mm_scene_read(&loaded, 7, &io));
            CHECK(loaded.currentFrame == -4 && loaded.firstFrame == -3 && loaded.lastFrame == 18 &&
                  loaded.transition == 3 && loaded.name.count == 6 && loaded.name.unit == unit);
            old = loaded;
            h.read_fail = 1;
            CHECK(!mm_scene_read(&loaded, 7, &io) && !memcmp(&old, &loaded, sizeof(old)));
            h.read_fail = 0;
            h.io_fail = 1;
            CHECK(!mm_scene_read(&loaded, 7, &io) && !memcmp(&old, &loaded, sizeof(old)));
            h.io_fail = 0;
            h.header[0] = h.header[1] = 0;
            CHECK(!mm_scene_read(&loaded, 7, &io) && !memcmp(&old, &loaded, sizeof(old)));
            CHECK(mm_scene_encode_header(&state, 0x0303, big, h.header));
            put32(h.header + 8, 0x80000000UL, big);
            CHECK(!mm_scene_decode_header(&stream, &loaded) && !memcmp(&old, &loaded, sizeof(old)));
            CHECK(mm_scene_encode_header(&state, 0x0303, big, h.header));
            put32(h.header + 4, 0xfffffffcUL, big);
            CHECK(!mm_scene_decode_header(&stream, &loaded));
            CHECK(mm_scene_encode_header(&state, 0x0303, big, h.header));
            put32(h.header + 12, 5, big);
            CHECK(!mm_scene_decode_header(&stream, &loaded));
            state.firstFrame = (-2147483647L - 1);
            memset(bytes, 0x5a, 16);
            CHECK(!mm_scene_encode_header(&state, 0x0303, big, bytes) && bytes[0] == 0x5a);
            state.firstFrame = -2147483647L;
            state.lastFrame = 0;
            state.transition = -1;
            CHECK(mm_scene_encode_header(&state, 0x0303, big, h.header));
            CHECK(mm_scene_decode_header(&stream, &loaded) && loaded.currentFrame == (-2147483647L - 1));
            state.firstFrame = 1;
            state.lastFrame = 0;
            CHECK(!mm_scene_encode_header(&state, 0x0303, big, bytes));
            state.lastFrame = 1;
            state.transition = -2;
            CHECK(!mm_scene_encode_header(&state, 0x0303, big, bytes));
            state.transition = 0;
            h.root_fail = 2;
            h.used = 0;
            h.log[0] = 0;
            CHECK(!mm_scene_write(&state, 7, 0x0303, big, &io) && !strcmp(h.log, "h"));
            h.root_fail = 3;
            h.used = 0;
            h.log[0] = 0;
            CHECK(!mm_scene_write(&state, 7, 0x0303, big, &io) && !strcmp(h.log, "hn"));
            h.root_fail = 0;
        }
}
static void *new_group(void *c)
{
    Host *h = (Host *)c;
    Group *g;
    int index = h->group_calls++;
    CHECK(h->number && *h->number == 99);
    event(h, index ? '2' : '1');
    if (h->group_fail == index + 1)
        return NULL;
    g = (Group *)calloc(1, sizeof(*g));
    CHECK(g != NULL);
    g->host = h;
    g->start = index;
    g->fixed_width = 8;
    ++h->groups;
    return g;
}
static void release_group(void *p)
{
    Group *g = (Group *)p;
    event(g->host, g->start ? 's' : 'r');
    --g->host->groups;
    free(g);
}
static int add_scene(void *c, MMU32 bytes, MMU32 *number)
{
    Host *h = (Host *)c;
    CHECK(bytes == 16 && *number == 0xffffffffUL);
    event(h, 'c');
    *number = 7;
    if (h->root_fail == 1)
        return 0;
    h->root = 1;
    return 1;
}
static void remove_scene(void *c, MMU32 n)
{
    Host *h = (Host *)c;
    CHECK(n == 7 && *h->number == 7 && !h->groups && h->root);
    event(h, 'X');
    h->root = 0;
}
static void creation(void)
{
    Host h;
    MMSceneState state;
    MMSceneCreateServices s;
    MMU32 number;
    int i;
    reset(&h);
    s.io = scene_io(&h);
    s.events = events(&h);
    s.new_group = new_group;
    s.release_group = release_group;
    s.add_scene = add_scene;
    s.remove_scene = remove_scene;
    CHECK(mm_scene_init(&state, 1));
    set_text(&state.name, "Name");
    h.number = &number;
    number = 99;
    h.have_old = 0;
    CHECK(mm_scene_create(&state, &number, 0x0303, 0, &s) && number == 7 && !h.groups && h.root &&
          !strcmp(h.log, "12chnpqawbxABlLrs"));
    for (i = 1; i <= 2; ++i)
    {
        reset(&h);
        h.number = &number;
        number = 99;
        h.group_fail = i;
        CHECK(!mm_scene_create(&state, &number, 0x0303, 0, &s) && number == 0xffffffffUL && !h.groups && !h.root);
        CHECK(!strcmp(h.log, i == 1 ? "12s" : "12r"));
    }
    for (i = 1; i <= 3; ++i)
    {
        reset(&h);
        h.number = &number;
        number = 99;
        h.root_fail = i;
        CHECK(!mm_scene_create(&state, &number, 0x0303, 0, &s) && number == 0xffffffffUL && !h.groups && !h.root);
        CHECK(!strcmp(h.log, i == 1 ? "12crs" : i == 2 ? "12chrsX" : "12chnrsX"));
    }
    for (i = 1; i <= 6; ++i)
    {
        reset(&h);
        h.number = &number;
        number = 99;
        h.stage = i;
        h.have_old = 0;
        CHECK(!mm_scene_create(&state, &number, 0x0303, 0, &s) && number == 0xffffffffUL && !h.groups &&
              !h.root && !h.created[0] && !h.created[1]);
    }
}
static void *read_event_group(void *p, int *order)
{
    Host *h = (Host *)p;
    event(h, 'r');
    *order = h->read_order;
    return h->read_fail_group ? NULL : h->read_group;
}
static void release_event_group(void *p, void *group)
{
    Host *h = (Host *)p;
    CHECK(group == h->read_group);
    ++h->read_releases;
    event(h, 'x');
}
static void group_reading(void)
{
    Host h;
    Group group;
    MMSceneReadGroupServices s;
    int native_big, swapped, start, order, big, failure;
    s.context = &h;
    s.groups = group_services();
    s.read = read_event_group;
    s.release = release_event_group;
    for (native_big = 0; native_big < 2; ++native_big)
        for (swapped = 0; swapped < 2; ++swapped)
            for (start = 0; start < 2; ++start)
                for (failure = 0; failure < 4; ++failure)
                {
                    reset(&h);
                    h.read_group = &group;
                    h.read_order = swapped ? MM_SCENE_GROUP_OTHER_ORDER : MM_SCENE_GROUP_NATIVE_ORDER;
                    big = native_big != swapped;
                    init_group(&group, &h, start, MM_SCENE_CHANGE_CAMERA, 4, big);
                    order = 77;
                    if (failure == 1)
                        h.read_fail_group = 1;
                    if (failure == 2)
                        h.read_order = 999;
                    if (failure == 3)
                        group.sizes[0] = 5;
                    if (!failure)
                    {
                        unsigned char original[8];
                        memcpy(original, group.fixed[0], 8);
                        CHECK(mm_scene_read_event_group(start, native_big, &order, &s) == &group);
                        CHECK(order == h.read_order && !h.read_releases && !strcmp(h.log, "r"));
                        CHECK(!memcmp(original, group.fixed[0], 8));
                        CHECK(mm_scene_read_event_group(start, native_big, NULL, &s) == &group &&
                              !h.read_releases);
                    }
                    else
                    {
                        CHECK(!mm_scene_read_event_group(start, native_big, &order, &s));
                        CHECK(order == 77 && h.read_releases == (failure != 1));
                        CHECK(!strcmp(h.log, failure == 1 ? "r" : "rx"));
                    }
                }
    reset(&h);
    h.read_group = &group;
    h.read_order = MM_SCENE_GROUP_NATIVE_ORDER;
    init_group(&group, &h, 1, MM_SCENE_ADD_ACTOR, 4, 0);
    order = 77;
    CHECK(!mm_scene_read_event_group(0, 0, &order, &s) && order == 77 && h.read_releases == 1);
    reset(&h);
    h.read_group = &group;
    h.read_order = MM_SCENE_GROUP_NATIVE_ORDER;
    init_group(&group, &h, 0, 99, 7, 0);
    order = 77;
    CHECK(mm_scene_read_event_group(MM_SCENE_GROUP_HEADERS_ONLY, 0, &order, &s) == &group);
    CHECK(order == MM_SCENE_GROUP_NATIVE_ORDER && !h.read_releases);
    group.fixed_width = 7;
    order = 77;
    CHECK(!mm_scene_read_event_group(MM_SCENE_GROUP_HEADERS_ONLY, 0, &order, &s));
    CHECK(order == 77 && h.read_releases == 1);
}
typedef struct StartOutput
{
    unsigned char bytes[96];
    MMU32 size;
    int accept;
} StartOutput;

static int capture_start(void *context, const unsigned char *bytes, MMU32 size)
{
    StartOutput *out = (StartOutput *)context;
    CHECK(size <= sizeof(out->bytes));
    memcpy(out->bytes, bytes, size);
    out->size = size;
    return out->accept;
}

static void append_actor(void)
{
    unsigned char input[60], row[12];
    MMCollectionHeader header;
    MMMemoryStream source, result;
    StartOutput out;
    MMU32 offset;
    MMI32 bytes;
    int big;
    for (big = 0; big <= 1; ++big)
    {
        memset(&header, 0, sizeof(header));
        header.kind = MM_COLLECTION_GROUP;
        header.system = MM_TEXT_MAC;
        header.big_endian = big;
        header.width = MM_SCENE_EVENT_BYTES;
        header.bytes = 24;
        header.count = 2;
        header.free_count = -1;
        memset(input, 0, sizeof(input));
        CHECK(mm_collection_header_write(&header, input, sizeof(input)) == MM_STORAGE_OK);
        put32(input + 24, MM_SCENE_CHANGE_CAMERA, big);
        put32(input + 28, 2, big);
        put32(input + 36, MM_SCENE_ADD_TEXT, big);
        put32(input + 40, 3, big);
        /* The dense group's row order can differ from its physical order. */
        put32(input + 44, 12, big);
        put32(input + 48, 12, big);
        put32(input + 56, 12, big);
        CHECK(mm_memory_stream_init(&source, input, sizeof(input)));
        out.accept = 1;
        CHECK(mm_scene_append_start_actor(&source.stream, MM_TEXT_WINDOWS, -2, 7, capture_start, &out) ==
              MM_MOVIE_OK);
        CHECK(out.size == 80 && mm_memory_stream_init(&result, out.bytes, out.size));
        CHECK(mm_collection_header_read(&result.stream, MM_COLLECTION_GROUP, 0, &header) == MM_STORAGE_OK);
        CHECK(header.system == MM_TEXT_WINDOWS && header.big_endian == big && header.count == 3);
        CHECK(!memcmp(input + 20, out.bytes + 20, 24) && !memcmp(input + 44, out.bytes + 56, 16));
        CHECK(mm_collection_group_row(&header, out.bytes + 20, 2, &offset, &bytes));
        CHECK(offset == 24 && bytes == 4);
        put32(row, (MMU32)-2, big);
        put32(row + 4, MM_SCENE_ADD_ACTOR, big);
        put32(row + 8, 7, big);
        CHECK(!memcmp(row, out.bytes + 20 + offset, sizeof(row)));
        out.accept = 0;
        CHECK(mm_scene_append_start_actor(&source.stream, MM_TEXT_WINDOWS, -2, 7, capture_start, &out) ==
              MM_MOVIE_IO);
        out.size = 0;
        put32(input + 48, 13, big);
        CHECK(mm_scene_append_start_actor(&source.stream, MM_TEXT_WINDOWS, -2, 7, capture_start, &out) ==
              MM_MOVIE_BAD_FILE && out.size == 0);
    }
}

enum { FRAME_WIRE_BYTES = 20 + 68 + 24 };
#define SCEN 0x5343454eUL
#define GGFR 0x47474652UL
#define THUM 0x5448554dUL

typedef struct ArchivedFrames
{
    MMMemoryStream reader;
    unsigned char scene[16], frames[FRAME_WIRE_BYTES], output[FRAME_WIRE_BYTES + 20];
    MMU32 output_size;
    int read_frames, header_written, linked, unlinked, thumbnail_removed, fail_write;
    int include_movie, linked_scene_sound, linked_movie_sound;
} ArchivedFrames;

static int scene_entry(void *context, MMU32 i, MMChunkEntry *out)
{
    ArchivedFrames *archive = (ArchivedFrames *)context;
    CHECK(i <= (MMU32)archive->include_movie);
    memset(out, 0, sizeof(*out));
    if (archive->include_movie && i == 0)
    {
        out->type = MM_SOUND_MOVIE;
        out->number = 2;
        out->child_count = 1;
        return 1;
    }
    out->type = SCEN;
    out->number = 5;
    out->child_count = archive->include_movie ? 4 : 2;
    return 1;
}
static int scene_child(void *context, MMU32 parent, MMU32 i, MMChunkChild *out)
{
    ArchivedFrames *archive = (ArchivedFrames *)context;
    if (archive->include_movie)
    {
        CHECK(parent <= 1);
        if (parent == 0 || i == 1 || i == 3)
        {
            CHECK(parent == 1 || i == 0);
            out->type = MM_SOUND_CHUNK;
            out->number = 8;
            out->slot = parent == 0 ? 1 : i == 1 ? 0 : 3;
            return 1;
        }
        CHECK(i == 0 || i == 2);
        i /= 2;
    }
    else
        CHECK(parent == 0 && i < 2);
    out->type = i == 0 ? GGFR : THUM;
    out->number = i == 0 ? 10 : 11;
    out->slot = 0;
    return 1;
}
static MMChunkAccess scene_chunks(void *context)
{
    MMChunkAccess chunks = {NULL, 1, scene_entry, scene_child};
    chunks.context = context;
    chunks.count += ((ArchivedFrames *)context)->include_movie;
    return chunks;
}
static int read_archive(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    ArchivedFrames *archive = (ArchivedFrames *)context;
    if (type == SCEN)
        CHECK(number == 5 && mm_memory_stream_init(&archive->reader, archive->scene, sizeof(archive->scene)));
    else
    {
        CHECK(type == GGFR && number == 10);
        ++archive->read_frames;
        CHECK(mm_memory_stream_init(&archive->reader, archive->frames, sizeof(archive->frames)));
    }
    *stream = archive->reader.stream;
    return 1;
}
static int write_frames(void *context, MMU32 offset, const void *bytes, size_t size)
{
    ArchivedFrames *archive = (ArchivedFrames *)context;
    CHECK(offset <= sizeof(archive->output) && size <= sizeof(archive->output) - offset);
    if (archive->fail_write && offset > 0)
        return 0;
    memcpy(archive->output + offset, bytes, size);
    return 1;
}
static int add_frames(void *context, MMU32 type, MMU32 size, MMU32 *number, MMStorageWriter *writer)
{
    ArchivedFrames *archive = (ArchivedFrames *)context;
    CHECK(type == GGFR && size <= sizeof(archive->output));
    archive->output_size = size;
    *number = 12;
    writer->context = context;
    writer->length = size;
    writer->write_at = write_frames;
    return 1;
}
static void unlink_archive(void *context, MMU32 type, MMU32 scene, MMU32 child, MMU32 number, MMU32 slot)
{
    ArchivedFrames *archive = (ArchivedFrames *)context;
    CHECK(type == SCEN && scene == 5 && slot == 0);
    if (child == GGFR)
    {
        CHECK(number == 10 && !archive->linked);
        ++archive->unlinked;
    }
    else
    {
        CHECK(child == THUM && number == 11 && archive->linked);
        ++archive->thumbnail_removed;
    }
}
static int link_frames(void *context, MMU32 type, MMU32 scene, MMU32 child, MMU32 number, MMU32 slot)
{
    ArchivedFrames *archive = (ArchivedFrames *)context;
    if (child == MM_SOUND_CHUNK)
    {
        CHECK(archive->include_movie && number == 9);
        if (type == SCEN)
        {
            CHECK(scene == 5 && slot == 1 && !archive->linked_movie_sound);
            ++archive->linked_scene_sound;
        }
        else
        {
            CHECK(type == MM_SOUND_MOVIE && scene == 2 && slot == 0 && archive->linked_scene_sound);
            ++archive->linked_movie_sound;
        }
        return 1;
    }
    CHECK(type == SCEN && scene == 5 && child == GGFR && number == 12 && slot == 0 && archive->unlinked);
    ++archive->linked;
    return 1;
}
static int put_range(void *context, MMU32 type, MMU32 number, const void *bytes, MMU32 size)
{
    ArchivedFrames *archive = (ArchivedFrames *)context;
    CHECK(type == SCEN && number == 5 && size == sizeof(archive->scene));
    memcpy(archive->scene, bytes, size);
    ++archive->header_written;
    return 1;
}

static void init_archive(ArchivedFrames *archive, int big)
{
    MMCollectionHeader header = {0};
    MMSceneState scene;
    unsigned char *p;
    memset(archive, 0, sizeof(*archive));
    CHECK(mm_scene_init(&scene, 1));
    scene.lastFrame = 5;
    scene.transition = 2;
    CHECK(mm_scene_encode_header(&scene, MM_TEXT_MAC, big, archive->scene));
    header.kind = MM_COLLECTION_GROUP;
    header.system = MM_TEXT_MAC;
    header.big_endian = big;
    header.width = 8;
    header.count = 3;
    header.bytes = 68;
    header.free_count = -1;
    CHECK(mm_collection_header_write(&header, archive->frames, sizeof(archive->frames)) == MM_STORAGE_OK);
    p = archive->frames + 20;
    put32(p, 5, big);
    put32(p + 4, MM_SCENE_CHANGE_CAMERA, big);
    put32(p + 8, 3, big);
    put32(p + 12, 1, big);
    put32(p + 16, MM_SCENE_PLAY_SOUND, big);
    memset(p + 20, 0x5a, 36);
    put32(p + 32, 1, big);
    put32(p + 56, 1, big);
    put32(p + 60, MM_SCENE_CHANGE_CAMERA, big);
    put32(p + 64, 2, big);
    put32(p + 68, 12, big);
    put32(p + 72, 44, big);
    put32(p + 76, 56, big);
    put32(p + 80, 12, big);
    put32(p + 88, 12, big);
}

static MMU32 saved_frame_row(ArchivedFrames *archive, int big, MMU32 index, MMI32 bytes)
{
    MMMemoryStream stream;
    MMCollectionHeader header;
    MMU32 offset;
    MMI32 size;
    CHECK(mm_memory_stream_init(&stream, archive->output, archive->output_size));
    CHECK(mm_collection_header_read(&stream.stream, MM_COLLECTION_GROUP, 0, &header) == MM_STORAGE_OK);
    CHECK(header.big_endian == big && header.system == MM_TEXT_WINDOWS && header.count == 3);
    CHECK(mm_collection_group_row(&header, archive->output + 20, index, &offset, &size) && size == bytes);
    return 20 + offset;
}

static void edit_archived_frames(void)
{
    ArchivedFrames archive;
    MMMovieArchiveEditor editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, scene_chunks, read_archive,
                                   add_frames, link_frames, unlink_archive, put_range, NULL, NULL, NULL, NULL, NULL};
    MMSceneState scene;
    MMMemoryStream stream;
    unsigned char value[4];
    MMU32 row;
    int big;
    editor.context = &archive;
    for (big = 0; big <= 1; ++big)
    {
        init_archive(&archive, big);
        CHECK(mm_scene_archive_set_camera(&editor, 5, 5, 7) == MM_SCENE_ARCHIVE_EDIT_OK);
        CHECK(archive.linked == 1 && archive.unlinked == 1 && !archive.thumbnail_removed && !archive.header_written);
        row = saved_frame_row(&archive, big, 0, 36);
        CHECK(row == 20 && !memcmp(archive.frames + 32, archive.output + row, 44));
        row = saved_frame_row(&archive, big, 2, 4);
        put32(value, 7, big);
        CHECK(row == 76 && !memcmp(archive.output + row + 8, value, 4));

        init_archive(&archive, big);
        CHECK(mm_scene_archive_set_camera(&editor, 5, MM_SCENE_OPENING_CAMERA_FRAME, 8) == MM_SCENE_ARCHIVE_EDIT_OK);
        CHECK(archive.linked == 1 && archive.thumbnail_removed == 1 && !archive.header_written);
        row = saved_frame_row(&archive, big, 1, 4);
        put32(value, 8, big);
        CHECK(!memcmp(archive.output + row + 8, value, 4));

        init_archive(&archive, big);
        CHECK(mm_scene_archive_set_range(&editor, 5, -2, 9) == MM_SCENE_ARCHIVE_EDIT_OK);
        CHECK(archive.header_written == 1 && archive.linked == 1 && archive.thumbnail_removed == 1);
        row = saved_frame_row(&archive, big, 0, 4);
        put32(value, (MMU32)-2, big);
        CHECK(row == 76 && !memcmp(archive.output + row, value, 4));
        row = saved_frame_row(&archive, big, 1, 36);
        CHECK(!memcmp(archive.frames + 32, archive.output + row, 44));
        CHECK(mm_scene_init(&scene, 1) && mm_memory_stream_init(&stream, archive.scene, sizeof(archive.scene)));
        CHECK(mm_scene_decode_header(&stream.stream, &scene) && scene.firstFrame == -2 &&
              scene.lastFrame == 9 && scene.transition == 2);

        init_archive(&archive, big);
        CHECK(mm_scene_archive_set_range(&editor, 5, 2, 9) == MM_SCENE_ARCHIVE_EDIT_RANGE);
        CHECK(!archive.read_frames && !archive.header_written);
        CHECK(mm_scene_archive_set_range(&editor, 5, 1, 9) == MM_SCENE_ARCHIVE_EDIT_OK);
        CHECK(!archive.read_frames && archive.header_written == 1 && !archive.linked && !archive.thumbnail_removed);

        init_archive(&archive, big);
        archive.fail_write = 1;
        CHECK(mm_scene_archive_set_range(&editor, 5, -2, 9) == MM_SCENE_ARCHIVE_EDIT_WRITE_FRAMES);
        CHECK(!archive.header_written && !archive.linked && !archive.unlinked && !archive.thumbnail_removed);
        put32(archive.frames + 88, 0x7fffffffUL, big);
        CHECK(mm_scene_archive_set_camera(&editor, 5, 5, 7) == MM_SCENE_ARCHIVE_EDIT_INVALID_FRAMES);
    }
}

static void init_archive_sound(ArchivedFrames *archive, int big)
{
    unsigned char *packet;
    init_archive(archive, big);
    packet = archive->frames + 40;
    put32(packet, 32768, big);
    put32(packet + 4, 2, big);
    put32(packet + 8, 0, big);
    put32(packet + 16, 3, big);
    put32(packet + 20, 4, big);
    put32(packet + 28, MM_SOUND_CHUNK, big);
    put32(packet + 32, 8, big);
}

static void edit_archived_sounds(void)
{
    ArchivedFrames archive;
    MMMovieArchiveEditor editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, scene_chunks, read_archive,
                                   add_frames, link_frames, unlink_archive, put_range, NULL, NULL, NULL, NULL, NULL};
    MMMovieSoundArchiveCue cue = {1, 4, 0, 2, -1, 3, 8, 0, 0};
    MMMovieSoundArchiveEdit edit = {NULL, NULL, 5, 12000, 1, 1, 0, 0};
    MMU32 offset, added = 9;
    unsigned char expected[44], tag[20] = {0};
    int big;
    editor.context = &archive;
    for (big = 0; big <= 1; ++big)
    {
        init_archive_sound(&archive, big);
        edit.cue = &cue;
        edit.added = NULL;
        edit.frame = 5; edit.style = 1; edit.loop = 1; edit.queue = 0;
        cue.number = 800;
        CHECK(mm_movie_archive_edit_sound(&editor, 5, &edit) == MM_SOUND_ARCHIVE_NO_CUE && !archive.output_size);
        cue.number = 8;
        CHECK(mm_movie_archive_edit_sound(&editor, 5, &edit) == MM_SOUND_ARCHIVE_OK);
        CHECK(archive.linked == 1 && !archive.thumbnail_removed);
        offset = saved_frame_row(&archive, big, 1, 36);
        memcpy(expected, archive.frames + 32, sizeof(expected));
        put32(expected, 5, big);
        put32(expected + 8, 12000, big);
        put32(expected + 12, 1, big);
        put32(expected + 16, 1, big);
        put32(expected + 32, 0, big);
        CHECK(offset == 32 && !memcmp(archive.output + offset, expected, sizeof(expected)));
        offset = saved_frame_row(&archive, big, 2, 4);
        CHECK(offset == 76 && !memcmp(archive.output + offset, archive.frames + 20, 12));

        init_archive_sound(&archive, big);
        archive.include_movie = 1;
        edit.cue = NULL;
        edit.added = &added;
        edit.frame = 1; edit.style = 2; edit.loop = 0; edit.queue = 1;
        CHECK(mm_movie_archive_edit_sound(&editor, 5, &edit) == MM_SOUND_ARCHIVE_OK);
        CHECK(archive.linked == 1 && archive.linked_scene_sound == 1 && archive.linked_movie_sound == 1);
        offset = saved_frame_row(&archive, big, 0, 56);
        put32(tag, 1, big); put32(tag + 12, MM_SOUND_CHUNK, big); put32(tag + 16, 9, big);
        CHECK(offset == 20 && !memcmp(archive.output + offset + 44, tag, sizeof(tag)));
    }
}

#define GGST 0x47475354UL

typedef struct ArchivedText
{
    MMMemoryStream read;
    unsigned char original[100], written[120];
    int writes, box_link, start_link, box_unlink, start_unlink, fail_write;
    MMU32 size, adopted_slot, removed_slot;
} ArchivedText;

static int text_entry(void *context, MMU32 i, MMChunkEntry *out)
{
    (void)context;
    CHECK(i == 0);
    memset(out, 0, sizeof(*out));
    out->type = SCEN;
    out->number = 5;
    out->child_count = 5;
    return 1;
}

static int text_child(void *context, MMU32 parent, MMU32 i, MMChunkChild *out)
{
    static const MMChunkChild children[] = {
        {0x41435452UL, 4, 0}, {GGST, 6, 1}, {MM_TEXT_BOX, 9, 1}, {MM_TEXT_BOX, 12, 4}, {MM_TEXT_BOX, 13, 4}
    };
    (void)context;
    CHECK(parent == 0 && i < sizeof(children) / sizeof(children[0]));
    *out = children[i];
    return 1;
}

static MMChunkAccess text_chunks(void *context)
{
    MMChunkAccess chunks = {NULL, 1, text_entry, text_child};
    chunks.context = context;
    return chunks;
}

static int text_read(void *context, MMU32 type, MMU32 number, MMReadStream *stream)
{
    ArchivedText *f = (ArchivedText *)context;
    CHECK(type == GGST && number == 6 && mm_memory_stream_init(&f->read, f->original, sizeof(f->original)));
    *stream = f->read.stream;
    return 1;
}

static int text_write(void *context, MMU32 offset, const void *data, size_t bytes)
{
    ArchivedText *f = (ArchivedText *)context;
    CHECK(offset <= f->size && bytes <= f->size - offset);
    if (f->fail_write)
        return 0;
    memcpy(f->written + offset, data, bytes);
    return 1;
}

static int text_add(void *context, MMU32 type, MMU32 bytes, MMU32 *number, MMStorageWriter *writer)
{
    ArchivedText *f = (ArchivedText *)context;
    CHECK(type == GGST && bytes <= sizeof(f->written));
    f->size = bytes;
    *number = 7;
    writer->context = f;
    writer->length = bytes;
    writer->write_at = text_write;
    return 1;
}

static int text_adopt(void *context, MMU32 type, MMU32 scene, MMU32 child, MMU32 number, MMU32 slot)
{
    ArchivedText *f = (ArchivedText *)context;
    CHECK(type == SCEN && scene == 5);
    if (child == GGST)
    {
        CHECK(number == 7 && slot == 1 && !f->start_unlink);
        ++f->start_link;
    }
    else
    {
        CHECK(child == MM_TEXT_BOX && number == 14 && f->writes == 1 && !f->box_unlink);
        f->adopted_slot = slot;
        ++f->box_link;
    }
    return 1;
}

static void text_unlink(void *context, MMU32 type, MMU32 scene, MMU32 child, MMU32 number, MMU32 slot)
{
    ArchivedText *f = (ArchivedText *)context;
    CHECK(type == SCEN && scene == 5);
    if (child == GGST)
    {
        CHECK(number == 6 && slot == 1 && f->start_link == 1);
        ++f->start_unlink;
    }
    else
    {
        CHECK(child == MM_TEXT_BOX && ((number == 9 && slot == 1) || (number == 12 && slot == 4)));
        CHECK(f->start_unlink == 1 || (f->box_link == 1 && f->adopted_slot == slot));
        f->removed_slot = slot;
        ++f->box_unlink;
    }
}

static int text_box(void *context, const MMSceneArchiveText *original, MMU32 *number)
{
    ArchivedText *f = (ArchivedText *)context;
    CHECK(!original || (original->number == 9 && original->slot == 1));
    ++f->writes;
    *number = 14;
    return 1;
}

static void init_text_archive(ArchivedText *f, int big)
{
    static const MMU32 kinds[] = {MM_SCENE_ADD_TEXT, MM_SCENE_CHANGE_CAMERA, MM_SCENE_ADD_TEXT, MM_SCENE_ADD_TEXT};
    static const MMU32 values[] = {4, 3, 1, 4};
    MMCollectionHeader header = {0};
    MMU32 i;
    memset(f, 0, sizeof(*f));
    header.kind = MM_COLLECTION_GROUP;
    header.system = MM_TEXT_MAC;
    header.big_endian = big;
    header.width = MM_SCENE_EVENT_BYTES;
    header.count = 4;
    header.bytes = 48;
    header.free_count = -1;
    CHECK(mm_collection_header_write(&header, f->original, sizeof(f->original)) == MM_STORAGE_OK);
    for (i = 0; i < 4; ++i)
    {
        put32(f->original + 24 + i * 12, kinds[i], big);
        put32(f->original + 28 + i * 12, values[i], big);
        put32(f->original + 68 + i * 8, i * 12, big);
        put32(f->original + 72 + i * 8, 12, big);
    }
}

static void edit_archived_text(void)
{
    ArchivedText f;
    MMMovieArchiveEditor editor = {NULL, 1, MM_TEXT_WINDOWS, 0, NULL, text_chunks, text_read,
                                   text_add, text_adopt, text_unlink, NULL, NULL, NULL, NULL, NULL, NULL};
    MMSceneArchiveTextEdit add = {5, -2, NULL, NULL, text_box};
    MMSceneArchiveTextEdit change = {5, 0, NULL, NULL, text_box};
    MMSceneArchiveTextEdit remove = {5, 0, NULL, NULL, NULL};
    MMSceneArchiveText one = {1, 9}, four = {4, 12}, stale = {1, 10};
    MMSceneArchiveText *list;
    MMCollectionHeader header;
    MMMemoryStream output;
    unsigned char row[12];
    MMU32 count, offset;
    MMI32 bytes;
    int big;
    editor.context = add.context = change.context = remove.context = &f;
    remove.original = &four;
    for (big = 0; big <= 1; ++big)
    {
        init_text_archive(&f, big);
        CHECK(mm_scene_archive_list_text(&editor, 5, &list, &count) == MM_SCENE_ARCHIVE_TEXT_OK);
        CHECK(count == 2 && list[0].number == 12 && list[0].slot == 4 && list[1].number == 9 && list[1].slot == 1);
        mm_scene_archive_free_text_list(list);
        CHECK(mm_scene_archive_edit_text(&editor, &add) == MM_SCENE_ARCHIVE_TEXT_OK);
        CHECK(f.box_link == 1 && f.adopted_slot == 0 && f.start_link == 1 && f.start_unlink == 1 && !f.box_unlink);
        CHECK(mm_memory_stream_init(&output, f.written, f.size) &&
              mm_collection_header_read(&output.stream, MM_COLLECTION_GROUP, 0, &header) == MM_STORAGE_OK);
        CHECK(header.big_endian == big && header.system == MM_TEXT_WINDOWS && header.count == 5);
        CHECK(!memcmp(f.original + 20, f.written + 20, 48));
        CHECK(mm_collection_group_row(&header, f.written + 20, 4, &offset, &bytes) && bytes == 4);
        put32(row, (MMU32)-2, big);
        put32(row + 4, MM_SCENE_ADD_TEXT, big);
        put32(row + 8, 0, big);
        CHECK(!memcmp(row, f.written + 20 + offset, sizeof(row)));

        init_text_archive(&f, big);
        change.original = &stale;
        CHECK(mm_scene_archive_edit_text(&editor, &change) == MM_SCENE_ARCHIVE_TEXT_CHANGED && !f.writes);
        change.original = &one;
        CHECK(mm_scene_archive_edit_text(&editor, &change) == MM_SCENE_ARCHIVE_TEXT_OK);
        CHECK(f.adopted_slot == 1 && f.removed_slot == 1 && f.box_link == 1 && f.box_unlink == 1 && !f.size);

        init_text_archive(&f, big);
        f.fail_write = 1;
        CHECK(mm_scene_archive_edit_text(&editor, &remove) == MM_SCENE_ARCHIVE_TEXT_WRITE_START);
        CHECK(!f.start_link && !f.start_unlink && !f.box_unlink);
        f.fail_write = 0;
        CHECK(mm_scene_archive_edit_text(&editor, &remove) == MM_SCENE_ARCHIVE_TEXT_OK);
        CHECK(!f.writes && f.start_unlink == 1 && f.removed_slot == 4 && f.box_unlink == 1);
        CHECK(mm_memory_stream_init(&output, f.written, f.size) &&
              mm_collection_header_read(&output.stream, MM_COLLECTION_GROUP, 0, &header) == MM_STORAGE_OK);
        CHECK(header.count == 2 && !memcmp(f.original + 32, f.written + 20, 24));
    }
}

int main(void)
{
    validation();
    transactions();
    headers();
    creation();
    group_reading();
    append_actor();
    edit_archived_frames();
    edit_archived_sounds();
    edit_archived_text();
    puts("scene data passed");
    return 0;
}
