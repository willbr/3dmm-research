#include "mm/background_resource.h"
#include "mm/chunky_file.h"
#include "engine_resource_block_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Background resource: %d\n", __LINE__);                                                    \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
enum
{
    FH = 1,
    RH,
    NAME,
    FS,
    RS,
    FP,
    DECODE_PALETTE,
    LP,
    GP,
    FL,
    LL,
    GL,
    AA,
    AL,
    CONFIG0,
    CONFIG1,
    RESOLVE,
    APPEND0,
    APPEND1,
    SELECT,
    DIAG,
    ADD0,
    ENABLE0,
    ADD1,
    ENABLE1,
    DISABLE0,
    REMOVE0,
    DISABLE1,
    REMOVE1,
    FREE_A,
    FREE_L,
    SIZE,
    CREATE,
    PUBLISH,
    FAILURE,
    RELEASE_OBJECT
};
typedef struct List
{
    unsigned char bytes[112];
    MMI32 count, stride;
} List;
typedef struct Host
{
    MMBackgroundResource resource;
    MMBackgroundCameraState camera;
    MMBackgroundServices services;
    unsigned char header[8], sound[28], actors[32], lights[32];
    List palette, light_list;
    MMChunkChild children[6];
    MMU32 child_count;
    int trace[256], traces, fail, big, profile, has_palette, has_sound, no_lights, no_cameras, gap, block, allocations;
    int configured, appended, selected, diagnostics, actor_live, light_live, colors;
    int created, released, published, factory_fail;
    MMI32 header_length, sound_length, last_size;
    MMBackgroundLight values[2];
} Host;
static void event(Host *h, int code)
{
    CHECK(h->traces < 256);
    h->trace[h->traces++] = code;
}
static int effect(Host *h, int code)
{
    event(h, code);
    return h->fail != code;
}
static void put_word(unsigned char *p, MMU32 value, int n, int big)
{
    int i;
    for (i = 0; i < n; ++i)
        p[big ? n - i - 1 : i] = (unsigned char)(value >> (i * 8));
}
static int read_at(void *p, MMU32 offset, void *out, size_t bytes)
{
    Host *h = (Host *)p;
    int code = h->block == FH ? RH : RS;
    CHECK(!offset && bytes == (size_t)(code == RH ? 8 : 28));
    memset(out, 0xA5, bytes);
    if (!effect(h, code))
        return 0;
    memcpy(out, code == RH ? h->header : h->sound, bytes);
    return 1;
}
static int entry(void *p, MMU32 position, MMChunkEntry *out)
{
    Host *h = (Host *)p;
    CHECK(position == 0);
    memset(out, 0, sizeof(*out));
    out->type = MM_BACKGROUND;
    out->number = 42;
    out->child_count = h->child_count;
    return 1;
}
static int child_at(void *p, MMU32 parent, MMU32 position, MMChunkChild *out)
{
    Host *h = (Host *)p;
    CHECK(parent == 0 && position < h->child_count);
    *out = h->children[position];
    return 1;
}
static void add_child(Host *h, MMU32 slot, MMU32 type, MMU32 number)
{
    MMChunkChild *child = &h->children[h->child_count++];
    child->slot = slot;
    child->type = type;
    child->number = number;
}
static MMReadStream stream(Host *h)
{
    MMReadStream r;
    r.context = h;
    r.read_at = read_at;
    r.length = (MMU32)(h->block == FH ? h->header_length : h->sound_length);
    return r;
}
static int name(void *p, MMU32 type, MMU32 number)
{
    CHECK(type == MM_BACKGROUND && number == 42);
    return effect((Host *)p, NAME);
}
static int read_array(void *p, MMU32 offset, void *out, size_t bytes)
{
    Host *h = (Host *)p;
    const int palette = h->block == FP;
    const List *list = palette ? &h->palette : &h->light_list;
    if (!offset)
    {
        unsigned char *header = (unsigned char *)out;
        CHECK(bytes == 12);
        if (!effect(h, palette ? LP : LL))
            return 0;
        put_word(header, 1, 2, h->big);
        put_word(header + 2, 0x0303, 2, h->big);
        put_word(header + 4, (MMU32)list->stride, 4, h->big);
        put_word(header + 8, (MMU32)list->count, 4, h->big);
        return 1;
    }
    CHECK(offset == 12 && bytes <= sizeof(list->bytes));
    if (!effect(h, palette ? GP : GL))
        return 0;
    memcpy(out, list->bytes, bytes);
    return 1;
}
static int payload(void *p, MMU32 type, MMU32 number, MMReadReceiver receive, void *out, int *result)
{
    Host *h = (Host *)p;
    MMReadStream r;
    int code = type == MM_BACKGROUND ? FH : type == MM_BACKGROUND_SOUND ? FS : type == MM_BACKGROUND_COLORS ? FP : FL;
    CHECK(number == (code == FH ? 42 : type));
    if (!effect(h, code))
        return MM_CHUNK_NOT_FOUND;
    if (code == FP && h->fail == DECODE_PALETTE)
        return MM_CHUNK_BAD_FILE;
    h->block = code;
    if (code == FP || code == FL)
    {
        const List *list = code == FP ? &h->palette : &h->light_list;
        r.context = h;
        r.read_at = read_array;
        r.length = 12 + (MMU32)(list->count == 2 ? 2 * list->stride : 0);
    }
    else
        r = stream(h);
    *result = receive(out, &r);
    return MM_CHUNK_OK;
}
static int allocate(void *p, MMI32 bytes, void **out)
{
    Host *h = (Host *)p;
    int index = h->allocations++;
    CHECK(index < 2 && bytes == h->resource.light_count * (index ? 7 : 6));
    CHECK(!*out);
    if (!effect(h, index ? AL : AA))
        return 0;
    *out = index ? (void *)h->lights : h->actors;
    if (index)
        h->light_live = 1;
    else
        h->actor_live = 1;
    return 1;
}
static void free_memory(void *p, void **out)
{
    Host *h = (Host *)p;
    int actors = out == &h->resource.actors;
    event(h, actors ? FREE_A : FREE_L);
    if (*out)
    {
        CHECK(*out == (actors ? (void *)h->actors : h->lights));
        if (actors)
        {
            CHECK(h->actor_live);
            h->actor_live = 0;
        }
        else
        {
            CHECK(h->light_live);
            h->light_live = 0;
        }
    }
    *out = NULL;
}
static int configure(void *p, MMBackgroundResource *resource, MMI32 i, const MMBackgroundLight *v)
{
    Host *h = (Host *)p;
    CHECK(resource == &h->resource && i < 2 && resource->light_count == 2);
    if (h->profile == MM_BACKGROUND_STUDIO)
        CHECK(h->actor_live && h->light_live);
    CHECK(v->matrix.m[0][0] == i * 100 - 500 && v->matrix.m[3][2] == i * 100 - 489 && v->intensity == i * 100 + 12 &&
          v->type == i + 1);
    if (!effect(h, CONFIG0 + i))
        return 0;
    h->values[i] = *v;
    ++h->configured;
    return 1;
}
static void light_effect(void *p, MMBackgroundResource *r, MMI32 i, int code, int enabled)
{
    Host *h = (Host *)p;
    CHECK(r == &h->resource && h->actor_live && h->light_live && r->enabled == enabled && i < 2);
    event(h, code + i * 2);
}
static void add(void *p, MMBackgroundResource *r, MMI32 i)
{
    light_effect(p, r, i, ADD0, 0);
}
static void enable(void *p, MMBackgroundResource *r, MMI32 i)
{
    light_effect(p, r, i, ENABLE0, 0);
}
static void disable(void *p, MMBackgroundResource *r, MMI32 i)
{
    light_effect(p, r, i, DISABLE0, 1);
}
static void remove_light(void *p, MMBackgroundResource *r, MMI32 i)
{
    light_effect(p, r, i, REMOVE0, 1);
}
static void color(void *p, MMI32 index, const unsigned char *bytes)
{
    Host *h = (Host *)p;
    unsigned char expected[4];
    CHECK(index == 20 + h->colors && h->colors < 2);
    put_word(expected, 0x10203040U + (MMU32)h->colors, 4, h->services.host_big_endian);
    CHECK(!memcmp(bytes, expected, 4));
    ++h->colors;
}
static void resolve(void *p)
{
    Host *h = (Host *)p;
    event(h, RESOLVE);
    CHECK(h->services.retain_palette || !h->resource.palette.buffers[0].data);
}
static int append(void *p, MMU32 number)
{
    Host *h = (Host *)p;
    CHECK(number == 80U + (MMU32)h->appended);
    if (!effect(h, APPEND0 + h->appended))
        return 0;
    ++h->appended;
    return 1;
}
static int select_camera(void *p, MMI32 index)
{
    Host *h = (Host *)p;
    CHECK(!index && h->camera.count == 2);
    ++h->selected;
    return effect(h, SELECT);
}
static void diagnostic(void *p, int code)
{
    Host *h = (Host *)p;
    CHECK(code == MM_BACKGROUND_READ_ERROR || (code == MM_BACKGROUND_CAMERA_GAPS && h->gap));
    event(h, DIAG);
    ++h->diagnostics;
}
static void init(Host *h, int big, int profile, int host_big)
{
    int i, j;
    MMBackgroundServices *s;
    memset(h, 0, sizeof(*h));
    mm_background_resource_init(&h->resource);
    CHECK(mm_background_camera_init(&h->camera, NULL));
    h->big = big;
    h->profile = profile;
    h->has_palette = h->has_sound = 1;
    h->header_length = 8;
    h->sound_length = 28;
    h->palette.count = h->light_list.count = 2;
    h->palette.stride = 4;
    h->light_list.stride = 56;
    put_word(h->header, 1, 2, big);
    put_word(h->header + 2, 0x1234, 2, big);
    h->header[4] = 20;
    put_word(h->sound, 1, 2, big);
    put_word(h->sound + 2, 0xFEDC, 2, big);
    put_word(h->sound + 4, 0x80000000U, 4, big);
    put_word(h->sound + 8, 1, 4, big);
    put_word(h->sound + 12, 0xFFFFFFFEU, 4, big);
    put_word(h->sound + 16, 0x1BADB002U, 4, big);
    put_word(h->sound + 20, 0x4D534E44U, 4, big);
    put_word(h->sound + 24, 0xFEDC1234U, 4, big);
    for (i = 0; i < 2; ++i)
    {
        put_word(h->palette.bytes + i * 4, 0x10203040U + (MMU32)i, 4, big);
        for (j = 0; j < 12; ++j)
            put_word(h->light_list.bytes + i * 56 + j * 4, (MMU32)(i * 100 - 500 + j), 4, big);
        put_word(h->light_list.bytes + i * 56 + 48, (MMU32)(i * 100 + 12), 4, big);
        put_word(h->light_list.bytes + i * 56 + 52, (MMU32)(i + 1), 4, big);
    }
    s = &h->services;
    s->context = h;
    s->host_big_endian = host_big;
    s->actor_bytes = 6;
    s->light_bytes = 7;
    s->name = name;
    s->allocate = allocate;
    s->free_memory = free_memory;
    s->configure_light = configure;
    s->add_light = add;
    s->enable_light = enable;
    s->disable_light = disable;
    s->remove_light = remove_light;
    s->palette_color = color;
    s->resolve_palette = resolve;
    s->append_camera = append;
    s->select_camera = select_camera;
    s->diagnostic = diagnostic;
}
static int load(Host *h)
{
    MMBackgroundSource source;
    h->child_count = 0;
    if (h->has_sound)
        add_child(h, 0, MM_BACKGROUND_SOUND, MM_BACKGROUND_SOUND);
    if (!h->no_cameras)
        add_child(h, 0, MM_BACKGROUND_CAMERA, 80);
    if (h->has_palette)
        add_child(h, 0, MM_BACKGROUND_COLORS, MM_BACKGROUND_COLORS);
    if (!h->no_lights)
        add_child(h, 0, MM_BACKGROUND_LIGHTS, MM_BACKGROUND_LIGHTS);
    if (!h->no_cameras)
        add_child(h, 1, MM_BACKGROUND_CAMERA, 81);
    if (h->gap)
        add_child(h, 3, MM_BACKGROUND_CAMERA, 83);
    source.chunks.context = h;
    source.chunks.count = 1;
    source.chunks.entry = entry;
    source.chunks.child = child_at;
    source.context = h;
    source.payload = payload;
    return mm_background_read(&h->resource, &h->camera, MM_BACKGROUND, 42, h->profile, &source, &h->services);
}
static void expect(Host *h, const int *trace, size_t n)
{
    CHECK(h->traces == (int)n && !memcmp(h->trace, trace, n * sizeof(*trace)));
}
static void dispose(Host *h)
{
    mm_background_resource_dispose(&h->resource, &h->services);
    CHECK(!h->actor_live && !h->light_live && !h->resource.actors && !h->resource.lights);
    CHECK(!h->resource.palette.buffers[0].data && !h->resource.palette_count);
}

