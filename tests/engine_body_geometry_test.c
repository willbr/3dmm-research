#include "mm/body_create.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct Body Body;
typedef struct Actor
{
    Body *owner;
    int type;
} Actor;
struct Body
{
    MMBodyState state;
    Actor actors[5];
    MMBodyPartBinding bindings[5];
    MMBodyBounds local, highlight_bounds;
    int queries, highlight_matrices, type_changes;
    int query_type, restored_type;
};
typedef struct Fixture
{
    int missing;
} Fixture;
static void *owner(void *c, void *a)
{
    (void)c;
    return ((Actor *)a)->owner;
}
static MMBodyState *state(void *c, void *b)
{
    (void)c;
    return &((Body *)b)->state;
}
static void *actor_at(void *c, void *actors, MMI32 i)
{
    (void)c;
    return (Actor *)actors + i;
}
static int actor_type(void *c, void *a)
{
    (void)c;
    return ((Actor *)a)->type;
}
static void set_type(void *c, void *a, int type)
{
    Actor *actor = (Actor *)a;
    (void)c;
    ++actor->owner->type_changes;
    actor->type = type;
}
static void local_bounds(void *c, void *root, MMBodyBounds *out)
{
    Body *body = ((Actor *)root)->owner;
    (void)c;
    assert(root == body->actors);
    ++body->queries;
    assert(body->actors[1].type == body->query_type);
    *out = body->local;
}
static void highlight_matrix(void *c, void *highlight, const MMBodyBounds *in)
{
    Body *body = ((Actor *)highlight)->owner;
    (void)c;
    assert(highlight == body->actors + 1);
    ++body->highlight_matrices;
    assert(body->actors[1].type == body->restored_type);
    body->highlight_bounds = *in;
}
static void missing(void *c)
{
    ++((Fixture *)c)->missing;
}
static void init(Body *b)
{
    int i;
    memset(b, 0, sizeof(*b));
    mm_body_state_init(&b->state);
    b->state.shape.actors = b->actors;
    b->state.shape.bindings = b->bindings;
    b->state.shape.part_count = 3;
    for (i = 0; i < 5; ++i)
    {
        b->actors[i].owner = b;
        b->bindings[i].set = i < 2 ? -1 : i % 2;
    }
    b->local.min[0] = -65536;
    b->local.min[1] = -131072;
    b->local.min[2] = -196608;
    b->local.max[0] = 262144;
    b->local.max[1] = 327680;
    b->local.max[2] = 393216;
    mm_matrix_identity(&b->bindings[0].matrix);
    b->query_type = 17;
    b->restored_type = 19;
    b->actors[1].type = 19;
}
static void geometry(MMBodyGeometryServices *s)
{
    Body b;
    MMBodyRect rect = {-11, -9, 30, 32}, empty = {31, 33, 30, 32}, out;
    MMBodyBounds bounds;
    MMBodyDrawView draw = {NULL, -1};
    MMI32 x, y, position[3];
    init(&b);
    assert(!mm_body_read_render(b.actors + 1, &draw, s) && !draw.bindings && draw.part_count == -1);
    assert(!mm_body_read_render(b.actors + 2, &draw, s) && !draw.bindings && draw.part_count == -1);
    assert(mm_body_read_render(b.actors, &draw, s) == &b && draw.bindings == b.bindings && draw.part_count == 3);
    b.state.geometry.last_visible = rect;
    assert(!mm_body_in_view(&b.state.geometry));
    mm_body_visible_rect(&b.state.geometry, &out);
    assert(!memcmp(&out, &rect, sizeof(rect)));
    mm_body_center(&b.state.geometry, &x, &y);
    assert(x == 9 && y == 11);
    mm_body_rendered(b.actors + 2, &empty, s);
    assert(!mm_body_in_view(&b.state.geometry));
    mm_body_rendered(b.actors + 3, &rect, s);
    assert(mm_body_in_view(&b.state.geometry));
    rect.left = -31;
    rect.right = 29;
    rect.top = -30;
    rect.bottom = 40;
    mm_body_rendered(b.actors + 4, &rect, s);
    assert(b.state.geometry.bounds.left == -31 && b.state.geometry.bounds.right == 30 && b.state.geometry.bounds.top == -30 &&
           b.state.geometry.bounds.bottom == 40);
    mm_body_prepare_render(b.actors, s);
    assert(!mm_body_in_view(&b.state.geometry) && b.state.geometry.last_visible.left == -31 &&
           b.state.geometry.last_visible.right == 30);
    assert(b.queries == 1 && b.highlight_matrices == 1 && b.type_changes == 2 &&
           !memcmp(&b.local, &b.highlight_bounds, sizeof(b.local)));
    b.state.geometry.bounds = empty;
    assert(mm_body_render_rect(b.actors + 2, &out, s) && !memcmp(&out, &empty, sizeof(out)));
    b.actors[1].type = 99;
    mm_body_prepare_render(b.actors, s);
    assert(!memcmp(&b.state.geometry.bounds, &empty, sizeof(empty)) && b.queries == 1 && b.state.geometry.last_visible.left == -31);
    b.actors[1].type = b.restored_type;
    b.bindings[0].matrix.m[3][0] = 12345;
    b.bindings[0].matrix.m[3][1] = -23456;
    b.bindings[0].matrix.m[3][2] = 34567;
    mm_body_bounds(&b.state.shape, 0, &bounds, s);
    assert(!memcmp(&bounds, &b.local, sizeof(bounds)) && b.actors[1].type == b.restored_type);
    mm_body_bounds(&b.state.shape, 1, &bounds, s);
    assert(bounds.min[0] == -65536 + 12345 && bounds.max[1] == 327680 - 23456 && bounds.max[2] == 393216 + 34567);
    assert(b.actors[1].type == b.restored_type);
    mm_body_position(&b.state.shape, position);
    assert(position[0] == 12345 && position[1] == -23456 && position[2] == 34567);
    /* Empty current bounds use the last visible rectangle. Sum wraps before
     * signed division; negative odd sums truncate toward zero. */
    b.state.geometry.last_visible.left = 2147483647L;
    b.state.geometry.last_visible.right = 2147483647L;
    b.state.geometry.last_visible.top = -4;
    b.state.geometry.last_visible.bottom = 1;
    mm_body_center(&b.state.geometry, &x, &y);
    assert(x == -1 && y == -1);
    b.state.geometry.last_visible.left = -2147483647L;
    b.state.geometry.last_visible.right = -1;
    mm_body_center(&b.state.geometry, &x, &y);
    assert(x == -1073741824L);
    b.state.geometry.last_visible.left = 2147483647L;
    b.state.geometry.found = 1;
    b.state.geometry.set = 4;
    mm_body_clear_bounds(&b.state.geometry);
    assert(b.state.geometry.found == 1 && b.state.geometry.set == 4 && b.state.geometry.last_visible.left == 2147483647L);
}
static void picking(MMBodyGeometryServices *s)
{
    Body a, b;
    Actor orphan;
    MMBodyPick pick, other;
    MMI32 set = 71;
    MMBodyRect visible = {0, 0, 10, 10}, out;
    init(&a);
    init(&b);
    memset(&orphan, 0, sizeof(orphan));
    mm_body_pick_begin(&pick);
    mm_body_pick_begin(&other);
    assert(!mm_body_pick_result(&pick, &set, s) && set == 71);
    a.state.geometry.bounds = b.state.geometry.bounds = visible;
    assert(!mm_body_pick_visit(&pick, a.actors + 2, 100, s));
    assert(!mm_body_pick_visit(&pick, b.actors + 3, 100, s));
    assert(pick.body == &a && mm_body_pick_result(&pick, &set, s) == &a && set == 0);
    mm_body_pick_visit(&other, b.actors + 3, 50, s);
    assert(other.body == &b && pick.body == &a);
    mm_body_pick_visit(&pick, b.actors + 3, 99, s);
    assert(pick.body == &b);
    mm_body_pick_visit(&pick, a.actors + 1, 98, s);
    assert(pick.body == &a);
    mm_body_pick_result(&pick, &set, s);
    assert(set == -1);
    mm_body_clear_bounds(&b.state.geometry);
    mm_body_pick_visit(&pick, b.actors + 2, -100, s);
    assert(pick.body == &a);
    mm_body_pick_visit(&pick, a.actors, (-2147483647L - 1), s);
    assert(pick.actor == a.actors && pick.depth == (-2147483647L - 1));
    mm_body_search_begin(&a.state.geometry);
    assert(!mm_body_search_visit(&a, b.actors + 2, s) && !a.state.geometry.found && a.state.geometry.set == -1);
    assert(!mm_body_search_visit(&a, a.actors + 1, s) && a.state.geometry.found && a.state.geometry.set == -1);
    assert(mm_body_search_visit(&a, a.actors + 3, s) && a.state.geometry.set == 1);
    assert(mm_body_search_visit(&a, a.actors, s) && a.state.geometry.set == -1);
    set = 71;
    assert(!mm_body_from_actor(&orphan, &set, s) && set == 71);
    out = visible;
    assert(!mm_body_render_rect(&orphan, &out, s) && !memcmp(&out, &visible, sizeof(out)));
    assert(!mm_body_pick_visit(&pick, &orphan, 0, s));
    set = 71;
    mm_body_pick_begin(&pick);
    mm_body_pick_visit(&pick, a.actors, 2147483647L, s);
    assert(!mm_body_pick_result(&pick, &set, s) && set == 71);
}

