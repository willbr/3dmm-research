#include "three_mm/scene_data.h"
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
static void put32(unsigned char *p, ThreeMMU32 n, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[big ? 3 - i : i] = (unsigned char)(n >> (i * 8));
}
static void set_text(ThreeMMMovieTitle *t, const char *text)
{
    unsigned short wide[256];
    size_t n = strlen(text), i;
    for (i = 0; i < n; ++i)
        wide[i] = (unsigned char)text[i];
    CHECK(three_mm_movie_title_set(t, t->unit == 1 ? (const void *)text : wide, (ThreeMMU32)n));
}
typedef struct Host Host;
typedef struct Group
{
    Host *host;
    int start;
    ThreeMMU32 fixed_width, count;
    ThreeMMI32 sizes[4];
    unsigned char fixed[4][8], payload[4][64];
} Group;
struct Host
{
    char log[128];
    int used, stage, active, have_old, old[2], created[2], linked[2], loner[2];
    int group_calls, group_fail, groups, root_fail, root, read_fail, io_fail;
    ThreeMMU32 *number, length;
    unsigned char header[16];
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
static ThreeMMU32 fixed_width(void *g)
{
    return ((Group *)g)->fixed_width;
}
static ThreeMMU32 count(void *g)
{
    return ((Group *)g)->count;
}
static void fixed(void *g, ThreeMMU32 i, unsigned char *out)
{
    CHECK(i < ((Group *)g)->count);
    memcpy(out, ((Group *)g)->fixed[i], 8);
}
static ThreeMMI32 size(void *g, ThreeMMU32 i)
{
    CHECK(i < ((Group *)g)->count);
    return ((Group *)g)->sizes[i];
}
static const void *data(void *g, ThreeMMU32 i)
{
    CHECK(i < ((Group *)g)->count);
    CHECK(((Group *)g)->sizes[i] >= 16);
    return ((Group *)g)->payload[i];
}
static ThreeMMSceneGroupServices group_services(void)
{
    ThreeMMSceneGroupServices s = {fixed_width, count, fixed, size, data};
    return s;
}
static void init_group(Group *g, Host *h, int start, int type, ThreeMMU32 bytes, int big)
{
    memset(g, 0, sizeof(*g));
    g->host = h;
    g->start = start;
    g->fixed_width = 8;
    g->count = 1;
    g->sizes[0] = bytes;
    put32(g->fixed[0], 0x80000000UL, big);
    put32(g->fixed[0] + 4, (ThreeMMU32)type, big);
}
static void validation(void)
{
    Group g;
    Host h;
    ThreeMMSceneGroupServices s;
    int big, type, start, expected;
    ThreeMMU32 sizes[] = {4, 36, 4, 4, 16, 8};
    s = group_services();
    reset(&h);
    for (big = 0; big < 2; ++big)
        for (type = 0; type < 6; ++type)
            for (start = 0; start < 2; ++start)
            {
                init_group(&g, &h, start, type, sizes[type], big);
                put32(g.payload[0] + 12, 1, big);
                expected = type == 3 || (start ? (type == 0 || type == 2 || type == 4) : (type == 1 || type == 5));
                CHECK(three_mm_scene_valid_events(&g, big, start, &s) == expected);
                ++g.sizes[0];
                CHECK(!three_mm_scene_valid_events(&g, big, start, &s));
            }
    for (big = 0; big < 2; ++big)
    {
        init_group(&g, &h, 0, 1, 36, big);
        put32(g.payload[0] + 12, 0, big);
        CHECK(!three_mm_scene_valid_events(&g, big, 0, &s));
        put32(g.payload[0] + 12, 0xffffffffUL, big);
        CHECK(!three_mm_scene_valid_events(&g, big, 0, &s));
        put32(g.payload[0] + 12, 0x7fffffffUL, big);
        CHECK(!three_mm_scene_valid_events(&g, big, 0, &s));
        put32(g.payload[0] + 12, 2, big);
        g.sizes[0] = 56;
        CHECK(three_mm_scene_valid_events(&g, big, 0, &s));
        g.sizes[0] = 16;
        CHECK(!three_mm_scene_valid_events(&g, big, 0, &s));
        g.sizes[0] = -1;
        CHECK(!three_mm_scene_valid_events(&g, big, 0, &s));
        init_group(&g, &h, 0, 99, 0, big);
        CHECK(!three_mm_scene_valid_events(&g, big, 0, &s));
        g.count = 0;
        CHECK(three_mm_scene_valid_events(&g, big, 0, &s));
        g.fixed_width = 4;
        CHECK(!three_mm_scene_valid_events(&g, big, 0, &s));
    }
}
static ThreeMMU32 wire_size(void *g)
{
    (void)g;
    return 32;
}
static int previous(void *c, ThreeMMU32 scene, int start, ThreeMMU32 *out)
{
    Host *h = (Host *)c;
    CHECK(scene == 7);
    event(h, start ? 'q' : 'p');
    *out = 10 + start;
    return h->have_old;
}
static int add(void *c, int start, ThreeMMU32 bytes, ThreeMMU32 *out)
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
static int adopt(void *c, ThreeMMU32 scene, int start, ThreeMMU32 n)
{
    Host *h = (Host *)c;
    CHECK(scene == 7 && n == (ThreeMMU32)(20 + start) && h->created[start]);
    event(h, start ? 'B' : 'A');
    if (h->stage == (start ? 6 : 5))
        return 0;
    h->linked[start] = 1;
    return 1;
}
static void remove_child(void *c, ThreeMMU32 scene, int start, ThreeMMU32 n, int linked)
{
    Host *h = (Host *)c;
    CHECK(scene == 7);
    if (n == (ThreeMMU32)(10 + start))
    {
        CHECK(linked && h->linked[0] && h->linked[1]);
        event(h, start ? 'O' : 'o');
        h->old[start] = 0;
        return;
    }
    CHECK(n == (ThreeMMU32)(20 + start) && h->created[start] && linked == h->linked[start]);
    event(h, start ? (linked ? 'E' : 'e') : (linked ? 'D' : 'd'));
    h->created[start] = h->linked[start] = 0;
}
static void clear_loner(void *c, int start, ThreeMMU32 n)
{
    Host *h = (Host *)c;
    CHECK(n == (ThreeMMU32)(20 + start) && h->linked[start]);
    event(h, start ? 'L' : 'l');
    h->loner[start] = 1;
}
static ThreeMMSceneEventIO events(Host *h)
{
    ThreeMMSceneEventIO s;
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
    ThreeMMSceneEventIO s;
    int stage;
    const unsigned short one = 1;
    int big = *(const unsigned char *)&one == 0;
    const char *logs[] = {"pqawbxABoOlL", "pqa", "pqawd", "pqawbd", "pqawbxde", "pqawbxAde", "pqawbxABDe"};
    reset(&h);
    s = events(&h);
    init_group(&frames, &h, 0, 3, 4, big);
    init_group(&starts, &h, 1, 3, 4, big);
    CHECK(three_mm_scene_write_events(7, &frames, &starts, &s) && !strcmp(h.log, logs[0]) && !h.old[0] && !h.old[1] &&
          h.loner[0] && h.loner[1]);
    for (stage = 1; stage <= 6; ++stage)
    {
        reset(&h);
        h.stage = stage;
        CHECK(!three_mm_scene_write_events(7, &frames, &starts, &s));
        CHECK(!strcmp(h.log, logs[stage]) && h.old[0] && h.old[1] && !h.created[0] && !h.created[1] && !h.linked[0] &&
              !h.linked[1] && !h.loner[0] && !h.loner[1]);
    }
    reset(&h);
    h.have_old = 0;
    CHECK(three_mm_scene_write_events(7, &frames, &starts, &s) && !strcmp(h.log, "pqawbxABlL"));
    reset(&h);
    frames.sizes[0] = 5;
    CHECK(!three_mm_scene_write_events(7, &frames, &starts, &s) && !h.used);
}
static int read_at(void *c, ThreeMMU32 offset, void *out, size_t bytes)
{
    Host *h = (Host *)c;
    if (h->io_fail)
        return 0;
    CHECK(offset + bytes <= 16);
    memcpy(out, h->header + offset, bytes);
    return 1;
}
static int read_scene(void *c, ThreeMMU32 n, ThreeMMReadStream *stream)
{
    Host *h = (Host *)c;
    CHECK(n == 7);
    event(h, 'R');
    stream->context = h;
    stream->length = h->length;
    stream->read_at = read_at;
    return !h->read_fail;
}
static void load_name(void *c, ThreeMMU32 n, ThreeMMMovieTitle *name)
{
    Host *h = (Host *)c;
    CHECK(n == 7 && !name->count);
    event(h, 'N');
    set_text(name, "Loaded");
}
static int put_header(void *c, ThreeMMU32 n, const unsigned char *bytes)
{
    Host *h = (Host *)c;
    CHECK(n == 7);
    event(h, 'h');
    if (h->root_fail == 2)
        return 0;
    memcpy(h->header, bytes, 16);
    return 1;
}
static int set_name(void *c, ThreeMMU32 n, const ThreeMMMovieTitle *name)
{
    Host *h = (Host *)c;
    CHECK(n == 7 && name->count == 4);
    event(h, 'n');
    return h->root_fail != 3;
}
static ThreeMMSceneIO scene_io(Host *h)
{
    ThreeMMSceneIO io = {NULL, read_scene, load_name, put_header, set_name};
    io.context = h;
    return io;
}
static void headers(void)
{
    Host h;
    ThreeMMSceneIO io;
    ThreeMMSceneState state, old, loaded;
    ThreeMMReadStream stream;
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
            CHECK(three_mm_scene_init(&state, unit));
            state.firstFrame = -3;
            state.lastFrame = 18;
            state.currentFrame = 99;
            state.transition = 3;
            set_text(&state.name, "Name");
            CHECK(three_mm_scene_write(&state, 7, 0x0303, big, &io));
            CHECK(three_mm_scene_init(&loaded, unit) && three_mm_scene_read(&loaded, 7, &io));
            CHECK(loaded.currentFrame == -4 && loaded.firstFrame == -3 && loaded.lastFrame == 18 &&
                  loaded.transition == 3 && loaded.name.count == 6 && loaded.name.unit == unit);
            old = loaded;
            h.read_fail = 1;
            CHECK(!three_mm_scene_read(&loaded, 7, &io) && !memcmp(&old, &loaded, sizeof(old)));
            h.read_fail = 0;
            h.io_fail = 1;
            CHECK(!three_mm_scene_read(&loaded, 7, &io) && !memcmp(&old, &loaded, sizeof(old)));
            h.io_fail = 0;
            h.header[0] = h.header[1] = 0;
            CHECK(!three_mm_scene_read(&loaded, 7, &io) && !memcmp(&old, &loaded, sizeof(old)));
            CHECK(three_mm_scene_encode_header(&state, 0x0303, big, h.header));
            put32(h.header + 8, 0x80000000UL, big);
            CHECK(!three_mm_scene_decode_header(&stream, &loaded) && !memcmp(&old, &loaded, sizeof(old)));
            CHECK(three_mm_scene_encode_header(&state, 0x0303, big, h.header));
            put32(h.header + 4, 0xfffffffcUL, big);
            CHECK(!three_mm_scene_decode_header(&stream, &loaded));
            CHECK(three_mm_scene_encode_header(&state, 0x0303, big, h.header));
            put32(h.header + 12, 5, big);
            CHECK(!three_mm_scene_decode_header(&stream, &loaded));
            state.firstFrame = (-2147483647L - 1);
            memset(bytes, 0x5a, 16);
            CHECK(!three_mm_scene_encode_header(&state, 0x0303, big, bytes) && bytes[0] == 0x5a);
            state.firstFrame = -2147483647L;
            state.lastFrame = 0;
            state.transition = -1;
            CHECK(three_mm_scene_encode_header(&state, 0x0303, big, h.header));
            CHECK(three_mm_scene_decode_header(&stream, &loaded) && loaded.currentFrame == (-2147483647L - 1));
            state.firstFrame = 1;
            state.lastFrame = 0;
            CHECK(!three_mm_scene_encode_header(&state, 0x0303, big, bytes));
            state.lastFrame = 1;
            state.transition = -2;
            CHECK(!three_mm_scene_encode_header(&state, 0x0303, big, bytes));
            state.transition = 0;
            h.root_fail = 2;
            h.used = 0;
            h.log[0] = 0;
            CHECK(!three_mm_scene_write(&state, 7, 0x0303, big, &io) && !strcmp(h.log, "h"));
            h.root_fail = 3;
            h.used = 0;
            h.log[0] = 0;
            CHECK(!three_mm_scene_write(&state, 7, 0x0303, big, &io) && !strcmp(h.log, "hn"));
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
static int add_scene(void *c, ThreeMMU32 bytes, ThreeMMU32 *number)
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
static void remove_scene(void *c, ThreeMMU32 n)
{
    Host *h = (Host *)c;
    CHECK(n == 7 && *h->number == 7 && !h->groups && h->root);
    event(h, 'X');
    h->root = 0;
}
static void creation(void)
{
    Host h;
    ThreeMMSceneState state;
    ThreeMMSceneCreateServices s;
    ThreeMMU32 number;
    int i;
    reset(&h);
    s.io = scene_io(&h);
    s.events = events(&h);
    s.new_group = new_group;
    s.release_group = release_group;
    s.add_scene = add_scene;
    s.remove_scene = remove_scene;
    CHECK(three_mm_scene_init(&state, 1));
    set_text(&state.name, "Name");
    h.number = &number;
    number = 99;
    h.have_old = 0;
    CHECK(three_mm_scene_create(&state, &number, 0x0303, 0, &s) && number == 7 && !h.groups && h.root &&
          !strcmp(h.log, "12chnpqawbxABlLrs"));
    for (i = 1; i <= 2; ++i)
    {
        reset(&h);
        h.number = &number;
        number = 99;
        h.group_fail = i;
        CHECK(!three_mm_scene_create(&state, &number, 0x0303, 0, &s) && number == 0xffffffffUL && !h.groups && !h.root);
        CHECK(!strcmp(h.log, i == 1 ? "12s" : "12r"));
    }
    for (i = 1; i <= 3; ++i)
    {
        reset(&h);
        h.number = &number;
        number = 99;
        h.root_fail = i;
        CHECK(!three_mm_scene_create(&state, &number, 0x0303, 0, &s) && number == 0xffffffffUL && !h.groups && !h.root);
        CHECK(!strcmp(h.log, i == 1 ? "12crs" : i == 2 ? "12chrsX" : "12chnrsX"));
    }
    for (i = 1; i <= 6; ++i)
    {
        reset(&h);
        h.number = &number;
        number = 99;
        h.stage = i;
        h.have_old = 0;
        CHECK(!three_mm_scene_create(&state, &number, 0x0303, 0, &s) && number == 0xffffffffUL && !h.groups &&
              !h.root && !h.created[0] && !h.created[1]);
    }
}
int main(void)
{
    validation();
    transactions();
    headers();
    creation();
    puts("scene data passed");
    return 0;
}
