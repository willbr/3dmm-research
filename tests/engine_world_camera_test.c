#include "three_mm/world_camera.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAXIMUM 2147483647
#define MINIMUM (-2147483647 - 1)
typedef struct Fixture
{
    ThreeMMWorldCamera *camera;
    ThreeMMWorldCamera published;
    int order, calls, actor_a, actor_b;
    ThreeMMI32 x, y;
} Fixture;
static void publish(void *context, const ThreeMMWorldCamera *w)
{
    Fixture *f = (Fixture *)context;
    assert(f->order++ == 0);
    assert(w == f->camera);
    f->published = *w;
}
static void dirty(void *context)
{
    Fixture *f = (Fixture *)context;
    assert(f->order++ == 1);
    assert(f->published.fov == f->camera->fov);
}
static void pick(void *context, ThreeMMI32 x, ThreeMMI32 y)
{
    Fixture *f = (Fixture *)context;
    ++f->calls;
    f->x = x;
    f->y = y;
    assert(f->camera->picked == NULL && f->camera->pick_depth == MAXIMUM);
    assert(!three_mm_world_pick_visit(f->camera, &f->actor_a, MAXIMUM));
    assert(f->camera->picked == NULL);
    assert(!three_mm_world_pick_visit(f->camera, &f->actor_a, 20));
    assert(!three_mm_world_pick_visit(f->camera, &f->actor_b, 20));
    assert(f->camera->picked == &f->actor_a);
    assert(!three_mm_world_pick_visit(f->camera, &f->actor_b, MINIMUM));
    assert(f->camera->picked == &f->actor_b);
}
static void camera_tests(void)
{
    ThreeMMWorldCamera w;
    ThreeMMWorldCameraServices s;
    ThreeMMActorMatrix matrix, result;
    ThreeMMI32 near_z, far_z;
    unsigned short fov;
    Fixture f;
    int i, mask;
    memset(&f, 0, sizeof(f));
    f.camera = &w;
    s.context = &f;
    s.publish = publish;
    s.dirty = dirty;
    s.pick = pick;
    three_mm_world_camera_init(&w);
    assert(!w.picked && !w.fov && !w.pick_depth && !w.near_z && !w.far_z);
    for (i = 0; i < 12; ++i)
        matrix.m[i / 3][i % 3] = i % 2 ? MINIMUM + i : MAXIMUM - i;
    three_mm_world_camera_set(&w, &matrix, MINIMUM, MAXIMUM, 65535, &s);
    assert(f.order == 2);
    for (mask = 0; mask < 8; ++mask)
    {
        near_z = far_z = 123;
        fov = 123;
        three_mm_world_camera_get(&w, &result, mask & 1 ? &near_z : NULL, mask & 2 ? &far_z : NULL,
                                  mask & 4 ? &fov : NULL);
        assert(!memcmp(&matrix, &result, sizeof(matrix)));
        assert(near_z == (mask & 1 ? MINIMUM : 123));
        assert(far_z == (mask & 2 ? MAXIMUM : 123));
        assert(fov == (mask & 4 ? 65535 : 123));
    }
    /* The SDK mirror may be overwritten; it must not become authoritative. */
    memset(&f.published, 0, sizeof(f.published));
    assert(w.fov == 65535 && w.matrix.m[0][0] == MAXIMUM);
    f.order = 0;
    three_mm_world_camera_set(&w, &w.matrix, -1, 1, 0, &s);
    assert(!memcmp(&matrix, &w.matrix, sizeof(matrix)));
}
static void point(const ThreeMMWorldBuffers *b, ThreeMMI32 ox, ThreeMMI32 oy, ThreeMMI32 x, ThreeMMI32 y, int valid,
                  ThreeMMI32 expected_x, ThreeMMI32 expected_y)
{
    ThreeMMI32 rx = 123, ry = 456;
    assert(three_mm_world_pick_point(b, ox, oy, x, y, &rx, &ry) == valid);
    assert(rx == (valid ? expected_x : 123));
    assert(ry == (valid ? expected_y : 456));
}
static void coordinate_tests(void)
{
    ThreeMMWorldBuffers b;
    ThreeMMWorldCamera w;
    ThreeMMWorldCameraServices s;
    Fixture f;
    int scale, hx, hy, i;
    const ThreeMMI32 values[] = {-9, -8, -3, -2, -1, 0, 1, 2, 3, 8, 9};
    const ThreeMMI32 halves[] = {-4, -4, -1, -1, 0, 0, 0, 1, 1, 4, 4};
    memset(&b, 0, sizeof(b));
    for (scale = 1; scale <= 4; scale *= 2)
        for (hx = 0; hx <= 1; ++hx)
            for (hy = 0; hy <= 1; ++hy)
                for (i = 0; i < 11; ++i)
                {
                    ThreeMMI32 x, y;
                    b.scale = scale;
                    b.half_x = hx;
                    b.half_y = hy;
                    x = scale > 1 ? values[i] * scale : hx ? halves[i] : values[i];
                    y = scale > 1 ? values[i] * scale : hy ? halves[i] : values[i];
                    point(&b, 3, -2, values[i], values[i], 1, x - 3, y + 2);
                }
    b.scale = 1;
    b.half_x = b.half_y = 0;
    point(&b, 0, 0, MINIMUM, MAXIMUM, 1, MINIMUM, MAXIMUM);
    point(&b, 1, 0, MINIMUM, 0, 0, 0, 0);
    point(&b, 0, -1, 0, MAXIMUM, 0, 0, 0);
    point(&b, MINIMUM, MAXIMUM, MINIMUM, MAXIMUM, 1, 0, 0);
    point(&b, MAXIMUM, MINIMUM, MINIMUM, MAXIMUM, 0, 0, 0);
    b.half_x = b.half_y = 1;
    point(&b, 0, 0, MINIMUM, MAXIMUM, 1, -1073741824, 1073741823);
    b.scale = 2;
    point(&b, 0, 0, -1073741824, 1073741823, 1, MINIMUM, MAXIMUM - 1);
    point(&b, 0, 0, -1073741825, 0, 0, 0, 0);
    point(&b, 0, 0, 0, 1073741824, 0, 0, 0);
    b.scale = 4;
    point(&b, 0, 0, -536870912, 536870911, 1, MINIMUM, MAXIMUM - 3);
    point(&b, 0, 0, -536870913, 0, 0, 0, 0);
    point(&b, 0, 0, 0, 536870912, 0, 0, 0);
    b.scale = 3;
    point(&b, 0, 0, 0, 0, 0, 0, 0);
    memset(&f, 0, sizeof(f));
    f.camera = &w;
    memset(&s, 0, sizeof(s));
    s.context = &f;
    s.pick = pick;
    three_mm_world_camera_init(&w);
    assert(three_mm_world_pick(&w, &b, 0, 0, 10, 20, &s) == NULL);
    assert(!f.calls && w.pick_depth == MAXIMUM);
    b.scale = 2;
    assert(three_mm_world_pick(&w, &b, 3, -2, -9, 20, &s) == &f.actor_b);
    assert(f.calls == 1 && f.x == -21 && f.y == 42 && w.pick_depth == MINIMUM);
    assert(!three_mm_world_pick(&w, &b, 0, 0, MAXIMUM, 20, &s));
    assert(f.calls == 1 && !w.picked && w.pick_depth == MAXIMUM);
}
static void bounds_tests(void)
{
    const ThreeMMI32 input[4] = {-8, -5, -3, -2};
    const ThreeMMI32 expected[3][4] = {{-8, -5, -2, -1}, {-4, -2, 0, 0}, {-2, -1, 0, 0}};
    ThreeMMI32 result[4], limit[4];
    int i, scale;
    for (i = 0, scale = 1; i < 3; ++i, scale *= 2)
    {
        assert(three_mm_world_rendered_bounds(scale, input, result));
        assert(!memcmp(result, expected[i], sizeof(result)));
        limit[0] = limit[1] = MINIMUM;
        limit[2] = limit[3] = MAXIMUM - scale;
        assert(three_mm_world_rendered_bounds(scale, limit, limit));
        assert(limit[0] == MINIMUM / scale && limit[2] == MAXIMUM / scale);
        memset(result, 0x35, sizeof(result));
        limit[2] = MAXIMUM - scale + 1;
        assert(!three_mm_world_rendered_bounds(scale, limit, result));
        assert(result[0] == 0x35353535 && result[3] == 0x35353535);
    }
    assert(!three_mm_world_rendered_bounds(0, input, result));
    assert(!three_mm_world_rendered_bounds(3, input, result));
}
typedef struct Actor
{
    struct Actor *parent;
    void *owner;
} Actor;
static void *parent(void *a)
{
    return ((Actor *)a)->parent;
}
static void *owner(void *a)
{
    return ((Actor *)a)->owner;
}
int main(void)
{
    Actor root, branch, leaf;
    int world;
    root.parent = NULL;
    root.owner = &world;
    branch.parent = &root;
    branch.owner = NULL;
    leaf.parent = &branch;
    leaf.owner = NULL;
    assert(three_mm_world_actor_owner(&leaf, parent, owner) == &world);
    assert(three_mm_world_actor_owner(&root, parent, owner) == &world);
    assert(!three_mm_world_actor_owner(NULL, parent, owner));
    root.owner = NULL;
    assert(!three_mm_world_actor_owner(&branch, parent, owner));
    camera_tests();
    coordinate_tests();
    bounds_tests();
    puts("C89 World camera: publication, picking, bounds and overflow passed.");
    return 0;
}