static MMChunkAccess cached_chunks(void *p)
{
    MMChunkAccess chunks = {NULL, 1, entry, child_at};
    chunks.context = p;
    return chunks;
}

static int locate_light(void *p, MMU32 type, MMU32 number, MMBlockFile *file, int *packed)
{
    Host *h = (Host *)p;
    CHECK(type == MM_BACKGROUND_LIGHTS && number == MM_BACKGROUND_LIGHTS);
    file->file = h;
    file->offset = 128;
    file->length = 12 + sizeof(h->light_list.bytes);
    *packed = 0;
    return effect(h, FL);
}

static int cache_read(void *p, void *file, MMU32 offset, void *out, size_t bytes)
{
    Host *h = (Host *)p;
    CHECK(file == h);
    if (offset == 32)
    {
        h->block = FH;
        CHECK(bytes == 8 || bytes == 9);
        if (bytes == 9)
            ((unsigned char *)out)[8] = 0;
        return read_at(h, 0, out, 8);
    }
    CHECK(offset >= 128);
    h->block = FL;
    return read_array(h, offset - 128, out, bytes);
}

static int no_write(void *p, void *file, MMU32 offset, const void *bytes, size_t n)
{
    (void)p; (void)file; (void)offset; (void)bytes; (void)n;
    return 0;
}

