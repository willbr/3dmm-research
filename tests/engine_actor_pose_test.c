#include "three_mm/actor_pose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ONE 65536L
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "actor pose failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Add
{
    ThreeMMActorPoint translation;
    unsigned short x, y, z, padding;
} Add;
typedef struct Tables
{
    ThreeMMActorRoutePoint route[5];
    ThreeMMActorEvent event;
    Add add;
    int count, reads;
} Tables;
static ThreeMMU32 route_width(void *p)
{
    (void)p;
    return 16;
}
static ThreeMMI32 route_count(void *p)
{
    return ((Tables *)p)->count;
}
static void point(void *p, ThreeMMI32 i, ThreeMMActorRoutePoint *out)
{
    Tables *t = (Tables *)p;
    CHECK(i >= 0 && i < t->count);
    ++t->reads;
    *out = t->route[i];
}
static ThreeMMU32 event_width(void *p)
{
    (void)p;
    return 20;
}
static ThreeMMI32 event_count(void *p)
{
    (void)p;
    return 1;
}
static void event(void *p, ThreeMMI32 i, ThreeMMActorEvent *out)
{
    CHECK(i == 0);
    *out = ((Tables *)p)->event;
}
static ThreeMMI32 size(void *p, ThreeMMI32 i)
{
    (void)p;
    CHECK(i == 0);
    return 20;
}
static void payload(void *p, ThreeMMI32 i, void *out)
{
    CHECK(i == 0 && sizeof(Add) == 20);
    memcpy(out, &((Tables *)p)->add, 20);
}
static ThreeMMActorMotionServices services = {route_width, route_count, point, event_width,
                                              event_count, event,       size,  payload};
