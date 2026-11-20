#include "mm/actor_placement.h"
#include "mm/actor_math.h"
#include "mm/actor_owner.h"
#include "mm/scene_content.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"placement check failed at %d: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef struct Fixture
{
    MMActorMatrix matrix;
    MMI32 hither, yon;
    int calls;
} Fixture;
static void camera(void *context, MMActorMatrix *matrix, MMI32 *hither, MMI32 *yon)
{
    Fixture *f = (Fixture *)context;
    ++f->calls;
    *matrix = f->matrix;
    *hither = f->hither;
    *yon = f->yon;
}

typedef struct PlaceActor
{
    int refs;
    MMI32 id;
} PlaceActor;

typedef struct PlaceFixture
{
    PlaceActor actor, snapshot;
    MMSceneActorUndo undo;
    int no_undo, fail_register;
    char calls[24];
    int used;
} PlaceFixture;

static void mark(PlaceFixture *f, char call)
{
    CHECK(f->used + 1 < (int)sizeof(f->calls));
    f->calls[f->used++] = call;
    f->calls[f->used] = 0;
}
static void retain(void *p, void *actor)
{
    ++((PlaceActor *)actor)->refs;
    mark((PlaceFixture *)p, '+');
}
static void release(void *p, void *actor)
{
    CHECK(((PlaceActor *)actor)->refs > 0);
    --((PlaceActor *)actor)->refs;
    mark((PlaceFixture *)p, '-');
}
static void remove_roll(void *p, void *actor)
{
    CHECK(actor == &((PlaceFixture *)p)->actor);
    mark((PlaceFixture *)p, 'R');
}
static void add_roll(void *p, void *actor)
{
    CHECK(actor == &((PlaceFixture *)p)->actor);
    mark((PlaceFixture *)p, 'A');
}
static int visible(void *p, void *actor)
{
    CHECK(actor == &((PlaceFixture *)p)->actor);
    return 1;
}
static MMI32 actor_id(void *p, void *actor)
{
    (void)p;
    return ((PlaceActor *)actor)->id;
}
static void set_id(void *p, void *actor, MMI32 id)
{
    (void)p;
    ((PlaceActor *)actor)->id = id;
}
static void *new_undo(void *p, int text)
{
    PlaceFixture *f = (PlaceFixture *)p;
    CHECK(!text);
    mark(f, 'N');
    return f->no_undo ? NULL : &f->undo;
}
static MMSceneActorUndo *actor_state(void *p, void *undo)
{
    CHECK(undo == &((PlaceFixture *)p)->undo);
    return (MMSceneActorUndo *)undo;
}
static int commit(void *p, void *undo)
{
    PlaceFixture *f = (PlaceFixture *)p;
    CHECK(undo == &f->undo && f->undo.operation == MM_SCENE_ACTOR_REPLACE &&
          f->undo.actor == &f->snapshot && f->snapshot.refs == 2 && f->snapshot.id == f->actor.id);
    mark(f, 'U');
    return !f->fail_register;
}
static void release_undo(void *p, void *undo)
{
    PlaceFixture *f = (PlaceFixture *)p;
    CHECK(undo == &f->undo && f->snapshot.refs == 1);
    mark(f, 'L');
    release(p, f->undo.actor);
}
static void clear_undo(void *p) { mark((PlaceFixture *)p, 'C'); }
static void error(void *p) { mark((PlaceFixture *)p, 'E'); }
static void tools(void *p) { mark((PlaceFixture *)p, 'T'); }

static void test_placement_ownership(void)
{
    static const char *expected[] = {"RAN", "+RN+UT--L-", "+RNEC--", "+RN+UECT--L-", "+RA--"};
    MMActorPlacementState state;
    MMActorOwnerServices actors;
    MMSceneContentServices scene;
    MMActorPlacementEditServices edit;
    PlaceFixture f;
    int mode;
    memset(&actors, 0, sizeof(actors));
    memset(&scene, 0, sizeof(scene));
    memset(&edit, 0, sizeof(edit));
    actors.context = &f;
    actors.retain = retain;
    actors.release = release;
    scene.context = &f;
    scene.actor_id = actor_id;
    scene.set_actor_id = set_id;
    scene.new_undo = new_undo;
    scene.actor_state = actor_state;
    scene.commit_undo = commit;
    scene.release_undo = release_undo;
    scene.clear_undo = clear_undo;
    scene.not_undoable = error;
    edit.context = &f;
    edit.remove_roll = remove_roll;
    edit.add_roll = add_roll;
    edit.visible = visible;
    edit.enable_tools = tools;
    for (mode = 1; mode <= 4; ++mode)
    {
        memset(&f, 0, sizeof(f));
        memset(&state, 0, sizeof(state));
        f.actor.refs = f.snapshot.refs = 1;
        f.actor.id = 7;
        f.no_undo = mode == 2;
        f.fail_register = mode == 3;
        mm_actor_placement_begin(&state, &f.actor, mode, &actors, &edit);
        CHECK(state.actor == &f.actor && state.entire_scene == mode && f.actor.refs == 2);
        mm_actor_placement_snapshot(&state, &f.snapshot, &actors);
        if (mode == 4)
            mm_actor_placement_end(&state, &actors, &edit);
        else
            CHECK(mm_actor_placement_finish(&state, &actors, &scene, &edit));
        CHECK(!state.actor && !state.snapshot && f.actor.refs == 1 && !f.snapshot.refs);
        CHECK(!strcmp(f.calls, expected[mode]));
        mm_actor_placement_discard(&state, &actors);
        CHECK(!strcmp(f.calls, expected[mode]));
    }
}