static int decode_test_header(void *p, void **handle)
{
    (void)p;
    CHECK(resource_handle_size(NULL, *handle) == 9);
    return resource_handle_resize(NULL, handle, 8);
}

static void bound_root(void)
{
    Host h;
    ResourceTestBlock root;
    MMResourceCache cache = {0};
    MMBlockFile extent;
    int packed;
    static const int trace[] = {RH, NAME, FL, LL, GL, AA, AL, CONFIG0, CONFIG1};
    for (packed = 0; packed <= 1; ++packed)
    {
        init(&h, packed, MM_BACKGROUND_STUDIO, 0);
        add_child(&h, 0, MM_BACKGROUND_CAMERA, 80);
        add_child(&h, 0, MM_BACKGROUND_LIGHTS, MM_BACKGROUND_LIGHTS);
        resource_block_init(&root, &h, cache_read, no_write);
        extent.file = &h;
        extent.offset = 32;
        extent.length = packed ? 9 : 8;
        CHECK(mm_owned_block_set_file(&root.owner, &extent, packed) == MM_DATA_OK);
        cache.file = &h;
        cache.services.handles = root.owner.handles;
        cache.services.block_files = &root.files;
        cache.services.live_chunks = cached_chunks;
        cache.services.locate = locate_light;
        CHECK(mm_background_read_cached(&h.resource, &h.camera, MM_BACKGROUND, 42, &cache, &root.owner,
                                         decode_test_header, &h.services));
        expect(&h, trace, sizeof(trace) / sizeof(trace[0]));
        CHECK(h.camera.count == 1 && h.configured == 2);
        CHECK(root.owner.view.kind == (packed ? MM_BLOCK_MEMORY : MM_BLOCK_FILE));
        CHECK(mm_owned_block_clear(&root.owner) == MM_DATA_OK);
        dispose(&h);
        mm_background_camera_dispose(&h.camera);
    }
}
static void decoder_tests(int big)
{
    Host h;
    MMReadStream r;
    MMBackgroundHeader header, before_header;
    MMBackgroundSound sound, before_sound;
    init(&h, big, 0, 0);
    h.block = FH;
    r = stream(&h);
    CHECK(mm_background_header_read(&r, &header) && header.system == 0x1234 && header.palette_base == 20);
    memcpy(&before_header, &header, sizeof(header));
    h.fail = RH;
    CHECK(!mm_background_header_read(&r, &header) && !memcmp(&header, &before_header, sizeof(header)));
    h.fail = 0;
    h.header[0] = h.header[1] = 0;
    CHECK(!mm_background_header_read(&r, &header));
    CHECK(!memcmp(&header, &before_header, sizeof(header)));
    r.length = 9;
    CHECK(!mm_background_header_read(&r, &header));
    h.block = FS;
    r = stream(&h);
    CHECK(mm_background_sound_read(&r, &sound));
    CHECK(sound.system == 0xFEDC && sound.volume == (-2147483647 - 1) && sound.loop && sound.tag.sid == -2);
    CHECK(!sound.tag.cache && sound.tag.type == 0x4D534E44U && sound.tag.number == 0xFEDC1234U);
    memcpy(&before_sound, &sound, sizeof(sound));
    h.fail = RS;
    CHECK(!mm_background_sound_read(&r, &sound) && !memcmp(&sound, &before_sound, sizeof(sound)));
    h.fail = 0;
    h.sound[0] = h.sound[1] = 0;
    CHECK(!mm_background_sound_read(&r, &sound));
    CHECK(!memcmp(&sound, &before_sound, sizeof(sound)));
    r.length = 27;
    CHECK(!mm_background_sound_read(&r, &sound));
}
static void read_tests(int big, int host_big)
{
    Host h;
    int i, j, profile;
    const unsigned char *colours;
    unsigned char expected[4];
    MMU32 colour_count;
    MMI32 base;
    const int native[] = {FH, RH, NAME, FS, RS, FP, LP, GP, FL, LL, GL, AA, AL, CONFIG0, CONFIG1};
    const int on[] = {ADD0, ENABLE0, ADD1, ENABLE1};
    const int off[] = {DISABLE0, REMOVE0, DISABLE1, REMOVE1};
    const int teardown[] = {DISABLE0, REMOVE0, DISABLE1, REMOVE1, FREE_A, FREE_L};
    const int failures[] = {FH, RH, NAME, FS, RS, FP, FL, LL, GL, AA, AL, CONFIG0, CONFIG1};
    init(&h, big, 0, host_big);
    CHECK(load(&h));
    CHECK(h.camera.count == 2 && h.camera.current == -1 && h.resource.light_count == 2);
    CHECK(h.resource.palette_count == 2 && h.resource.palette_base == 20 && h.configured == 2);
    CHECK(h.resource.sound.loop && h.resource.sound.volume == (-2147483647 - 1));
    expect(&h, native, sizeof(native) / sizeof(*native));
    colours = mm_background_palette(&h.resource, &colour_count, &base);
    CHECK(colours && colour_count == 2 && base == 20);
    memset(h.palette.bytes, 0, sizeof(h.palette.bytes));
    for (i = 0; i < 2; ++i)
    {
        put_word(expected, 0x10203040U + (MMU32)i, 4, host_big);
        CHECK(!memcmp(colours + i * 4, expected, 4));
    }
    h.traces = 0;
    mm_background_lights_on(&h.resource, &h.services);
    CHECK(h.resource.enabled);
    expect(&h, on, 4);
    mm_background_lights_on(&h.resource, &h.services);
    expect(&h, on, 4);
    h.resource.leave_on = 1;
    mm_background_lights_off(&h.resource, &h.services);
    CHECK(h.resource.enabled);
    expect(&h, on, 4);
    h.resource.leave_on = 0;
    h.traces = 0;
    mm_background_lights_off(&h.resource, &h.services);
    CHECK(!h.resource.enabled);
    expect(&h, off, 4);
    mm_background_lights_off(&h.resource, &h.services);
    expect(&h, off, 4);
    mm_background_lights_on(&h.resource, &h.services);
    h.traces = 0;
    dispose(&h);
    expect(&h, teardown, sizeof(teardown) / sizeof(*teardown));
    h.traces = 0;
    dispose(&h);
    CHECK(h.traces == 2 && h.trace[0] == FREE_A && h.trace[1] == FREE_L);
    init(&h, big, 0, host_big);
    h.has_palette = h.has_sound = 0;
    CHECK(load(&h));
    CHECK(h.resource.sound.tag.sid == -1);
    CHECK(!mm_background_palette(&h.resource, &colour_count, &base) && !colour_count && !base);
    dispose(&h);
    for (i = DECODE_PALETTE; i <= GP; ++i)
    {
        init(&h, big, 0, host_big);
        h.fail = i;
        CHECK(load(&h));
        CHECK(!mm_background_palette(&h.resource, &colour_count, &base) && !colour_count && base == 20);
        dispose(&h);
        init(&h, big, 1, host_big);
        h.fail = i;
        CHECK(!load(&h) && !h.colors);
        dispose(&h);
    }
    for (i = 0; i < (int)(sizeof(failures) / sizeof(*failures)); ++i)
    {
        init(&h, big, 0, host_big);
        h.fail = failures[i];
        CHECK(!load(&h) && h.diagnostics == 1 && h.trace[h.traces - 1] == DIAG);
        dispose(&h);
    }
    for (profile = 0; profile < 2; ++profile)
        for (j = 0; j < 7; ++j)
        {
            init(&h, big, profile, host_big);
            if (j == 0)
                h.no_lights = 1;
            else if (j == 1)
                h.header_length = 7;
            else if (j == 2)
                h.sound_length = 27;
            else if (j == 3)
                h.palette.stride = 8;
            else if (j == 4)
                h.light_list.stride = 55;
            else if (j == 5)
                h.light_list.count = -1;
            else
                h.light_list.count = 2147483647;
            if (profile && j == 2)
            {
                CHECK(load(&h));
            }
            else
                CHECK(!load(&h));
            dispose(&h);
        }
    init(&h, big, 0, host_big);
    h.services.debug_checks = 1;
    h.gap = 1;
    CHECK(load(&h) && h.diagnostics == 1);
    dispose(&h);
    init(&h, big, 1, host_big);
    CHECK(load(&h));
    CHECK(h.colors == 2 && h.configured == 2 && h.appended == 2 && h.selected == 1);
    CHECK(!h.allocations && !h.resource.actors && !h.resource.lights);
    CHECK(!mm_background_palette(&h.resource, &colour_count, &base) && !colour_count && base == 20);
    dispose(&h);
    init(&h, big, 1, host_big);
    h.services.retain_palette = 1;
    CHECK(load(&h));
    colours = mm_background_palette(&h.resource, &colour_count, &base);
    CHECK(colours && colour_count == 2 && base == 20 && h.colors == 2);
    mm_background_resource_dispose(&h.resource, NULL);
    CHECK(!mm_background_palette(&h.resource, &colour_count, &base) && !colour_count);
    for (i = 0; i < 6; ++i)
    {
        init(&h, big, 1, host_big);
        h.fail = i == 0 ? LP : i == 1 ? CONFIG0 : i == 2 ? CONFIG1 : i == 3 ? APPEND0 : i == 4 ? APPEND1 : SELECT;
        CHECK(!load(&h) && !h.diagnostics && !h.resource.palette.buffers[0].data);
        dispose(&h);
    }
    init(&h, big, 1, host_big);
    h.header[4] = 255;
    CHECK(!load(&h));
    dispose(&h);
    init(&h, big, 1, host_big);
    h.no_cameras = 1;
    CHECK(!load(&h) && !h.selected);
    dispose(&h);
    init(&h, big, 0, host_big);
    h.no_cameras = 1;
    CHECK(load(&h) && !h.camera.count);
    dispose(&h);
}
static void *create(void *p)
{
    Host *h = (Host *)p;
    event(h, CREATE);
    ++h->created;
    return h->factory_fail == CREATE ? NULL : &h->resource;
}
static int initialize(void *p, void *object, MMU32 type, MMU32 number)
{
    Host *h = (Host *)p;
    CHECK(object == &h->resource && type == MM_BACKGROUND && number == 42);
    return load(h);
}
static MMI32 light_count(void *p, void *object)
{
    Host *h = (Host *)p;
    CHECK(object == &h->resource);
    return h->factory_fail == SIZE ? 2147483647 : h->resource.light_count;
}
static void release_object(void *p, void *object)
{
    Host *h = (Host *)p;
    CHECK(object == &h->resource && h->trace[h->traces - 1] == FAILURE);
    event(h, RELEASE_OBJECT);
    ++h->released;
    dispose(h);
}
static void size_out(void *p, MMI32 bytes)
{
    Host *h = (Host *)p;
    event(h, SIZE);
    h->last_size = bytes;
}
static void publish(void *p, void *object)
{
    Host *h = (Host *)p;
    CHECK(object == &h->resource && !h->released);
    event(h, PUBLISH);
    ++h->published;
}
static void failure(void *p)
{
    event((Host *)p, FAILURE);
}
static void factory_tests(void)
{
    Host h;
    MMBackgroundFactoryServices s;
    int i;
    memset(&s, 0, sizeof(s));
    s.context = &h;
    s.object_bytes = 100;
    s.actor_bytes = 6;
    s.light_bytes = 7;
    s.create = create;
    s.initialize = initialize;
    s.lights = light_count;
    s.release = release_object;
    s.size = size_out;
    s.publish = publish;
    s.failure = failure;
    init(&h, 0, 0, 0);
    CHECK(mm_background_factory(MM_BACKGROUND, 42, 0, &s));
    CHECK(h.traces == 1 && h.last_size == 113 && !h.created);
    init(&h, 0, 0, 0);
    CHECK(mm_background_factory(MM_BACKGROUND, 42, 1, &s));
    CHECK(h.trace[0] == SIZE && h.trace[1] == CREATE && h.trace[h.traces - 2] == PUBLISH &&
          h.trace[h.traces - 1] == SIZE && h.last_size == 126 && h.published == 1);
    dispose(&h);
    for (i = 0; i < 3; ++i)
    {
        init(&h, 0, 0, 0);
        if (i == 0)
            h.factory_fail = CREATE;
        else if (i == 1)
            h.fail = AL;
        else
            h.factory_fail = SIZE;
        CHECK(!mm_background_factory(MM_BACKGROUND, 42, 1, &s) && !h.published && h.last_size == 113);
        CHECK(h.released == (i != 0) && !h.actor_live && !h.light_live && !h.resource.palette.buffers[0].data);
    }
    init(&h, 0, 0, 0);
    s.object_bytes = 2147483647;
    CHECK(!mm_background_factory(MM_BACKGROUND, 42, 0, &s) && !h.traces);
}
typedef struct Tags
{
    int calls, fail, cache_calls;
    MMU32 types[9];
    MMTag parent;
} Tags;
static const MMU32 kinds[] = {MM_BACKGROUND_SOUND, MM_BACKGROUND_LIGHTS, MM_BACKGROUND_COLORS,
                                   MM_BACKGROUND_CAMERA};