static void arithmetic(void)
{
    ThreeMMActorMatrix a, b, out;
    CHECK(three_mm_fixed_mac3(1, 32768, 1, 32768, 0, 0) == 1);
    CHECK(three_mm_fixed_mac3(-1, 32768, 1, 32768, 0, 0) == 0);
    CHECK(three_mm_fixed_mac3(-1, 1, 0, 0, 0, 0) == -1);
    CHECK(three_mm_fixed_mac3(2147483647L, 2147483647L, 2147483647L, 2147483647L, 2147483647L, 2147483647L) ==
          -196608L);
    CHECK(three_mm_angle_sin(0) == 0 && three_mm_angle_cos(0) == 65534L);
    CHECK(three_mm_angle_sin(0x4000) == 65534L && three_mm_angle_cos(0x4000) == 0);
    CHECK(three_mm_angle_sin(0x8000) == 0 && three_mm_angle_cos(0x8000) == -65534L);
    CHECK(three_mm_angle_sin(0xc000) == -65534L && three_mm_angle_cos(0xc000) == 0);
    CHECK(three_mm_angle_sin(0x2000) == 46338L);
    CHECK(three_mm_angle_atan2(0, 0) == 0 && three_mm_angle_atan2(ONE, 0) == 0x4000);
    CHECK(three_mm_angle_atan2(-ONE, 0) == 0xc000 && three_mm_angle_atan2(0, -ONE) == 0x8000);
    CHECK(three_mm_angle_atan2(ONE, ONE) == 0x2000 && three_mm_angle_atan2(-ONE, ONE) == 0xe000);
    CHECK(three_mm_angle_atan2(ONE, -ONE) == 0x6000 && three_mm_angle_atan2(-ONE, -ONE) == 0xa000);
    CHECK(three_mm_angle_atan2((-2147483647L - 1), (-2147483647L - 1)) == 0xa000);
    three_mm_matrix_identity(&a);
    three_mm_matrix_identity(&b);
    a.m[0][0] = a.m[0][1] = 1;
    a.m[0][2] = 0;
    b.m[0][0] = b.m[1][0] = 32768;
    three_mm_matrix_multiply(&out, &a, &b);
    CHECK(out.m[0][0] == 1);
    three_mm_matrix_identity(&a);
    three_mm_matrix_identity(&b);
    a.m[3][0] = ONE;
    a.m[3][1] = 2 * ONE;
    a.m[3][2] = 3 * ONE;
    b.m[0][0] = 2 * ONE;
    b.m[1][1] = 3 * ONE;
    b.m[2][2] = 4 * ONE;
    b.m[3][0] = 5 * ONE;
    b.m[3][1] = 6 * ONE;
    b.m[3][2] = 7 * ONE;
    three_mm_matrix_multiply(&out, &a, &b);
    CHECK(out.m[3][0] == 7 * ONE && out.m[3][1] == 12 * ONE && out.m[3][2] == 19 * ONE);
    three_mm_matrix_identity(&a);
    three_mm_matrix_rotate(&a, THREE_MM_MATRIX_X, 0, 0);
    CHECK(a.m[0][0] == ONE && a.m[1][1] == 65534L);
    three_mm_matrix_rotate(&a, THREE_MM_MATRIX_Y, 0, 0);
    three_mm_matrix_rotate(&a, THREE_MM_MATRIX_Z, 0, 0);
    CHECK(a.m[0][0] == 65532L && a.m[1][1] == 65532L && a.m[2][2] == 65532L);
    three_mm_matrix_scale(&a, 2 * ONE, 3 * ONE, 4 * ONE);
    CHECK(a.m[0][0] == 2 * 65532L && a.m[1][1] == 3 * 65532L && a.m[2][2] == 4 * 65532L);
}
static void vectors(void)
{
    ThreeMMActorPoint p = {ONE, ONE, 0}, saved = p;
    ThreeMMActorMatrix m;
    ThreeMMU32 axes = 99;
    unsigned short x = 123, y = 123, z = 123;
    three_mm_matrix_identity(&m);
    three_mm_actor_pose_vector(THREE_MM_ACTOR_ROTATE_X, &p, &m, &x, &y, &z, &axes);
    CHECK(x == 0xe000 && !y && !z && axes == 1 && !memcmp(&p, &saved, sizeof(p)));
    x = y = z = 123;
    three_mm_actor_pose_vector(1, &p, NULL, NULL, NULL, &z, NULL);
    CHECK(x == 123 && y == 123 && !z);
    three_mm_actor_pose_vector(3, &p, NULL, &x, &y, &z, &axes);
    CHECK(x == 0xe000 && y == 0x4000 && !z && axes == 3);
    p.dxr = p.dyr = p.dzr = 0;
    three_mm_matrix_identity(&m);
    saved = p;
    three_mm_actor_pose_vector(7, &p, &m, &x, &y, &z, &axes);
    CHECK(!x && !y && !z && !axes && m.m[0][0] == ONE);
    p.dxr = p.dyr = p.dzr = 1;
    three_mm_actor_pose_vector(3, &p, &m, &x, &y, &z, &axes);
    CHECK(!x && !y && !z && axes == 3 && m.m[0][0] < ONE); /* Axis rotations still happen for rounded zero angles. */
    p.dxr = p.dyr = p.dzr = ONE;
    three_mm_actor_pose_vector(3, &p, NULL, &x, &y, &z, (ThreeMMU32 *)&p.dyr);
    CHECK(p.dyr == 3 && x == 0x8000 && y == 0x2000 && !z);
}
static void poses(void)
{
    Tables t;
    ThreeMMActorState state;
    ThreeMMActorPoseInputs inputs = {0, 0, 0, 0};
    ThreeMMActorPoint position = {2 * ONE, 0, 0};
    ThreeMMActorMatrix m, moving, rotation;
    unsigned short x, y, z;
    ThreeMMU32 axes;
    memset(&t, 0, sizeof(t));
    t.count = 2;
    t.route[0].dwr = 4 * ONE;
    t.route[1].xyz.dxr = 4 * ONE;
    t.event.aet = THREE_MM_ACTOR_ADD;
    t.event.nfrm = 1;
    t.add.y = 0x2000;
    three_mm_actor_state_init(&state);
    state.addEvent = 0;
    state.actionFlags = THREE_MM_ACTOR_ROTATE_Y;
    state.frame = 2;
    state.location.dwrOffset = 2 * ONE;
    three_mm_actor_pose_update(&state, &t, &t, &inputs, &position, &m, &services);
    CHECK(state.position.dxr == 2 * ONE && state.transform.yaPath == 0x4000 && t.reads > 0);
    moving = m;
    rotation = state.transform.bmat34Cur;
    state.useCurrentRotation = 1;
    three_mm_actor_pose_update(&state, NULL, NULL, &inputs, &state.position, &m, NULL);
    CHECK(!memcmp(&m, &moving, sizeof(m)));
    state.useCurrentRotation = 0;
    three_mm_actor_pose_update(&state, &t, &t, &inputs, &position, &state.transform.bmat34Cur, &services);
    CHECK(!memcmp(&state.transform.bmat34Cur, &rotation, sizeof(rotation)));
    inputs.fixedPathOrientation = 1;
    three_mm_actor_pose_update(&state, &t, &t, &inputs, &position, &m, &services);
    CHECK(state.transform.yaPath == 0x2000);
    inputs.fixedPathOrientation = 0;
    state.frame = 100;
    state.location.dwrOffset = 0;
    three_mm_actor_pose_update(&state, &t, &t, &inputs, &position, &m, &services);
    CHECK(state.transform.yaPath == 0x2000);
    state.location.irpt = 1;
    three_mm_actor_pose_update(&state, &t, &t, &inputs, &position, &m, &services);
    CHECK(state.transform.yaPath == 0x4000);
    three_mm_actor_state_init_transform(&state);
    state.useCurrentRotation = 1;
    state.transform.aevpull.rScaleX = 3 * ONE;
    state.transform.rScaleStep = 2 * ONE;
    three_mm_actor_pose_update(&state, NULL, NULL, &inputs, &position, &m, NULL);
    CHECK(m.m[0][0] == 6 * 65532L && m.m[1][1] == 2 * 65532L && m.m[2][2] == 2 * 65532L);
    /* Weighted tangents exclude the preceding unrelated subroute. */
    memset(&t, 0, sizeof(t));
    t.count = 5;
    t.event.rtel.irpt = 1;
    t.route[0].xyz.dxr = t.route[0].xyz.dzr = 100 * ONE;
    t.route[1].dwr = t.route[2].dwr = t.route[3].dwr = 4 * ONE;
    t.route[2].xyz.dzr = t.route[3].xyz.dzr = 4 * ONE;
    t.route[3].xyz.dxr = t.route[4].xyz.dxr = 4 * ONE;
    state.location.irpt = 3;
    state.location.dwrOffset = 2 * ONE;
    state.actionFlags = 2;
    three_mm_actor_pose_orientation(&state, &t, &t, NULL, &x, &y, &z, &axes, &services);
    CHECK(y > 26396 && y < 26943 && !x && !z && axes == 2);
    state.location.irpt = 2;
    three_mm_actor_pose_orientation(&state, &t, &t, NULL, &x, &y, &z, &axes, &services);
    CHECK(y > 11286 && y < 11833);
    memset(&t, 0, sizeof(t));
    t.count = 1;
    state.location.irpt = 0;
    state.location.dwrOffset = 0;
    three_mm_matrix_identity(&m);
    moving = m;
    three_mm_actor_pose_orientation(&state, &t, &t, &m, &x, &y, &z, &axes, &services);
    CHECK(!x && !y && !z && !axes && !memcmp(&m, &moving, sizeof(m)));
}
int main(void)
{
    arithmetic();
    vectors();
    poses();
    puts("actor pose and math passed");
    return 0;
}