typedef struct HitSequence
{
    Actor *actor[4];
    MMI32 depth[4];
    int count, visited;
} HitSequence;

static void scan_hits(void *context, MMBodyHitVisitor *visit, void *visitor_context)
{
    HitSequence *hits = (HitSequence *)context;
    int i;
    hits->visited = 0;
    for (i = 0; i < hits->count; ++i)
    {
        ++hits->visited;
        if (visit(visitor_context, hits->actor[i], hits->depth[i]))
            break;
    }
}

static void scanning(const MMBodyGeometryServices *s)
{
    Body target, other;
    HitSequence hits = {{NULL}, {9, 8, 7, 6}, 0, 0};
    MMBodyRect visible = {0, 0, 10, 10};
    MMI32 set = 71;
    init(&target);
    init(&other);
    target.state.geometry.bounds = other.state.geometry.bounds = visible;
    assert(!mm_body_pick_at(scan_hits, &hits, &set, s) && set == 71);
    assert(!mm_body_search_at(&target, scan_hits, &hits, &set, s) && set == -1);
    hits.actor[0] = other.actors + 2;
    hits.actor[1] = target.actors + 1;
    hits.actor[2] = target.actors + 3;
    hits.actor[3] = target.actors + 4;
    hits.count = 2;
    assert(mm_body_search_at(&target, scan_hits, &hits, &set, s) && set == -1 && hits.visited == 2);
    hits.count = 4;
    assert(mm_body_search_at(&target, scan_hits, &hits, &set, s) && set == 1 && hits.visited == 3);
    assert(mm_body_pick_at(scan_hits, &hits, &set, s) == &target && set == 0 && hits.visited == 4);
}
static void transform(void)
{
    MMBodyBounds b = {{-65536, -131072, -196608}, {262144, 327680, 393216}}, out, alias;
    MMActorMatrix m;
    mm_matrix_identity(&m);
    m.m[0][0] = -131072;
    m.m[1][0] = 32768;
    m.m[3][0] = 65536;
    mm_body_transform_bounds(&b, &m, &out);
    assert(out.min[0] == -524288 && out.max[0] == 360448 && out.min[1] == b.min[1] && out.max[2] == b.max[2]);
    alias = b;
    mm_body_transform_bounds(&alias, &m, &alias);
    assert(!memcmp(&out, &alias, sizeof(out)));
    /* Preserve the retail +/-10000 sentinel even when every corner is beyond
     * it; changing to mathematical extrema would be a behavior change. */
    mm_matrix_identity(&m);
    m.m[3][0] = 786432000L;
    m.m[3][1] = -786432000L;
    mm_body_transform_bounds(&b, &m, &out);
    assert(out.min[0] == 655360000L && out.max[1] == -655360000L);
}
static void model_bounds(const void *model, MMBodyBounds *out)
{
    *out = *(const MMBodyBounds *)model;
}

