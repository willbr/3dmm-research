#include "three_mm/actor_placement.h"
#include "three_mm/actor_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"placement check failed at %d: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef struct Fixture
{
    ThreeMMActorMatrix matrix;
    ThreeMMI32 hither, yon;
    int calls;
} Fixture;
static void camera(void *context, ThreeMMActorMatrix *matrix, ThreeMMI32 *hither, ThreeMMI32 *yon)
{
    Fixture *f = (Fixture *)context;
    ++f->calls;
    *matrix = f->matrix;
    *hither = f->hither;
    *yon = f->yon;
}
int main(void)
{
    Fixture f;
    ThreeMMActorPlacementServices services;
    ThreeMMActorPoint point;
    int axis;
    memset(&f, 0, sizeof(f));
    services.context = &f;
    services.camera = camera;
    three_mm_matrix_identity(&f.matrix);
    /* Placement must retain even a single authored nonzero coordinate without
     * touching the camera. In particular, authored Y is not put on the floor. */
    for (axis = 0; axis < 3; ++axis)
    {
        point.dxr = axis == 0 ? -1 : 0;
        point.dyr = axis == 1 ? -65536L : 0;
        point.dzr = axis == 2 ? 1 : 0;
        three_mm_actor_placement(&point, 1, &services);
        CHECK(f.calls == 0);
        CHECK(point.dxr == (axis == 0 ? -1 : 0));
        CHECK(point.dyr == (axis == 1 ? 589824L : 655360L));
        CHECK(point.dzr == (axis == 2 ? 1 : 0));
    }
    point.dxr = 1; point.dyr = 2147483647L; point.dzr = 0;
    three_mm_actor_placement(&point, 1, &services);
    CHECK(point.dyr == -2146828289L && f.calls == 0);
    /* Exactly 25 units uses the original distance, rather than the midpoint. */
    f.hither = 65536L; f.yon = 1638400L;
    point.dxr = point.dyr = point.dzr = 0;
    three_mm_actor_placement(&point, 0, &services);
    CHECK(f.calls == 1 && point.dxr == 0 && point.dyr == 0 && point.dzr == -1638400L);
    f.yon = 1638399L;
    point.dxr = point.dyr = point.dzr = 0;
    three_mm_actor_placement(&point, 1, &services);
    CHECK(f.calls == 2 && point.dyr == 655360L && point.dzr == -851967L);
    /* Division rounds the clipping midpoint toward zero at subpixel distances. */
    f.hither = 0; f.yon = 3;
    point.dxr = point.dyr = point.dzr = 0;
    three_mm_actor_placement(&point, 0, &services);
    CHECK(point.dzr == -1 && f.calls == 3);
    f.yon = 0;
    point.dxr = point.dyr = point.dzr = 0;
    three_mm_actor_placement(&point, 0, &services);
    CHECK(point.dzr == 0 && f.calls == 4);
    /* Camera translation and projection use X/Z only. Camera Y is ignored. */
    f.yon = 3276800L;
    f.matrix.m[2][0] = -65536L; f.matrix.m[2][2] = 0;
    f.matrix.m[3][0] = 65536L; f.matrix.m[3][1] = 999; f.matrix.m[3][2] = -65536L;
    point.dxr = point.dyr = point.dzr = 0;
    three_mm_actor_placement(&point, 1, &services);
    CHECK(point.dxr == 1703936L && point.dyr == 655360L && point.dzr == -65536L);
    /* Translation wraps at the BRender scalar width. */
    f.matrix.m[3][0] = 2147483647L;
    point.dxr = point.dyr = point.dzr = 0;
    three_mm_actor_placement(&point, 0, &services);
    CHECK(point.dxr == -2145845249L);
    puts("actor placement checks passed");
    return 0;
}