static void test_mouse_projection(void)
{
    const MMActorMatrix axes = {{{65536L, 0, 0}, {0, 0, 65536L}, {0, -65536L, 0}, {0, 0, 0}}};
    MMActorMatrix matrix;
    MMActorPoint point = {65536L, 131072L, 196608L};
    mm_matrix_identity(&matrix);
    matrix.m[3][0] = 17 * 65536L;
    mm_actor_mouse_to_world(&point, &axes, &matrix, 0);
    CHECK(point.dxr == 1310 && point.dyr == 3930 && point.dzr == -2620);
    matrix.m[0][0] = 0;
    matrix.m[0][1] = -65536L;
    matrix.m[1][0] = 65536L;
    matrix.m[1][1] = 0;
    point.dxr = 65536L;
    point.dyr = 131072L;
    point.dzr = 196608L;
    mm_actor_mouse_to_world(&point, &axes, &matrix, 1);
    CHECK(point.dxr == 39321 && point.dyr == -13107 && point.dzr == -26214);
    point.dxr = 65536L;
    point.dyr = -65536L;
    point.dzr = 32768L;
    mm_actor_scale_drag_depth(&point, 65536L, 50 * 65536L);
    CHECK(point.dxr == 72089 && point.dyr == -72089 && point.dzr == 36044);
    point.dxr = 65536L;
    mm_actor_scale_drag_depth(&point, 0, 250 * 65536L);
    CHECK(point.dxr == 180222);
}
int main(void)
{
    Fixture f;
    MMActorPlacementServices services;
    MMActorPoint point;
    int axis;
    memset(&f, 0, sizeof(f));
    services.context = &f;
    services.camera = camera;
    mm_matrix_identity(&f.matrix);
    /* Placement must retain even a single authored nonzero coordinate without
     * touching the camera. In particular, authored Y is not put on the floor. */
    for (axis = 0; axis < 3; ++axis)
    {
        point.dxr = axis == 0 ? -1 : 0;
        point.dyr = axis == 1 ? -65536L : 0;
        point.dzr = axis == 2 ? 1 : 0;
        mm_actor_placement(&point, 1, &services);
        CHECK(f.calls == 0);
        CHECK(point.dxr == (axis == 0 ? -1 : 0));
        CHECK(point.dyr == (axis == 1 ? 589824L : 655360L));
        CHECK(point.dzr == (axis == 2 ? 1 : 0));
    }
    point.dxr = 1; point.dyr = 2147483647L; point.dzr = 0;
    mm_actor_placement(&point, 1, &services);
    CHECK(point.dyr == -2146828289L && f.calls == 0);
    /* Exactly 25 units uses the original distance, rather than the midpoint. */
    f.hither = 65536L; f.yon = 1638400L;
    point.dxr = point.dyr = point.dzr = 0;
    mm_actor_placement(&point, 0, &services);
    CHECK(f.calls == 1 && point.dxr == 0 && point.dyr == 0 && point.dzr == -1638400L);
    f.yon = 1638399L;
    point.dxr = point.dyr = point.dzr = 0;
    mm_actor_placement(&point, 1, &services);
    CHECK(f.calls == 2 && point.dyr == 655360L && point.dzr == -851967L);
    /* Division rounds the clipping midpoint toward zero at subpixel distances. */
    f.hither = 0; f.yon = 3;
    point.dxr = point.dyr = point.dzr = 0;
    mm_actor_placement(&point, 0, &services);
    CHECK(point.dzr == -1 && f.calls == 3);
    f.yon = 0;
    point.dxr = point.dyr = point.dzr = 0;
    mm_actor_placement(&point, 0, &services);
    CHECK(point.dzr == 0 && f.calls == 4);
    /* Camera translation and projection use X/Z only. Camera Y is ignored. */
    f.yon = 3276800L;
    f.matrix.m[2][0] = -65536L; f.matrix.m[2][2] = 0;
    f.matrix.m[3][0] = 65536L; f.matrix.m[3][1] = 999; f.matrix.m[3][2] = -65536L;
    point.dxr = point.dyr = point.dzr = 0;
    mm_actor_placement(&point, 1, &services);
    CHECK(point.dxr == 1703936L && point.dyr == 655360L && point.dzr == -65536L);
    /* Translation wraps at the BRender scalar width. */
    f.matrix.m[3][0] = 2147483647L;
    point.dxr = point.dyr = point.dzr = 0;
    mm_actor_placement(&point, 0, &services);
    CHECK(point.dxr == -2145845249L);
    test_placement_ownership();
    test_mouse_projection();
    puts("actor placement checks passed");
    return 0;
}
