#include "mm/scene_control.h"
#include "scene_member_fixture.h"
#include "chunk_writer_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Scene control: %d\n", __LINE__);                                                          \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Object
{
    int id, story, visible, valid, view;
    MMI32 first, last;
} Object;
typedef struct Undo
{
    MMSceneTitleUndo title;
    MMSceneTransitionUndo transition;
} Undo;
typedef struct Host
{
    MMSceneState data, other;
    MMSceneOwner owner;
    SceneMemberMemory members;
    Undo *undo;
    Object objects[8];
    int trace[128], traces, fail, allocations, releases, dead;
    unsigned char header[16];
    MMMemoryStream memory;
    unsigned char name_wire[32];
    MMU32 name_bytes;
} Host;
static void event(Host *h, int value)
{
    CHECK(h->traces < 128);
    h->trace[h->traces++] = value;
}
static void expect(Host *h, const int *values, int n)
{
    CHECK(h->traces == n && !memcmp(h->trace, values, n * sizeof(*values)));
    h->traces = 0;
}
static void text(MMMovieTitle *t, const char *s)
{
    unsigned short wide[256];
    MMU32 i, n = (MMU32)strlen(s);
    for (i = 0; i < n; ++i)
        wide[i] = (unsigned char)s[i];
    CHECK(mm_movie_title_set(t, t->unit == 1 ? (const void *)s : wide, n));
}
static void init(Host *h, unsigned unit)
{
    int i;
    memset(h, 0, sizeof(*h));
    CHECK(mm_scene_init(&h->data, unit));
    CHECK(mm_scene_init(&h->other, unit));
    text(&h->data.name, "old");
    text(&h->other.name, "other");
    h->data.transition = 2;
    h->other.transition = 4;
    for (i = 0; i < 8; ++i)
    {
        h->objects[i].id = i;
        h->objects[i].valid = 1;
        h->objects[i].view = 1;
        h->objects[i].first = 1;
        h->objects[i].last = 20;
    }
}
static void *new_undo(void *p, int kind)
{
    Host *h = (Host *)p;
    event(h, 10 + kind);
    if (h->fail == 1)
        return NULL;
    h->undo = (Undo *)calloc(1, sizeof(Undo));
    CHECK(h->undo);
    ++h->allocations;
    return h->undo;
}
static MMSceneTitleUndo *title(void *p, void *u)
{
    Host *h = (Host *)p;
    CHECK(u == h->undo);
    return &h->undo->title;
}
static MMSceneTransitionUndo *transition(void *p, void *u)
{
    Host *h = (Host *)p;
    CHECK(u == h->undo);
    return &h->undo->transition;
}
static int commit(void *p, void *u)
{
    Host *h = (Host *)p;
    CHECK(u == h->undo);
    event(h, 12);
    CHECK(h->data.transition == 2 && h->data.name.count == 3);
    if (h->trace[0] == 10)
        CHECK(h->undo->transition.transition == 2);
    else
        CHECK(h->undo->title.name.count == 3 && h->undo->title.name.unit == h->data.name.unit);
    return h->fail != 2;
}
static void release(void *p, void *u)
{
    Host *h = (Host *)p;
    CHECK(u == h->undo);
    CHECK(h->data.transition == 2 && h->data.name.count == 3);
    event(h, 13);
    ++h->releases;
    free(h->undo);
    h->undo = NULL;
}
static MMScenePropertyServices properties(Host *h)
{
    MMScenePropertyServices s;
    s.context = h;
    s.data = &h->data;
    s.new_undo = new_undo;
    s.title = title;
    s.transition = transition;
    s.commit = commit;
    s.release = release;
    return s;
}
static int switch_scene(void *p)
{
    Host *h = (Host *)p;
    CHECK(!h->dead);
    event(h, 20);
    return h->fail != 1;
}
static int seek(void *p)
{
    Host *h = (Host *)p;
    CHECK(!h->dead);
    event(h, 21);
    return h->fail != 2;
}
static MMSceneState *scene(void *p)
{
    Host *h = (Host *)p;
    CHECK(!h->dead);
    event(h, 22);
    return &h->other;
}
static void flush(void *p)
{
    Host *h = (Host *)p;
    CHECK(!h->dead);
    event(h, 23);
}
static void clear(void *p)
{
    Host *h = (Host *)p;
    event(h, 24);
    free(h->undo);
    h->undo = NULL;
    h->dead = 1;
}
static MMScenePropertyUndoServices navigation(Host *h)
{
    MMScenePropertyUndoServices s;
    s.context = h;
    s.switch_scene = switch_scene;
    s.seek = seek;
    s.scene = scene;
    s.flush = flush;
    s.clear = clear;
    return s;
}
static void test_properties(void)
{
    Host h;
    MMScenePropertyServices s;
    MMMovieTitle name;
    unsigned unit;
    int kind, fail;
    for (unit = 1; unit <= 2; ++unit)
        for (kind = 0; kind < 2; ++kind)
            for (fail = 0; fail < 3; ++fail)
            {
                init(&h, unit);
                s = properties(&h);
                h.fail = fail;
                CHECK(mm_movie_title_init(&name, unit));
                text(&name, "new title");
                CHECK((kind ? mm_scene_name_edit(&name, &s) : mm_scene_transition_edit(3, &s)) == !fail);
                CHECK(h.allocations == h.releases && !h.undo);
                CHECK(h.data.transition == (!kind && !fail ? 3 : 2));
                CHECK(h.data.name.count == (kind && !fail ? 9 : 3));
                if (fail == 1)
                {
                    CHECK(h.traces == 1 && h.trace[0] == 10 + kind);
                }
                else
                {
                    int e[3];
                    e[0] = 10 + kind;
                    e[1] = 12;
                    e[2] = 13;
                    expect(&h, e, 3);
                }
            }
    /* Full counted native payload, including empty and 255-unit strings. */
    for (unit = 1; unit <= 2; ++unit)
    {
        MMScenePropertyUndoServices n;
        int i;
        init(&h, unit);
        n = navigation(&h);
        h.undo = (Undo *)calloc(1, sizeof(Undo));
        CHECK(h.undo);
        CHECK(mm_movie_title_init(&h.undo->title.name, unit));
        CHECK(mm_scene_title_swap(&h.undo->title, &n));
        CHECK(h.other.name.count == 0 && h.undo->title.name.count == 5);
        h.traces = 0;
        CHECK(mm_scene_title_swap(&h.undo->title, &n));
        CHECK(h.other.name.count == 5 && h.undo->title.name.count == 0);
        CHECK(mm_movie_title_init(&name, unit));
        for (i = 0; i < 255; ++i)
            if (unit == 2)
                name.text[i] = (unsigned short)(0x1200 + i);
            else
                ((unsigned char *)name.text)[i] = (unsigned char)(128 + i % 128);
        name.count = 255;
        h.other.name = name;
        h.traces = 0;
        CHECK(mm_scene_title_swap(&h.undo->title, &n));
        CHECK(h.undo->title.name.count == 255 && !memcmp(h.undo->title.name.text, name.text, 255 * unit));
        free(h.undo);
    }
}
static void test_swaps(void)
{
    Host h;
    MMScenePropertyUndoServices s;
    int kind, fail;
    for (kind = 0; kind < 2; ++kind)
        for (fail = 0; fail < 3; ++fail)
        {
            int ok;
            init(&h, 1);
            s = navigation(&h);
            h.fail = fail;
            h.undo = (Undo *)calloc(1, sizeof(Undo));
            CHECK(h.undo);
            h.undo->title.name = h.data.name;
            h.undo->transition.transition = 2;
            ok = kind ? mm_scene_title_swap(&h.undo->title, &s)
                      : mm_scene_transition_swap(&h.undo->transition, &s);
            CHECK(ok == (fail == 0 || (kind && fail == 2)));
            if (ok)
            {
                if (kind)
                {
                    const int e[] = {20, 22, 23};
                    expect(&h, e, 3);
                    CHECK(h.other.name.count == 3 && h.undo->title.name.count == 5);
                }
                else
                {
                    const int e[] = {20, 21, 22, 23};
                    expect(&h, e, 4);
                    CHECK(h.other.transition == 2 && h.undo->transition.transition == 4);
                }
                free(h.undo);
            }
            else
            {
                CHECK(!h.undo && h.dead && h.other.name.count == 5 && h.other.transition == 4);
                if (fail == 1)
                {
                    const int e[] = {20, 23, 24};
                    expect(&h, e, 3);
                }
                else
                {
                    const int e[] = {20, 21, 23, 24};
                    expect(&h, e, 4);
                }
            }
        }
}
static int lifetime(void *p, int text_kind, void *o, MMI32 *first, MMI32 *last)
{
    Host *h = (Host *)p;
    Object *v = (Object *)o;
    event(h, (text_kind ? 40 : 30) + v->id);
    if (!v->valid)
        return 0;
    *first = v->first;
    *last = v->last;
    return 1;
}
static int story(void *p, void *o)
{
    Object *v = (Object *)o;
    event((Host *)p, 50 + v->id);
    return v->story;
}
static int visible(void *p, void *o)
{
    Object *v = (Object *)o;
    event((Host *)p, 60 + v->id);
    return v->visible;
}
static void *view(void *p, void *o)
{
    Object *v = (Object *)o;
    event((Host *)p, 70 + v->id);
    return v->view ? o : NULL;
}
static void scroll(void *p, void *o)
{
    event((Host *)p, 80 + ((Object *)o)->id);
}
static void missing(void *p)
{
    event((Host *)p, 90);
}
static void prerender(void *p, int start)
{
    event((Host *)p, start ? 91 : 92);
}
static void clean(void *p, void *o)
{
    event((Host *)p, 100 + ((Object *)o)->id);
}
static MMScenePlaybackServices playback(Host *h)
{
    MMScenePlaybackServices s;
    s.context = h;
    s.owner = &h->owner;
    s.lifetime = lifetime;
    s.story = story;
    s.visible = visible;
    s.view = view;
    s.scroll = scroll;
    s.missing_view = missing;
    s.prerender = prerender;
    s.clean_text = clean;
    return s;
}
static void test_playback(void)
{
    Host h;
    MMScenePlaybackServices s;
    int i;
    init(&h, 1);
    init_scene_members(&h.owner, &h.members);
    s = playback(&h);
    for (i = 0; i < 3; ++i)
        CHECK(mm_scene_member_append(&h.owner, MM_SCENE_ACTORS, &h.objects[i]));
    h.objects[1].valid = 0;
    CHECK(!mm_scene_start_playing(&s));
    {
        const int e[] = {30, 31};
        expect(&h, e, 2);
    }
    h.objects[1].valid = 1;
    for (i = 0; i < 6; ++i)
        CHECK(mm_scene_member_append(&h.owner, MM_SCENE_TEXTS, &h.objects[i]));
    h.objects[0].story = h.objects[0].visible = 1;
    h.objects[1].story = 1;
    h.objects[2].first = 1;
    h.objects[3].first = 0;
    h.objects[4].valid = 0;
    h.objects[5].view = 0;
    CHECK(mm_scene_start_playing(&s));
    {
        const int e[] = {30, 31, 32, 50, 60, 70, 80, 51, 61, 51, 52, 52, 42, 72,
                         82, 53, 53, 43, 54, 54, 44, 55, 55, 45, 75, 90, 91};
        expect(&h, e, 27);
    }
    mm_scene_stop_playing(&s);
    {
        const int e[] = {92, 100, 101, 102, 103, 104, 105};
        expect(&h, e, 7);
    }
    mm_scene_member_clear(&h.owner, MM_SCENE_ACTORS);
    mm_scene_member_clear(&h.owner, MM_SCENE_TEXTS);
    CHECK(mm_scene_start_playing(&s));
    {
        const int e[] = {91};
        expect(&h, e, 1);
    }
    mm_scene_stop_playing(&s);
    {
        const int e[] = {92};
        expect(&h, e, 1);
    }
}
static int read(const MMSceneIO *io, MMU32 number, MMReadReceiver receive, void *out)
{
    Host *h = io->context;
    CHECK(number == 7);
    event(h, 110);
    if (h->fail == 1)
        return 0;
    CHECK(mm_memory_stream_init(&h->memory, h->header, 16));
    return receive(out, &h->memory.stream);
}
static int load_name(void *p, MMU32 type, MMU32 number, void *wire, MMU32 capacity, MMU32 *written)
{
    Host *h = (Host *)p;
    CHECK(type == MM_SCENE_CHUNK && number == 7 && h->name_bytes <= capacity);
    event(h, 111);
    memcpy(wire, h->name_wire, h->name_bytes);
    *written = h->name_bytes;
    return 1;
}
static int scene_header_extent(void *p, MMU32 bytes, MMChunkLocation *extent)
{
    CHECK(bytes == MM_SCENE_HEADER_BYTES);
    extent->file = p;
    extent->offset = 64;
    extent->bytes = bytes;
    extent->flags = 0;
    return 1;
}
static int put(void *p, void *file, MMU32 offset, const void *bytes, size_t size)
{
    Host *h = ((ChunkWriterFixture *)p)->context;
    CHECK(file == p && offset == 64 && size == MM_SCENE_HEADER_BYTES);
    event(h, 112);
    if (h->fail == 2)
        return 0;
    memcpy(h->header, bytes, size);
    return 1;
}
static int set_name(void *p, MMU32 type, MMU32 number, const void *wire, MMU32 bytes)
{
    Host *h = (Host *)p;
    CHECK(type == MM_SCENE_CHUNK && number == 7 && bytes && bytes <= sizeof(h->name_wire));
    event(h, 113);
    if (h->name_bytes)
        CHECK(h->name_bytes == bytes && !memcmp(wire, h->name_wire, bytes));
    else
    {
        memcpy(h->name_wire, wire, bytes);
        h->name_bytes = bytes;
    }
    return h->fail != 3;
}
static void test_files(void)
{
    Host h;
    ChunkWriterFixture fixture;
    MMArchiveWriter archive;
    MMSceneIO io = {NULL, read, NULL, {NULL, MM_TEXT_WINDOWS, 0, NULL, load_name, set_name, NULL}};
    MMSceneState decoded;
    MMI32 value;
    unsigned unit;
    int big, fail;
    const unsigned short one = 1;
    for (unit = 1; unit <= 2; ++unit)
        for (big = 0; big < 2; ++big)
            for (fail = 0; fail < 4; ++fail)
            {
                init(&h, unit);
                h.data.firstFrame = -2;
                h.data.lastFrame = 19;
                CHECK(mm_scene_encode_header(&h.data, 0x303, big, h.header));
                archive = chunk_writer_fixture(&fixture, &h, scene_header_extent, put);
                fixture_chunk_add(&fixture.index, MM_SCENE_CHUNK, 7);
                archive.system = 0x303;
                archive.big_endian = big;
                io.context = &h;
                io.archive = &archive;
                io.titles.encoding = unit == 1 ? MM_TEXT_WINDOWS :
                    (*(const unsigned char *)&one ? MM_TEXT_UTF16_LE : MM_TEXT_UTF16_BE);
                CHECK(mm_chunk_title_write(&h.other.name, &h, MM_SCENE_CHUNK, 7, &io.titles));
                h.traces = 0;
                value = 99;
                h.fail = fail;
                CHECK(mm_scene_transition_read(7, unit, &value, &io) == (fail != 1));
                CHECK(value == (fail == 1 ? 99 : 2));
                h.traces = 0;
                CHECK(mm_scene_transition_write(7, unit, 4, &io) == (fail == 0));
                if (fail == 1)
                {
                    const int e[] = {110};
                    expect(&h, e, 1);
                }
                else if (fail == 2)
                {
                    const int e[] = {110, 111, 112};
                    expect(&h, e, 3);
                }
                else
                {
                    const int e[] = {110, 111, 112, 113};
                    expect(&h, e, 4);
                }
                h.fail = 0;
                h.traces = 0;
                value = 99;
                CHECK(mm_scene_transition_read(7, unit, &value, &io));
                CHECK(value == ((fail == 0 || fail == 3) ? 4 : 2));
                CHECK(mm_scene_init(&decoded, unit));
                CHECK(mm_scene_decode_header(&h.memory.stream, &decoded));
                CHECK(decoded.firstFrame == -2 && decoded.lastFrame == 19);
                h.traces = 0;
                CHECK(!mm_scene_transition_write(7, unit, 5, &io));
                {
                    const int e[] = {110, 111};
                    expect(&h, e, 2);
                }
                h.header[0] = h.header[1] = 0;
                value = 99;
                CHECK(!mm_scene_transition_read(7, unit, &value, &io) && value == 99);
            }
}
int main(void)
{
    test_properties();
    test_swaps();
    test_playback();
    test_files();
    puts("Scene control policy passed");
    return 0;
}