static int tag_child(void *p, MMU32 kind, MMTag *out)
{
    Tags *t = (Tags *)p;
    CHECK(!t->cache_calls && t->calls < 4 && kind == kinds[t->calls]);
    t->types[t->calls++] = kind;
    *out = t->parent;
    out->type = kind;
    return t->calls != t->fail;
}
static int cache(void *p, const MMTag *tag, int children)
{
    Tags *t = (Tags *)p;
    CHECK(t->calls >= 4 && children == (t->cache_calls != 0));
    CHECK(tag->type == (t->cache_calls ? kinds[t->cache_calls - 1] : MM_BACKGROUND));
    CHECK(tag->sid == 0 && tag->cache == t && tag->number == 42);
    ++t->cache_calls;
    ++t->calls;
    return t->calls != t->fail;
}
static void tag_tests(void)
{
    Tags t;
    MMBackgroundTagServices s;
    MMTagList list;
    MMTagCached tag;
    MMTagChild child;
    MMGroup group;
    MMGroupLocation location;
    int i, j;
    unsigned types = 0;
    memset(&s, 0, sizeof(s));
    s.context = &t;
    s.child = tag_child;
    s.cache = cache;
    memset(&t, 0, sizeof(t));
    t.parent.type = MM_BACKGROUND;
    t.parent.number = 42;
    CHECK(mm_tag_list_init(&list, NULL) == MM_STORAGE_OK);
    CHECK(mm_background_tags(&list, &t.parent) && list.count == 1);
    CHECK(mm_tag_list_get(&list, 0, &tag) == MM_STORAGE_OK &&
          tag.tag.type == MM_BACKGROUND && tag.tag.number == 42 && !tag.children);
    memset(&group, 0, sizeof(group));
    group.storage = &list.storage;
    group.fixed_bytes = sizeof(tag);
    group.count = list.count;
    group.bytes = list.bytes;
    group.free_count = -1;
    CHECK(mm_group_location(&group, 0, &location) == MM_STORAGE_OK &&
          location.bytes == (MMI32)(sizeof(tag) + 4 * sizeof(child)));
    for (i = 0; i < 4; ++i)
    {
        memcpy(&child, list.storage.buffers[0].data + location.offset + sizeof(tag) + i * sizeof(child), sizeof(child));
        CHECK(child.slot == 0);
        for (j = 0; j < 4 && child.type != kinds[j]; ++j)
            ;
        CHECK(j < 4);
        types |= 1U << j;
    }
    CHECK(types == 15U && mm_tag_list_dispose(&list) == MM_STORAGE_OK);
    for (i = 0; i <= 9; ++i)
    {
        memset(&t, 0, sizeof(t));
        t.fail = i;
        t.parent.sid = 0;
        t.parent.cache = &t;
        t.parent.type = MM_BACKGROUND;
        t.parent.number = 42;
        CHECK(mm_background_cache(&t.parent, &s) == !i);
        CHECK(t.calls == (i ? i : 9));
    }
}
int main(void)
{
    int big, host;
    for (big = 0; big < 2; ++big)
    {
        decoder_tests(big);
        for (host = 0; host < 2; ++host)
            read_tests(big, host);
    }
    factory_tests();
    bound_root();
    tag_tests();
    puts("Background resource passed");
    return 0;
}
