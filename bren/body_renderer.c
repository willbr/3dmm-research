#include "body_renderer.h"
#include "brender.h"
#include <string.h>

typedef char BodyMatrixSize[sizeof(br_matrix34) == sizeof(MMActorMatrix) ? 1 : -1];

void mm_brender_body_matrix(void *context, void *actors, MMI32 index, const MMActorMatrix *matrix)
{
    br_actor *actor = (br_actor *)actors + index;
    (void)context;
    memcpy(&actor->t.t.mat, matrix, sizeof(actor->t.t.mat));
}

void mm_brender_body_model(void *context, void *actors, MMI32 index, void *model)
{
    (void)context;
    ((br_actor *)actors)[index].model = (br_model *)model;
}

void mm_brender_body_material(void *context, void *actors, MMI32 index, void *material)
{
    (void)context;
    ((br_actor *)actors)[index].material = (br_material *)material;
}

static void apply(br_actor *actor, const MMBodyPartBinding *binding)
{
    actor->model = (br_model *)binding->model_handle;
    actor->material = (br_material *)binding->material;
    actor->t.type = BR_TRANSFORM_MATRIX34;
    mm_brender_body_matrix(NULL, actor, 0, &binding->matrix);
}

static int submit_part(void *actors, MMI32 index, MMI32 parent, const MMBodyPartBinding *binding)
{
    (void)parent;
    apply((br_actor *)actors + index, binding);
    return 1;
}

void mm_brender_body_submit(const MMBodyDrawView *view, void *actors)
{
    mm_body_draw(view, actors, submit_part);
}

void *mm_brender_body_attach(void *context, void *parent, MMI32 index, const MMBodyPartBinding *binding)
{
    br_actor *actor;
    (void)context;
    actor = BrActorAllocate(index ? BR_ACTOR_MODEL : BR_ACTOR_NONE, NULL);
    if (!actor)
        return NULL;
    BrActorAdd((br_actor *)parent, actor);
    if (index)
        actor->render_style = BR_RSTYLE_FACES;
    apply(actor, binding);
    return actor;
}

void *mm_brender_body_owner(const void *actor)
{
    return ((const br_actor *)actor)->identifier;
}

static int actor_type(void *context, void *actor)
{
    (void)context;
    return ((br_actor *)actor)->type;
}

static void set_actor_type(void *context, void *actor, int type)
{
    (void)context;
    ((br_actor *)actor)->type = (br_uint_8)type;
}

static void local_bounds(void *context, void *actor, MMBodyBounds *out)
{
    br_bounds bounds;
    int axis;
    (void)context;
    BrActorToBounds(&bounds, (br_actor *)actor);
    for (axis = 0; axis < 3; ++axis)
    {
        out->min[axis] = bounds.min.v[axis];
        out->max[axis] = bounds.max.v[axis];
    }
}

static void highlight_matrix(void *context, void *actor, const MMBodyBounds *source)
{
    br_bounds bounds;
    int axis;
    (void)context;
    for (axis = 0; axis < 3; ++axis)
    {
        bounds.min.v[axis] = source->min[axis];
        bounds.max.v[axis] = source->max[axis];
    }
    BrBoundsToMatrix34(&((br_actor *)actor)->t.t.mat, &bounds);
}

void mm_brender_body_geometry_services(MMBodyGeometryServices *s)
{
    s->actor_type = actor_type;
    s->set_actor_type = set_actor_type;
    s->local_bounds = local_bounds;
    s->highlight_matrix = highlight_matrix;
    s->none_type = BR_ACTOR_NONE;
    s->model_type = BR_ACTOR_MODEL;
}