static void set_bounds(void)
{
    MMBodyShape shape = {0};
    MMBodyPartBinding parts[6] = {0};
    MMBodyBounds model = {{0, 0, 0}, {65536L, 131072L, 196608L}}, out, previous;
    int i;
    shape.bindings = parts;
    shape.part_count = 4;
    shape.set_count = 3;
    for (i = 0; i < 6; ++i)
        mm_matrix_identity(&parts[i].matrix);
    parts[0].matrix.m[3][0] = 786432000L;
    parts[2].parent = -1;
    parts[2].matrix.m[3][0] = 65536L;
    parts[3].parent = 0;
    parts[3].set = 1;
    parts[3].matrix.m[3][1] = 655360L;
    parts[4].parent = 1;
    parts[4].matrix.m[3][0] = 131072L;
    parts[4].matrix.m[3][1] = 131072L;
    parts[2].model_handle = parts[3].model_handle = parts[4].model_handle = &model;
    parts[5].set = 2;
    assert(mm_body_set_bounds(&shape, 0, model_bounds, &out));
    assert(out.min[0] == 786497536L && out.max[0] == 786694144L);
    assert(out.min[1] == 0 && out.max[1] == 917504L);
    assert(out.min[2] == 0 && out.max[2] == 196608L);
    previous = out;
    assert(!mm_body_set_bounds(&shape, 2, model_bounds, &out));
    assert(!mm_body_set_bounds(&shape, 3, model_bounds, &out));
    assert(!memcmp(&previous, &out, sizeof(out)));
}
int main(void)
{
    MMBodyGeometryServices s;
    Fixture f;
    memset(&s, 0, sizeof(s));
    memset(&f, 0, sizeof(f));
    s.context = &f;
    s.owner = owner;
    s.state = state;
    s.actor_at = actor_at;
    s.actor_type = actor_type;
    s.set_actor_type = set_type;
    s.local_bounds = local_bounds;
    s.highlight_matrix = highlight_matrix;
    s.missing_owner = missing;
    s.none_type = 17;
    s.model_type = 19;
    geometry(&s);
    picking(&s);
    scanning(&s);
    transform();
    set_bounds();
    assert(f.missing == 3);
    puts("C89 body geometry: render bounds, highlight ordering, fixed transforms and isolated hit contexts passed.");
    return 0;
}
