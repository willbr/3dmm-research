#include "three_mm/background_resource.h"
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
    C0 = 1,
    C1,
    C2,
    ENUM0,
    ENUM1,
    ENUM2,
    FH,
    RH,
    NAME,
    CS,
    FS,
    RS,
    CP,
    FP,
    LP,
    GP0,
    PP0,
    GP1,
    PP1,
    CL,
    FL,
    LL,
    AA,
    AL,
    GL0,
    CONFIG0,
    GL1,
    CONFIG1,
    REL_LIGHT,
    REL_PALETTE,
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
    NEW_PAL,
    DUP_PAL,
    SIZE,
    CREATE,
    PUBLISH,
    FAILURE,
    RELEASE_OBJECT
};
typedef struct List
{
    unsigned char bytes[112];
    ThreeMMI32 count, stride;
    int live, kind;
} List;
typedef struct Host
{
    ThreeMMBackgroundResource resource;
    ThreeMMBackgroundCameraState camera;
    ThreeMMBackgroundServices services;
    unsigned char header[8], sound[28], actors[32], lights[32];
    List palette, light_list;
    int trace[256], traces, fail, big, profile, has_palette, has_sound, no_lights, no_cameras, gap, block, allocations;
    int configured, appended, selected, diagnostics, actor_live, light_live, colors, palette_failure;
    int created, released, published, factory_fail;
    ThreeMMI32 header_length, sound_length, last_size, *base_output;
    ThreeMMBackgroundLight values[2];
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
static void put_word(unsigned char *p, ThreeMMU32 value, int n, int big)
{
    int i;
    for (i = 0; i < n; ++i)
        p[big ? n - i - 1 : i] = (unsigned char)(value >> (i * 8));
}
static int read_at(void *p, ThreeMMU32 offset, void *out, size_t bytes)
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
static int child(void *p, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 id, ThreeMMU32 kind, ThreeMMU32 *out)
{
    Host *h = (Host *)p;
    (void)type;
    CHECK(number == 42);
    if (kind == THREE_MM_BACKGROUND_CAMERA)
    {
        event(h, C0 + (int)id);
        *out = 80 + id;
        return !h->no_cameras && id < 2;
    }
    CHECK(!id);
    *out = kind;
    if (kind == THREE_MM_BACKGROUND_SOUND)
    {
        event(h, CS);
        return h->has_sound;
    }
    if (kind == THREE_MM_BACKGROUND_COLORS)
    {
        event(h, CP);
        return h->has_palette;
    }
    CHECK(kind == THREE_MM_BACKGROUND_LIGHTS);
    event(h, CL);
    return !h->no_lights;
}
static int enumerate(void *p, ThreeMMU32 type, ThreeMMU32 number, ThreeMMI32 index, ThreeMMU32 *out)
{
    Host *h = (Host *)p;
    (void)type;
    (void)number;
    event(h, ENUM0 + index);
    *out = THREE_MM_BACKGROUND_CAMERA;
    return index < (h->no_cameras ? 0 : 2) + h->gap;
}
static int find(void *p, ThreeMMU32 type, ThreeMMU32 number, int unpack)
{
    Host *h = (Host *)p;
    int code;
    code = type == THREE_MM_BACKGROUND          ? FH
           : type == THREE_MM_BACKGROUND_SOUND  ? FS
           : type == THREE_MM_BACKGROUND_COLORS ? FP
                                                : FL;
    CHECK(number == (code == FH ? 42 : type));
    CHECK(unpack == (code == FH || code == FS));
    h->block = code;
    return effect(h, code);
}
static ThreeMMReadStream stream(void *p)
{
    Host *h = (Host *)p;
    ThreeMMReadStream r;
    r.context = h;
    r.read_at = read_at;
    r.length = (ThreeMMU32)(h->block == FH ? h->header_length : h->sound_length);
    return r;
}
static int name(void *p, ThreeMMU32 type, ThreeMMU32 number)
{
    CHECK(type == THREE_MM_BACKGROUND && number == 42);
    return effect((Host *)p, NAME);
}
static void *read_list(void *p, int *big)
{
    Host *h = (Host *)p;
    List *list = h->block == FP ? &h->palette : &h->light_list;
    int code = h->block == FP ? LP : LL;
    *big = h->big;
    if (!effect(h, code))
        return NULL;
    CHECK(!list->live);
    list->live = 1;
    return list;
}
static ThreeMMI32 count(void *p, void *list)
{
    (void)p;
    return ((List *)list)->count;
}
static ThreeMMI32 stride(void *p, void *list)
{
    (void)p;
    return ((List *)list)->stride;
}
static void get(void *p, void *value, ThreeMMI32 i, void *out, size_t bytes)
{
    Host *h = (Host *)p;
    List *list = (List *)value;
    CHECK(list->live && i >= 0 && i < list->count && bytes == (size_t)list->stride);
    CHECK(i < 2);
    event(h, (list->kind ? GL0 : GP0) + i * 2);
    memcpy(out, list->bytes + i * list->stride, bytes);
}
static void put(void *p, void *value, ThreeMMI32 i, const void *in, size_t bytes)
{
    Host *h = (Host *)p;
    List *list = (List *)value;
    CHECK(list == &h->palette && list->live && bytes == 4);
    event(h, PP0 + i * 2);
    memcpy(list->bytes + i * 4, in, 4);
}
static void release(void *p, void *value)
{
    Host *h = (Host *)p;
    List *list = (List *)value;
    CHECK(list->live);
    list->live = 0;
    event(h, list->kind ? REL_LIGHT : REL_PALETTE);
}
static int allocate(void *p, ThreeMMI32 bytes, void **out)
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
static int configure(void *p, ThreeMMBackgroundResource *resource, ThreeMMI32 i, const ThreeMMBackgroundLight *v)
{
    Host *h = (Host *)p;
    CHECK(resource == &h->resource && i < 2 && resource->light_count == 2);
    if (h->profile == THREE_MM_BACKGROUND_STUDIO)
        CHECK(h->actor_live && h->light_live);
    CHECK(v->matrix.m[0][0] == i * 100 - 500 && v->matrix.m[3][2] == i * 100 - 489 && v->intensity == i * 100 + 12 &&
          v->type == i + 1);
    if (!effect(h, CONFIG0 + i * 2))
        return 0;
    h->values[i] = *v;
    ++h->configured;
    return 1;
}
static void light_effect(void *p, ThreeMMBackgroundResource *r, ThreeMMI32 i, int code, int enabled)
{
    Host *h = (Host *)p;
    CHECK(r == &h->resource && h->actor_live && h->light_live && r->enabled == enabled && i < 2);
    event(h, code + i * 2);
}
static void add(void *p, ThreeMMBackgroundResource *r, ThreeMMI32 i)
{
    light_effect(p, r, i, ADD0, 0);
}
static void enable(void *p, ThreeMMBackgroundResource *r, ThreeMMI32 i)
{
    light_effect(p, r, i, ENABLE0, 0);
}
static void disable(void *p, ThreeMMBackgroundResource *r, ThreeMMI32 i)
{
    light_effect(p, r, i, DISABLE0, 1);
}
static void remove_light(void *p, ThreeMMBackgroundResource *r, ThreeMMI32 i)
{
    light_effect(p, r, i, REMOVE0, 1);
}
static void *new_palette(void *p)
{
    Host *h = (Host *)p;
    CHECK(h->base_output && *h->base_output == (ThreeMMI32)h->resource.palette_base);
    event(h, NEW_PAL);
    return h->palette_failure ? NULL : &h->palette;
}
static void *duplicate(void *p, void *value)
{
    Host *h = (Host *)p;
    CHECK(value == &h->palette && h->palette.live);
    event(h, DUP_PAL);
    return h->palette_failure ? NULL : value;
}
static void color(void *p, ThreeMMI32 index, const unsigned char *bytes)
{
    Host *h = (Host *)p;
    unsigned char expected[4];
    CHECK(index == 20 + h->colors && h->colors < 2);
    put_word(expected, 0x10203040U + (ThreeMMU32)h->colors, 4, h->services.host_big_endian);
    CHECK(!memcmp(bytes, expected, 4));
    ++h->colors;
}
static void resolve(void *p)
{
    Host *h = (Host *)p;
    event(h, RESOLVE);
    CHECK(!h->palette.live);
}
static int append(void *p, ThreeMMU32 number)
{
    Host *h = (Host *)p;
    CHECK(number == 80U + (ThreeMMU32)h->appended);
    if (!effect(h, APPEND0 + h->appended))
        return 0;
    ++h->appended;
    return 1;
}
static int select_camera(void *p, ThreeMMI32 index)
{
    Host *h = (Host *)p;
    CHECK(!index && h->camera.count == 2 && !h->light_list.live);
    ++h->selected;
    return effect(h, SELECT);
}
static void diagnostic(void *p, int code)
{
    Host *h = (Host *)p;
    CHECK(code == THREE_MM_BACKGROUND_READ_ERROR || (code == THREE_MM_BACKGROUND_CAMERA_GAPS && h->gap));
    event(h, DIAG);
    ++h->diagnostics;
}
static void init(Host *h, int big, int profile, int host_big)
{
    int i, j;
    ThreeMMBackgroundServices *s;
    memset(h, 0, sizeof(*h));
    three_mm_background_resource_init(&h->resource);
    three_mm_background_camera_init(&h->camera);
    h->big = big;
    h->profile = profile;
    h->has_palette = h->has_sound = 1;
    h->header_length = 8;
    h->sound_length = 28;
    h->palette.count = h->light_list.count = 2;
    h->palette.stride = 4;
    h->light_list.stride = 56;
    h->light_list.kind = 1;
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
        put_word(h->palette.bytes + i * 4, 0x10203040U + (ThreeMMU32)i, 4, big);
        for (j = 0; j < 12; ++j)
            put_word(h->light_list.bytes + i * 56 + j * 4, (ThreeMMU32)(i * 100 - 500 + j), 4, big);
        put_word(h->light_list.bytes + i * 56 + 48, (ThreeMMU32)(i * 100 + 12), 4, big);
        put_word(h->light_list.bytes + i * 56 + 52, (ThreeMMU32)(i + 1), 4, big);
    }
    s = &h->services;
    s->context = h;
    s->host_big_endian = host_big;
    s->actor_bytes = 6;
    s->light_bytes = 7;
    s->child = child;
    s->child_at = enumerate;
    s->find = find;
    s->stream = stream;
    s->name = name;
    s->read_list = read_list;
    s->count = count;
    s->stride = stride;
    s->get = get;
    s->put = put;
    s->release = release;
    s->allocate = allocate;
    s->free_memory = free_memory;
    s->configure_light = configure;
    s->add_light = add;
    s->enable_light = enable;
    s->disable_light = disable;
    s->remove_light = remove_light;
    s->new_palette = new_palette;
    s->duplicate_palette = duplicate;
    s->palette_color = color;
    s->resolve_palette = resolve;
    s->append_camera = append;
    s->select_camera = select_camera;
    s->diagnostic = diagnostic;
}
static int load(Host *h)
{
    return three_mm_background_read(&h->resource, &h->camera, THREE_MM_BACKGROUND, 42, h->profile, &h->services);
}
static void expect(Host *h, const int *trace, size_t n)
{
    CHECK(h->traces == (int)n && !memcmp(h->trace, trace, n * sizeof(*trace)));
}
static void dispose(Host *h)
{
    three_mm_background_resource_dispose(&h->resource, &h->services);
    CHECK(!h->actor_live && !h->light_live && !h->palette.live && !h->light_list.live);
    CHECK(!h->resource.actors && !h->resource.lights && !h->resource.palette);
}
static void decoder_tests(int big)
{
    Host h;
    ThreeMMReadStream r;
    ThreeMMBackgroundHeader header, before_header;
    ThreeMMBackgroundSound sound, before_sound;
    init(&h, big, 0, 0);
    CHECK(find(&h, THREE_MM_BACKGROUND, 42, 1));
    r = stream(&h);
    CHECK(three_mm_background_header_read(&r, &header) && header.system == 0x1234 && header.palette_base == 20);
    memcpy(&before_header, &header, sizeof(header));
    h.fail = RH;
    CHECK(!three_mm_background_header_read(&r, &header) && !memcmp(&header, &before_header, sizeof(header)));
    h.fail = 0;
    h.header[0] = h.header[1] = 0;
    CHECK(!three_mm_background_header_read(&r, &header));
    CHECK(!memcmp(&header, &before_header, sizeof(header)));
    r.length = 9;
    CHECK(!three_mm_background_header_read(&r, &header));
    CHECK(find(&h, THREE_MM_BACKGROUND_SOUND, THREE_MM_BACKGROUND_SOUND, 1));
    r = stream(&h);
    CHECK(three_mm_background_sound_read(&r, &sound));
    CHECK(sound.system == 0xFEDC && sound.volume == (-2147483647 - 1) && sound.loop && sound.tag.sid == -2);
    CHECK(!sound.tag.cache && sound.tag.type == 0x4D534E44U && sound.tag.number == 0xFEDC1234U);
    memcpy(&before_sound, &sound, sizeof(sound));
    h.fail = RS;
    CHECK(!three_mm_background_sound_read(&r, &sound) && !memcmp(&sound, &before_sound, sizeof(sound)));
    h.fail = 0;
    h.sound[0] = h.sound[1] = 0;
    CHECK(!three_mm_background_sound_read(&r, &sound));
    CHECK(!memcmp(&sound, &before_sound, sizeof(sound)));
    r.length = 27;
    CHECK(!three_mm_background_sound_read(&r, &sound));
}
static void read_tests(int big, int host_big)
{
    Host h;
    int i, j, profile;
    unsigned char expected[4];
    ThreeMMI32 base;
    const int native[] = {C0,  C1,  C2, FH, RH, NAME, CS, FS,  RS,      CP,  FP,      LP,
                          GP0, GP1, CL, FL, LL, AA,   AL, GL0, CONFIG0, GL1, CONFIG1, REL_LIGHT};
    const int on[] = {ADD0, ENABLE0, ADD1, ENABLE1};
    const int off[] = {DISABLE0, REMOVE0, DISABLE1, REMOVE1};
    const int teardown[] = {DISABLE0, REMOVE0, DISABLE1, REMOVE1, FREE_A, FREE_L, REL_PALETTE};
    const int failures[] = {FH, RH, NAME, FS, RS, FP, FL, LL, AA, AL, CONFIG0, CONFIG1};
    init(&h, big, 0, host_big);
    CHECK(load(&h));
    CHECK(h.camera.count == 2 && h.camera.current == -1 && h.resource.light_count == 2);
    CHECK(h.resource.palette == &h.palette && h.resource.palette_base == 20 && h.configured == 2);
    CHECK(h.resource.sound.loop && h.resource.sound.volume == (-2147483647 - 1));
    if (big == host_big)
        expect(&h, native, sizeof(native) / sizeof(*native));
    else
        CHECK(h.trace[13] == PP0 && h.trace[15] == PP1);
    for (i = 0; i < 2; ++i)
    {
        put_word(expected, 0x10203040U + (ThreeMMU32)i, 4, host_big);
        CHECK(!memcmp(h.palette.bytes + i * 4, expected, 4));
    }
    h.traces = 0;
    three_mm_background_lights_on(&h.resource, &h.services);
    CHECK(h.resource.enabled);
    expect(&h, on, 4);
    three_mm_background_lights_on(&h.resource, &h.services);
    expect(&h, on, 4);
    h.resource.leave_on = 1;
    three_mm_background_lights_off(&h.resource, &h.services);
    CHECK(h.resource.enabled);
    expect(&h, on, 4);
    h.resource.leave_on = 0;
    h.traces = 0;
    three_mm_background_lights_off(&h.resource, &h.services);
    CHECK(!h.resource.enabled);
    expect(&h, off, 4);
    three_mm_background_lights_off(&h.resource, &h.services);
    expect(&h, off, 4);
    h.base_output = &base;
    base = -1;
    CHECK(three_mm_background_palette(&h.resource, &base, &h.services) == &h.palette && base == 20);
    h.palette_failure = 1;
    CHECK(!three_mm_background_palette(&h.resource, &base, &h.services) && base == 20);
    three_mm_background_lights_on(&h.resource, &h.services);
    h.traces = 0;
    dispose(&h);
    expect(&h, teardown, 7);
    h.traces = 0;
    dispose(&h);
    CHECK(h.traces == 2 && h.trace[0] == FREE_A && h.trace[1] == FREE_L);
    init(&h, big, 0, host_big);
    h.has_palette = h.has_sound = 0;
    CHECK(load(&h));
    CHECK(h.resource.sound.tag.sid == -1 && !h.resource.palette && !h.resource.palette_base);
    h.base_output = &base;
    base = -1;
    CHECK(three_mm_background_palette(&h.resource, &base, &h.services) == &h.palette && !base);
    dispose(&h);
    init(&h, big, 0, host_big);
    h.fail = LP;
    CHECK(load(&h) && !h.resource.palette && h.resource.palette_base == 20);
    dispose(&h);
    for (i = 0; i < (int)(sizeof(failures) / sizeof(*failures)); ++i)
    {
        init(&h, big, 0, host_big);
        h.fail = failures[i];
        CHECK(!load(&h) && h.diagnostics == 1 && !h.light_list.live);
        if (h.fail == AA || h.fail == AL || h.fail == CONFIG0 || h.fail == CONFIG1)
            CHECK(h.traces >= 2 && h.trace[h.traces - 2] == DIAG && h.trace[h.traces - 1] == REL_LIGHT);
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
    CHECK(!h.allocations && !h.resource.palette && !h.resource.actors && !h.resource.lights && !h.palette.live);
    dispose(&h);
    for (i = 0; i < 6; ++i)
    {
        init(&h, big, 1, host_big);
        h.fail = i == 0 ? LP : i == 1 ? CONFIG0 : i == 2 ? CONFIG1 : i == 3 ? APPEND0 : i == 4 ? APPEND1 : SELECT;
        CHECK(!load(&h) && !h.diagnostics && !h.light_list.live && !h.palette.live);
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
static int initialize(void *p, void *object, ThreeMMU32 type, ThreeMMU32 number)
{
    Host *h = (Host *)p;
    CHECK(object == &h->resource && type == THREE_MM_BACKGROUND && number == 42);
    return load(h);
}
static ThreeMMI32 light_count(void *p, void *object)
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
static void size_out(void *p, ThreeMMI32 bytes)
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
    ThreeMMBackgroundFactoryServices s;
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
    CHECK(three_mm_background_factory(THREE_MM_BACKGROUND, 42, 0, &s));
    CHECK(h.traces == 1 && h.last_size == 113 && !h.created);
    init(&h, 0, 0, 0);
    CHECK(three_mm_background_factory(THREE_MM_BACKGROUND, 42, 1, &s));
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
        CHECK(!three_mm_background_factory(THREE_MM_BACKGROUND, 42, 1, &s) && !h.published && h.last_size == 113);
        CHECK(h.released == (i != 0) && !h.actor_live && !h.light_live && !h.palette.live);
    }
    init(&h, 0, 0, 0);
    s.object_bytes = 2147483647;
    CHECK(!three_mm_background_factory(THREE_MM_BACKGROUND, 42, 0, &s) && !h.traces);
}
typedef struct Tags
{
    int calls, fail, cache_calls;
    ThreeMMU32 types[9];
    ThreeMMTag parent;
} Tags;
static const ThreeMMU32 kinds[] = {THREE_MM_BACKGROUND_SOUND, THREE_MM_BACKGROUND_LIGHTS, THREE_MM_BACKGROUND_COLORS,
                                   THREE_MM_BACKGROUND_CAMERA};
static int insert(void *p, int parent, ThreeMMU32 kind)
{
    Tags *t = (Tags *)p;
    CHECK(parent == (t->calls == 0));
    CHECK(kind == (parent ? 0 : kinds[t->calls - 1]));
    t->types[t->calls++] = kind;
    return t->calls != t->fail;
}
static int tag_child(void *p, ThreeMMU32 kind, ThreeMMTag *out)
{
    Tags *t = (Tags *)p;
    CHECK(!t->cache_calls && t->calls < 4 && kind == kinds[t->calls]);
    t->types[t->calls++] = kind;
    *out = t->parent;
    out->type = kind;
    return t->calls != t->fail;
}
static int cache(void *p, const ThreeMMTag *tag, int children)
{
    Tags *t = (Tags *)p;
    CHECK(t->calls >= 4 && children == (t->cache_calls != 0));
    CHECK(tag->type == (t->cache_calls ? kinds[t->cache_calls - 1] : THREE_MM_BACKGROUND));
    CHECK(tag->sid == 0 && tag->cache == t && tag->number == 42);
    ++t->cache_calls;
    ++t->calls;
    return t->calls != t->fail;
}
static void tag_tests(void)
{
    Tags t;
    ThreeMMBackgroundTagServices s;
    int i;
    memset(&s, 0, sizeof(s));
    s.context = &t;
    s.insert = insert;
    s.child = tag_child;
    s.cache = cache;
    for (i = 0; i <= 5; ++i)
    {
        memset(&t, 0, sizeof(t));
        t.fail = i;
        CHECK(three_mm_background_tags(&s) == !i);
        CHECK(t.calls == (i ? i : 5));
    }
    for (i = 0; i <= 9; ++i)
    {
        memset(&t, 0, sizeof(t));
        t.fail = i;
        t.parent.sid = 0;
        t.parent.cache = &t;
        t.parent.type = THREE_MM_BACKGROUND;
        t.parent.number = 42;
        CHECK(three_mm_background_cache(&t.parent, &s) == !i);
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
    tag_tests();
    puts("Background resource passed");
    return 0;
}
