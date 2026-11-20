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
