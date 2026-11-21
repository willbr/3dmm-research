#include "body_renderer.h"
#include "brender.h"
#include <string.h>

typedef char BodyMatrixSize[sizeof(br_matrix34) == sizeof(MMActorMatrix) ? 1 : -1];

struct BodyBindingAlignment
{
    char prefix;
    MMBodyPartBinding binding;
};

int mm_brender_body_layout(MMI32 count, size_t limit, size_t *bytes, size_t *bindings_offset)
{
    const size_t alignment = offsetof(struct BodyBindingAlignment, binding);
    const size_t element_bytes = sizeof(br_actor) + sizeof(MMBodyPartBinding);
    size_t actor_bytes, payload, padding;
    if (count < 0 || (size_t)count > limit / element_bytes)
        return 0;
    actor_bytes = (size_t)count * sizeof(br_actor);
    payload = (size_t)count * element_bytes;
    padding = (alignment - actor_bytes % alignment) % alignment;
    if (padding > limit - payload)
        return 0;
    *bytes = payload + padding;
    *bindings_offset = actor_bytes + padding;
    return 1;
}

static br_material *shared_highlight;

static void *highlight_material(void *context)
{
    (void)context;
    return shared_highlight;
}

static void *allocate_highlight(void *context)
{
    (void)context;
    return BrMaterialAllocate("Hilite");
}

static void install_highlight(void *context, void *p)
{
    br_material *material = (br_material *)p;
    (void)context;
    shared_highlight = material;
    material->colour = BR_COLOUR_RGB(255, 255, 255);
    material->ka = BR_UFRACTION(0.10);
    material->kd = BR_UFRACTION(0.60);
    material->ks = BR_UFRACTION(0.60);
    material->power = BR_SCALAR(50);
    material->flags = BR_MATF_LIGHT | BR_MATF_GOURAUD | BR_MATF_FORCE_Z_0;
    material->index_base = 0;
    material->index_range = 1;
    BrMaterialAdd(material);
}

void mm_brender_body_release_highlight(void)
{
    if (shared_highlight)
    {
        BrMaterialRemove(shared_highlight);
        BrMaterialFree(shared_highlight);
        shared_highlight = NULL;
    }
}

void mm_brender_body_highlight_colour(MMI32 colour)
{
    if (shared_highlight)
        shared_highlight->index_base = (br_uint_8)colour;
}

static void initialize_actor(void *context, void *body, void *actors, MMI32 index, int role)
{
    br_actor *actor = (br_actor *)actors + index;
    (void)context;
    actor->type = role == MM_BODY_PART ? BR_ACTOR_MODEL : BR_ACTOR_NONE;
    actor->t.type = BR_TRANSFORM_MATRIX34;
    actor->identifier = (char *)body;
    if (role == MM_BODY_HIGHLIGHT)
    {
        BrMatrix34Identity(&actor->t.t.mat);
        actor->material = shared_highlight;
        actor->render_style = BR_RSTYLE_BOUNDING_EDGES;
    }
    else if (role == MM_BODY_PART)
        actor->render_style = BR_RSTYLE_FACES;
}

void mm_brender_body_link(void *context, void *actors, MMI32 parent, MMI32 child)
{
    br_actor *body = (br_actor *)actors;
    (void)context;
    BrActorAdd(body + parent, body + child);
}

static void set_matrix(void *context, void *actors, MMI32 index, const MMActorMatrix *matrix)
{
    br_actor *actor = (br_actor *)actors + index;
    (void)context;
    memcpy(&actor->t.t.mat, matrix, sizeof(actor->t.t.mat));
}

static void set_model(void *context, void *actors, MMI32 index, void *model)
{
    (void)context;
    ((br_actor *)actors)[index].model = (br_model *)model;
}

static void set_material(void *context, void *actors, MMI32 index, void *material)
{
    (void)context;
    ((br_actor *)actors)[index].material = (br_material *)material;
}

static void apply(br_actor *actor, const MMBodyPartBinding *binding)
{
    actor->model = (br_model *)binding->model_handle;
    actor->material = (br_material *)binding->material;
    actor->t.type = BR_TRANSFORM_MATRIX34;
    set_matrix(NULL, actor, 0, &binding->matrix);
}

void mm_brender_body_create_services(MMBodyCreateServices *s)
{
    s->highlight = highlight_material;
    s->allocate_highlight = allocate_highlight;
    s->install_highlight = install_highlight;
    s->initialize_actor = initialize_actor;
    s->assign_matrix = set_matrix;
    s->link = mm_brender_body_link;
}

static void copy_actors(void *context, void *body, void *destination, const void *source, MMI32 count)
{
    br_actor *to = (br_actor *)destination;
    const br_actor *from = (const br_actor *)source;
    MMI32 i;
    (void)context;
    for (i = 0; i < count; ++i)
    {
        const br_actor *actor = from + i;
        br_actor *copy = to + i;
        *copy = *actor;
        copy->identifier = (char *)body;
        copy->parent = i && actor->parent ? to + (actor->parent - from) : NULL;
        copy->next = i && actor->next ? to + (actor->next - from) : NULL;
        copy->children = actor->children ? to + (actor->children - from) : NULL;
        copy->prev = NULL;
    }
    /* BRender backlinks point to the preceding link slot, not the preceding
     * actor. Rebuild them from the new child heads and sibling links. */
    for (i = 0; i < count; ++i)
    {
        br_actor *actor = to + i;
        if (actor->children)
            actor->children->prev = &actor->children;
        if (actor->next)
            actor->next->prev = &actor->next;
    }
}

static void identify_actors(void *context, void *body, void *actors, MMI32 count)
{
    br_actor *array = (br_actor *)actors;
    MMI32 i;
    (void)context;
    for (i = 0; i < count; ++i)
        array[i].identifier = (char *)body;
}

static int highlighted(void *context, void *actors)
{
    (void)context;
    return ((br_actor *)actors)[1].type == BR_ACTOR_MODEL;
}

void mm_brender_body_edit_services(MMBodyEditServices *s)
{
    s->copy_actors = copy_actors;
    s->identify = identify_actors;
    s->highlighted = highlighted;
}

static void highlight(void *context, void *actors, int enabled)
{
    (void)context;
    ((br_actor *)actors)[1].type = enabled ? BR_ACTOR_MODEL : BR_ACTOR_NONE;
}

static int attached(void *context, void *actors)
{
    (void)context;
    return ((br_actor *)actors)->parent != NULL;
}

static void detach(void *context, void *actors)
{
    (void)context;
    BrActorRemove((br_actor *)actors);
}

void mm_brender_body_runtime_services(MMBodyRuntimeServices *s)
{
    s->assign_model = set_model;
    s->assign_material = set_material;
    s->highlight = highlight;
    s->attached = attached;
    s->detach = detach;
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
