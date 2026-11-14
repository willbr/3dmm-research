#include "three_mm/body_geometry.h"
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
    ThreeMMBodyGeometry geometry;
    Actor actors[5];
    ThreeMMBodyBounds local, highlight_bounds;
    ThreeMMActorMatrix matrix;
    int queries, matrices, highlight_matrices, type_changes;
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
static ThreeMMBodyGeometry *state(void *c, void *b)
{
    (void)c;
    return &((Body *)b)->geometry;
}
static ThreeMMI32 count(void *c, void *b)
{
    (void)c;
    (void)b;
    return 3;
}
static void *part(void *c, void *b, ThreeMMI32 i)
{
    (void)c;
    return ((Body *)b)->actors + i + 2;
}
static ThreeMMI32 part_set(void *c, void *b, ThreeMMI32 i)
{
    (void)c;
    (void)b;
    return i % 2;
}
static void *highlight(void *c, void *b)
{
    (void)c;
    return ((Body *)b)->actors + 1;
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
static void local_bounds(void *c, void *b, ThreeMMBodyBounds *out)
{
    Body *body = (Body *)b;
    (void)c;
    ++body->queries;
    assert(body->actors[1].type == body->query_type);
    *out = body->local;
}
static void matrix(void *c, void *b, ThreeMMActorMatrix *out)
{
    Body *body = (Body *)b;
    (void)c;
    ++body->matrices;
    assert(body->actors[1].type == body->restored_type);
    *out = body->matrix;
}
static void highlight_matrix(void *c, void *b, const ThreeMMBodyBounds *in)
{
    Body *body = (Body *)b;
    (void)c;
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
    three_mm_body_geometry_init(&b->geometry);
    for (i = 0; i < 5; ++i)
        b->actors[i].owner = b;
    b->local.min[0] = -65536;
    b->local.min[1] = -131072;
    b->local.min[2] = -196608;
    b->local.max[0] = 262144;
    b->local.max[1] = 327680;
    b->local.max[2] = 393216;
    three_mm_matrix_identity(&b->matrix);
    b->query_type = 17;
    b->restored_type = 19;
    b->actors[1].type = 19;
}
static void geometry(ThreeMMBodyGeometryServices *s)
{
    Body b;
    ThreeMMBodyRect rect = {-11, -9, 30, 32}, empty = {31, 33, 30, 32}, out;
    ThreeMMBodyBounds bounds;
    ThreeMMI32 x, y, position[3];
    init(&b);
    b.geometry.last_visible = rect;
    assert(!three_mm_body_in_view(&b.geometry));
    three_mm_body_visible_rect(&b.geometry, &out);
    assert(!memcmp(&out, &rect, sizeof(rect)));
    three_mm_body_center(&b.geometry, &x, &y);
    assert(x == 9 && y == 11);
    three_mm_body_rendered(b.actors + 2, &empty, s);
    assert(!three_mm_body_in_view(&b.geometry));
    three_mm_body_rendered(b.actors + 3, &rect, s);
    assert(three_mm_body_in_view(&b.geometry));
    rect.left = -31;
    rect.right = 29;
    rect.top = -30;
    rect.bottom = 40;
    three_mm_body_rendered(b.actors + 4, &rect, s);
    assert(b.geometry.bounds.left == -31 && b.geometry.bounds.right == 30 && b.geometry.bounds.top == -30 &&
           b.geometry.bounds.bottom == 40);
    three_mm_body_prepare_render(b.actors, s);
    assert(!three_mm_body_in_view(&b.geometry) && b.geometry.last_visible.left == -31 &&
           b.geometry.last_visible.right == 30);
    assert(b.queries == 1 && b.highlight_matrices == 1 && b.type_changes == 2 &&
           !memcmp(&b.local, &b.highlight_bounds, sizeof(b.local)));
    b.geometry.bounds = empty;
    b.actors[1].type = 99;
    three_mm_body_prepare_render(b.actors, s);
    assert(!memcmp(&b.geometry.bounds, &empty, sizeof(empty)) && b.queries == 1 && b.geometry.last_visible.left == -31);
    b.actors[1].type = b.restored_type;
    b.matrix.m[3][0] = 12345;
    b.matrix.m[3][1] = -23456;
    b.matrix.m[3][2] = 34567;
    three_mm_body_bounds(&b, 0, &bounds, s);
    assert(!memcmp(&bounds, &b.local, sizeof(bounds)) && !b.matrices);
    three_mm_body_bounds(&b, 1, &bounds, s);
    assert(bounds.min[0] == -65536 + 12345 && bounds.max[1] == 327680 - 23456 && bounds.max[2] == 393216 + 34567);
    three_mm_body_position(&b, position, s);
    assert(position[0] == 12345 && position[1] == -23456 && position[2] == 34567);
    /* Empty current bounds use the last visible rectangle. Sum wraps before
     * signed division; negative odd sums truncate toward zero. */
    b.geometry.last_visible.left = 2147483647L;
    b.geometry.last_visible.right = 2147483647L;
    b.geometry.last_visible.top = -4;
    b.geometry.last_visible.bottom = 1;
    three_mm_body_center(&b.geometry, &x, &y);
    assert(x == -1 && y == -1);
    b.geometry.last_visible.left = -2147483647L;
    b.geometry.last_visible.right = -1;
    three_mm_body_center(&b.geometry, &x, &y);
    assert(x == -1073741824L);
    b.geometry.last_visible.left = 2147483647L;
    b.geometry.found = 1;
    b.geometry.set = 4;
    three_mm_body_clear_bounds(&b.geometry);
    assert(b.geometry.found == 1 && b.geometry.set == 4 && b.geometry.last_visible.left == 2147483647L);
}
static void picking(ThreeMMBodyGeometryServices *s)
{
    Body a, b;
    Actor orphan;
    ThreeMMBodyPick pick, other;
    ThreeMMI32 set = 71;
    ThreeMMBodyRect visible = {0, 0, 10, 10};
    init(&a);
    init(&b);
    memset(&orphan, 0, sizeof(orphan));
    three_mm_body_pick_begin(&pick);
    three_mm_body_pick_begin(&other);
    assert(!three_mm_body_pick_result(&pick, &set, s) && set == 71);
    a.geometry.bounds = b.geometry.bounds = visible;
    assert(!three_mm_body_pick_visit(&pick, a.actors + 2, 100, s));
    assert(!three_mm_body_pick_visit(&pick, b.actors + 3, 100, s));
    assert(pick.body == &a && three_mm_body_pick_result(&pick, &set, s) == &a && set == 0);
    three_mm_body_pick_visit(&other, b.actors + 3, 50, s);
    assert(other.body == &b && pick.body == &a);
    three_mm_body_pick_visit(&pick, b.actors + 3, 99, s);
    assert(pick.body == &b);
    three_mm_body_pick_visit(&pick, a.actors + 1, 98, s);
    assert(pick.body == &a);
    three_mm_body_pick_result(&pick, &set, s);
    assert(set == -1);
    three_mm_body_clear_bounds(&b.geometry);
    three_mm_body_pick_visit(&pick, b.actors + 2, -100, s);
    assert(pick.body == &a);
    three_mm_body_pick_visit(&pick, a.actors, (-2147483647L - 1), s);
    assert(pick.actor == a.actors && pick.depth == (-2147483647L - 1));
    three_mm_body_search_begin(&a.geometry);
    assert(!three_mm_body_search_visit(&a, b.actors + 2, s) && !a.geometry.found && a.geometry.set == -1);
    assert(!three_mm_body_search_visit(&a, a.actors + 1, s) && a.geometry.found && a.geometry.set == -1);
    assert(three_mm_body_search_visit(&a, a.actors + 3, s) && a.geometry.set == 1);
    assert(three_mm_body_search_visit(&a, a.actors, s) && a.geometry.set == -1);
    set = 71;
    assert(!three_mm_body_from_actor(&orphan, &set, s) && set == 71);
    assert(!three_mm_body_pick_visit(&pick, &orphan, 0, s));
    set = 71;
    three_mm_body_pick_begin(&pick);
    three_mm_body_pick_visit(&pick, a.actors, 2147483647L, s);
    assert(!three_mm_body_pick_result(&pick, &set, s) && set == 71);
}
static void transform(void)
{
    ThreeMMBodyBounds b = {{-65536, -131072, -196608}, {262144, 327680, 393216}}, out, alias;
    ThreeMMActorMatrix m;
    three_mm_matrix_identity(&m);
    m.m[0][0] = -131072;
    m.m[1][0] = 32768;
    m.m[3][0] = 65536;
    three_mm_body_transform_bounds(&b, &m, &out);
    assert(out.min[0] == -524288 && out.max[0] == 360448 && out.min[1] == b.min[1] && out.max[2] == b.max[2]);
    alias = b;
    three_mm_body_transform_bounds(&alias, &m, &alias);
    assert(!memcmp(&out, &alias, sizeof(out)));
    /* Preserve the retail +/-10000 sentinel even when every corner is beyond
     * it; changing to mathematical extrema would be a behavior change. */
    three_mm_matrix_identity(&m);
    m.m[3][0] = 786432000L;
    m.m[3][1] = -786432000L;
    three_mm_body_transform_bounds(&b, &m, &out);
    assert(out.min[0] == 655360000L && out.max[1] == -655360000L);
}
int main(void)
{
    ThreeMMBodyGeometryServices s;
    Fixture f;
    memset(&s, 0, sizeof(s));
    memset(&f, 0, sizeof(f));
    s.context = &f;
    s.owner = owner;
    s.state = state;
    s.part_count = count;
    s.part = part;
    s.part_set = part_set;
    s.highlight = highlight;
    s.actor_type = actor_type;
    s.set_actor_type = set_type;
    s.local_bounds = local_bounds;
    s.root_matrix = matrix;
    s.highlight_matrix = highlight_matrix;
    s.missing_owner = missing;
    s.none_type = 17;
    s.model_type = 19;
    geometry(&s);
    picking(&s);
    transform();
    assert(f.missing == 2);
    puts("C89 body geometry: render bounds, highlight ordering, fixed transforms and isolated hit contexts passed.");
    return 0;
}
