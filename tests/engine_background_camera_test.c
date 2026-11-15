#include "three_mm/background_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Background camera: %d\n", __LINE__);                                                      \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
enum
{
    LIGHTS = 1,
    READ,
    HEADER,
    RGB,
    DEPTH,
    BACKGROUND,
    RELEASE,
    ALLOCATE,
    LOCK,
    POINT_READ,
    PUT,
    UNLOCK,
    EULER,
    ROTATE,
    TRANSLATE,
    CAMERA,
    DIRTY,
    DIAGNOSTIC
};
typedef struct Points
{
    int live, locked;
    ThreeMMI32 count;
    ThreeMMActorPoint values[2];
} Points;
typedef struct Host
{
    ThreeMMBackgroundCameraState state;
    ThreeMMBackgroundCameraServices services;
    unsigned char bytes[100];
    ThreeMMU32 length;
    Points old_points, new_points;
    int trace[64], traces, fail, fail_point, reads, puts;
} Host;
static void event(Host *h, int value)
{
    CHECK(h->traces < 64);
    h->trace[h->traces++] = value;
}
static int read_at(void *p, ThreeMMU32 offset, void *out, size_t count)
{
    Host *h = (Host *)p;
    event(h, offset ? POINT_READ : HEADER);
    CHECK(offset <= h->length && count <= h->length - offset);
    if ((!offset && h->fail == HEADER) || (offset && h->reads++ == h->fail_point))
    {
        memset(out, 0xA5, count);
        return 0;
    }
    CHECK(offset + count <= sizeof(h->bytes));
    memcpy(out, h->bytes + offset, count);
    return 1;
}
static void word(unsigned char *p, ThreeMMU32 value, int big)
{
    int i;
    for (i = 0; i < 4; ++i)
        p[big ? 3 - i : i] = (unsigned char)(value >> (i * 8));
}
static void wire(Host *h, int big)
{
    int i;
    memset(h->bytes, 0xCC, sizeof(h->bytes));
    h->bytes[0] = (unsigned char)!big;
    h->bytes[1] = (unsigned char)big;
    word(h->bytes + 4, 0x80000000U, big);
    word(h->bytes + 8, 0x7FFFFFFFU, big);
    h->bytes[12] = (unsigned char)(big ? 0xFE : 0xDC);
    h->bytes[13] = (unsigned char)(big ? 0xDC : 0xFE);
    for (i = 0; i < 3; ++i)
        word(h->bytes + 16 + i * 4, (ThreeMMU32)(i - 2), big);
    for (i = 0; i < 12; ++i)
        word(h->bytes + 28 + i * 4, (ThreeMMU32)(i * 100 - 555), big);
    for (i = 0; i < 6; ++i)
        word(h->bytes + 76 + i * 4, (ThreeMMU32)(i * 200 - 666), big);
}
static void lights(void *p)
{
    event((Host *)p, LIGHTS);
}
static int read_camera(void *p, ThreeMMI32 camera, ThreeMMReadStream *r, ThreeMMU32 *number)
{
    Host *h = (Host *)p;
    event(h, READ);
    CHECK(camera == 1);
    r->context = h;
    r->read_at = read_at;
    r->length = h->length;
    *number = 456;
    return h->fail != READ;
}
static int child(void *p, ThreeMMU32 number, int depth, ThreeMMU32 *out)
{
    Host *h = (Host *)p;
    int code = depth ? DEPTH : RGB;
    event(h, code);
    CHECK(number == 456);
    *out = depth ? 789 : 123;
    return h->fail != code;
}
static int background(void *p, ThreeMMU32 rgb, ThreeMMU32 depth)
{
    Host *h = (Host *)p;
    event(h, BACKGROUND);
    CHECK(rgb == 123 && depth == 789);
    CHECK(h->old_points.live && h->state.next_point == 2 && h->state.last_point == 1);
    return h->fail != BACKGROUND;
}
static void *new_points(void *p, ThreeMMI32 count)
{
    Host *h = (Host *)p;
    event(h, ALLOCATE);
    CHECK(!h->old_points.live && !h->state.next_point && !h->state.last_point);
    CHECK(count == 2);
    if (h->fail == ALLOCATE)
        return NULL;
    h->new_points.live = 1;
    h->new_points.count = count;
    return &h->new_points;
}
static void release(void *p, void *points)
{
    Host *h = (Host *)p;
    Points *v = (Points *)points;
    event(h, RELEASE);
    CHECK(v->live && !v->locked);
    v->live = 0;
}
static void lock(void *p, void *points)
{
    Points *v = (Points *)points;
    event((Host *)p, LOCK);
    CHECK(v->live && !v->locked);
    v->locked = 1;
}
static void unlock(void *p, void *points)
{
    Points *v = (Points *)points;
    event((Host *)p, UNLOCK);
    CHECK(v->live && v->locked);
    v->locked = 0;
}
static void put(void *p, void *points, ThreeMMI32 index, const ThreeMMActorPoint *value)
{
    Host *h = (Host *)p;
    Points *v = (Points *)points;
    event(h, PUT);
    CHECK(v->live && v->locked && index >= 0 && index < v->count);
    v->values[index] = *value;
    ++h->puts;
}
static void get(void *p, void *points, ThreeMMI32 index, ThreeMMActorPoint *value)
{
    Points *v = (Points *)points;
    (void)p;
    CHECK(v->live && !v->locked && index >= 0 && index < v->count);
    *value = v->values[index];
}
static ThreeMMI32 count(void *p, void *points)
{
    (void)p;
    return ((Points *)points)->count;
}
static void euler(void *p, const ThreeMMActorMatrix *m, unsigned short *a, unsigned short *c)
{
    Host *h = (Host *)p;
    event(h, EULER);
    CHECK(h->state.current == 1 && h->state.place.dxr == -2 && h->state.position.dxr == 345);
    CHECK(m->m[0][0] == -555 && m->m[3][2] == 545);
    *a = 0xFFFF;
    *c = 0x2345;
}
static void rotate(void *p, ThreeMMActorMatrix *out, unsigned short yaw)
{
    Host *h = (Host *)p;
    event(h, ROTATE);
    CHECK(yaw == 0x2344 && h->state.yaw == yaw);
    memset(out, 0, sizeof(*out));
    out->m[0][0] = 1234;
}
static void translate(void *p, ThreeMMActorMatrix *out, const ThreeMMActorPoint *v)
{
    event((Host *)p, TRANSLATE);
    CHECK(out->m[0][0] == 1234 && v->dxr == 345 && v->dyr == 445 && v->dzr == 545);
    out->m[3][0] = v->dxr;
    out->m[3][1] = v->dyr;
    out->m[3][2] = v->dzr;
}
static void camera(void *p, const ThreeMMBackgroundCameraData *d)
{
    Host *h = (Host *)p;
    event(h, CAMERA);
    CHECK(h->state.mouse.m[3][2] == 545);
    CHECK(d->near_z == (-2147483647 - 1) && d->far_z == 2147483647 && d->fov == 0xFEDC);
}
static void dirty(void *p)
{
    event((Host *)p, DIRTY);
}
static void diagnostic(void *p, int issue)
{
    Host *h = (Host *)p;
    event(h, DIAGNOSTIC);
    CHECK(issue == (h->length < 76 ? THREE_MM_BACKGROUND_CAMERA_SHORT : THREE_MM_BACKGROUND_CAMERA_TAIL));
}
static void init(Host *h, int big)
{
    ThreeMMBackgroundCameraServices *s;
    memset(h, 0, sizeof(*h));
    three_mm_background_camera_init(&h->state);
    CHECK(h->state.current == -1 && !h->state.points && !h->state.count);
    h->state.count = 3;
    h->state.current = 2;
    h->state.next_point = 2;
    h->state.last_point = 1;
    h->state.place.dxr = 777;
    h->state.position.dxr = 888;
    h->state.mouse.m[0][0] = 999;
    h->state.yaw = 0x4444;
    h->state.points = &h->old_points;
    h->old_points.live = 1;
    h->length = 100;
    h->fail_point = -1;
    wire(h, big);
    s = &h->services;
    s->context = h;
    s->lights_on = lights;
    s->read = read_camera;
    s->child = child;
    s->background = background;
    s->new_points = new_points;
    s->release = release;
    s->lock = lock;
    s->unlock = unlock;
    s->put_point = put;
    s->point = get;
    s->point_count = count;
    s->euler_yxy = euler;
    s->rotate_y = rotate;
    s->translate = translate;
    s->camera = camera;
    s->dirty = dirty;
    s->diagnostic = diagnostic;
}
static void expect(Host *h, const int *values, int n)
{
    CHECK(h->traces == n && !memcmp(h->trace, values, n * sizeof(*values)));
}
static void unchanged(Host *h)
{
    CHECK(h->state.current == 2 && h->state.place.dxr == 777 && h->state.position.dxr == 888);
    CHECK(h->state.mouse.m[0][0] == 999 && h->state.yaw == 0x4444 && h->state.count == 3);
}
static void decode_tests(int big)
{
    Host h;
    ThreeMMReadStream r;
    ThreeMMBackgroundCameraData d, before;
    ThreeMMActorPoint v, previous;
    int i;
    init(&h, big);
    r.context = &h;
    r.read_at = read_at;
    r.length = h.length;
    CHECK(three_mm_background_camera_decode(&r, &d));
    CHECK(d.big == big && d.point_count == 2);
    CHECK(d.near_z == (-2147483647 - 1) && d.far_z == 2147483647 && d.fov == 0xFEDC);
    CHECK(d.place.dxr == -2 && d.place.dyr == -1 && !d.place.dzr);
    for (i = 0; i < 12; ++i)
        CHECK(d.matrix.m[i / 3][i % 3] == i * 100 - 555);
    for (i = 0; i < 2; ++i)
    {
        CHECK(three_mm_background_point_decode(&r, &d, i, &v));
        CHECK(v.dxr == i * 600 - 666 && v.dyr == i * 600 - 466 && v.dzr == i * 600 - 266);
    }
    previous = v;
    CHECK(!three_mm_background_point_decode(&r, &d, -1, &v));
    CHECK(!memcmp(&v, &previous, sizeof(v)));
    CHECK(!three_mm_background_point_decode(&r, &d, 2, &v));
    CHECK(!memcmp(&v, &previous, sizeof(v)));
    r.length = 88;
    CHECK(!three_mm_background_point_decode(&r, &d, 1, &v));
    CHECK(!memcmp(&v, &previous, sizeof(v)));
    r.length = 100;
    h.reads = 0;
    h.fail_point = 0;
    CHECK(!three_mm_background_point_decode(&r, &d, 0, &v));
    CHECK(!memcmp(&v, &previous, sizeof(v)));
    memcpy(&before, &d, sizeof(before));
    h.fail = HEADER;
    CHECK(!three_mm_background_camera_decode(&r, &d));
    CHECK(!memcmp(&d, &before, sizeof(d)));
    h.fail = 0;
    h.bytes[0] = h.bytes[1] = 0;
    CHECK(!three_mm_background_camera_decode(&r, &d));
    CHECK(!memcmp(&d, &before, sizeof(d)));
    wire(&h, big);
    r.length = 75;
    CHECK(!three_mm_background_camera_decode(&r, &d));
    CHECK(!memcmp(&d, &before, sizeof(d)));
    r.length = 77;
    CHECK(!three_mm_background_camera_decode(&r, &d));
    CHECK(!memcmp(&d, &before, sizeof(d)));
    r.length = 76;
    CHECK(three_mm_background_camera_decode(&r, &d) && !d.point_count);
    /* Maximum wire length: count and offsets remain in range without allocation. */
    r.length = 4294967284U;
    CHECK(three_mm_background_camera_decode(&r, &d) && d.point_count == 357913934);
    d.point_count = 2147483647;
    r.length = 76;
    CHECK(!three_mm_background_point_decode(&r, &d, 2147483646, &v));
    CHECK(!memcmp(&v, &previous, sizeof(v)));
}
static void controller_tests(int big)
{
    Host h;
    ThreeMMActorPoint v;
    int i;
    const int success[] = {LIGHTS, READ,       HEADER, RGB,    DEPTH, BACKGROUND, RELEASE,   ALLOCATE, LOCK, POINT_READ,
                           PUT,    POINT_READ, PUT,    UNLOCK, EULER, ROTATE,     TRANSLATE, CAMERA,   DIRTY};
    const int no_points[] = {LIGHTS,  READ,  HEADER, RGB,       DEPTH,  BACKGROUND,
                             RELEASE, EULER, ROTATE, TRANSLATE, CAMERA, DIRTY};
    const int no_memory[] = {LIGHTS,   READ,  HEADER, RGB,       DEPTH,  BACKGROUND, RELEASE,
                             ALLOCATE, EULER, ROTATE, TRANSLATE, CAMERA, DIRTY};
    const int failed_point[] = {LIGHTS,  READ,     HEADER, RGB,        DEPTH,  BACKGROUND,
                                RELEASE, ALLOCATE, LOCK,   POINT_READ, UNLOCK, RELEASE};
    const int failed_second[] = {LIGHTS,   READ, HEADER,     RGB, DEPTH,      BACKGROUND, RELEASE,
                                 ALLOCATE, LOCK, POINT_READ, PUT, POINT_READ, UNLOCK,     RELEASE};
    init(&h, big);
    CHECK(three_mm_background_camera_set(&h.state, 1, &h.services));
    expect(&h, success, sizeof(success) / sizeof(*success));
    CHECK(h.state.points == &h.new_points && h.puts == 2);
    CHECK(h.state.count == 3 && !h.state.last_point && !h.state.next_point);
    for (i = 0; i < 7; ++i)
    {
        three_mm_background_place(&h.state, &v, &h.services);
        CHECK(v.dxr == (i % 3 == 0 ? -2 : i % 3 == 1 ? -666 : -66));
        if (i == 1)
        {
            three_mm_background_reuse_place(&h.state);
            three_mm_background_place(&h.state, &v, &h.services);
            CHECK(v.dxr == -666);
        }
    }
    h.traces = 0;
    three_mm_background_camera_dispose(&h.state, &h.services);
    CHECK(!h.state.points && !h.new_points.live && h.traces == 1 && h.trace[0] == RELEASE);
    CHECK(!h.state.next_point && !h.state.last_point);
    three_mm_background_place(&h.state, &v, &h.services);
    CHECK(v.dxr == -2 && !h.state.next_point);
    three_mm_background_camera_dispose(&h.state, &h.services);
    CHECK(h.traces == 1);
    for (i = READ; i <= BACKGROUND; ++i)
    {
        init(&h, big);
        h.fail = i;
        CHECK(!three_mm_background_camera_set(&h.state, 1, &h.services));
        expect(&h, success, i);
        unchanged(&h);
        CHECK(h.state.points == &h.old_points && h.old_points.live);
        CHECK(h.state.next_point == 2 && h.state.last_point == 1);
    }
    for (i = 0; i < 2; ++i)
    {
        init(&h, big);
        h.length = i ? 77 : 75;
        CHECK(!three_mm_background_camera_set(&h.state, 1, &h.services));
        CHECK(h.traces == 3 && h.trace[0] == LIGHTS && h.trace[1] == READ && h.trace[2] == DIAGNOSTIC);
        unchanged(&h);
        CHECK(h.old_points.live);
    }
    init(&h, big);
    h.bytes[0] = h.bytes[1] = 0;
    CHECK(!three_mm_background_camera_set(&h.state, 1, &h.services));
    expect(&h, success, 3);
    unchanged(&h);
    CHECK(h.old_points.live);
    for (i = 0; i < 2; ++i)
    {
        init(&h, big);
        h.fail_point = i;
        CHECK(!three_mm_background_camera_set(&h.state, 1, &h.services));
        if (i)
            expect(&h, failed_second, sizeof(failed_second) / sizeof(*failed_second));
        else
            expect(&h, failed_point, sizeof(failed_point) / sizeof(*failed_point));
        unchanged(&h);
        CHECK(!h.state.points && !h.new_points.live && !h.new_points.locked && h.puts == i);
        CHECK(!h.state.next_point && !h.state.last_point);
    }
    for (i = 0; i < 2; ++i)
    {
        init(&h, big);
        if (i)
            h.length = 76;
        else
            h.fail = ALLOCATE;
        CHECK(three_mm_background_camera_set(&h.state, 1, &h.services));
        CHECK(!h.state.points);
        if (i)
            expect(&h, no_points, sizeof(no_points) / sizeof(*no_points));
        else
            expect(&h, no_memory, sizeof(no_memory) / sizeof(*no_memory));
        three_mm_background_reuse_place(&h.state);
        three_mm_background_place(&h.state, &v, &h.services);
        CHECK(v.dxr == -2 && !h.state.next_point);
        three_mm_background_place(&h.state, &v, &h.services);
        CHECK(v.dxr == -2 && !h.state.last_point);
    }
}
int main(void)
{
    decode_tests(0);
    decode_tests(1);
    controller_tests(0);
    controller_tests(1);
    puts("Background camera passed");
    return 0;
}
