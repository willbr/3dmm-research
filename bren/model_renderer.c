#include "model_renderer.h"
#include "brender.h"
#include <string.h>

typedef char VertexWireSize[sizeof(br_vertex) == MM_MODEL_VERTEX_BYTES ? 1 : -1];

static void model_owner_slot(char slot[sizeof(void *) + 1])
{
    memset(slot, ' ', sizeof(void *));
    slot[sizeof(void *)] = 0;
}

static void *allocate_model(void *context, MMI32 vertices, MMI32 faces)
{
    char identifier[sizeof(void *) + 1];
    (void)context;
    model_owner_slot(identifier);
    return BrModelAllocate(identifier, vertices, faces);
}

static void identify(void *context, void *handle, void *owner)
{
    br_model *model = (br_model *)handle;
    (void)context;
    memcpy(model->identifier, &owner, sizeof(owner));
}

void *mm_brender_model_owner(const void *handle)
{
    const br_model *model = (const br_model *)handle;
    void *owner;
    memcpy(&owner, model->identifier, sizeof(owner));
    return owner;
}

static void copy_input(void *context, void *handle, const void *vertices, const void *faces,
                       MMI32 vertex_count, MMI32 face_count)
{
    br_model *model = (br_model *)handle;
    (void)context;
    if (vertex_count)
        memcpy(model->vertices, vertices, (size_t)vertex_count * sizeof(*model->vertices));
    if (face_count)
        memcpy(model->faces, faces, (size_t)face_count * sizeof(*model->faces));
}

static void add_model(void *context, void *handle) { (void)context; BrModelAdd((br_model *)handle); }
static void remove_model(void *context, void *handle) { (void)context; BrModelRemove((br_model *)handle); }
static void free_model(void *context, void *handle) { (void)context; BrModelFree((br_model *)handle); }

static int component(void *context, void *handle, int kind, MMI32 bytes)
{
    br_model *model = (br_model *)handle;
    (void)context;
    switch (kind)
    {
    case MM_MODEL_VERTICES:
        model->prepared_vertices = BrResAllocate(model, bytes, BR_MEMORY_PREPARED_VERTICES);
        return model->prepared_vertices != NULL;
    case MM_MODEL_FACES:
        model->prepared_faces = BrResAllocate(model, bytes, BR_MEMORY_PREPARED_FACES);
        return model->prepared_faces != NULL;
    case MM_MODEL_VERTEX_GROUP:
        model->vertex_groups = BrResAllocate(model, bytes, BR_MEMORY_GROUPS);
        return model->vertex_groups != NULL;
    case MM_MODEL_FACE_GROUP:
        model->face_groups = BrResAllocate(model, bytes, BR_MEMORY_GROUPS);
        return model->face_groups != NULL;
    default:
        return 0;
    }
}

static void *vertex_data(void *context, void *handle, int prepared)
{
    br_model *model = (br_model *)handle;
    (void)context;
    return prepared ? model->prepared_vertices : model->vertices;
}

static void set_face(void *context, void *handle, int prepared, MMI32 index, const MMModelFace *in)
{
    br_model *model = (br_model *)handle;
    br_face *face = (prepared ? model->prepared_faces : model->faces) + index;
    unsigned i;
    (void)context;
    for (i = 0; i < 3; ++i)
    {
        face->vertices[i] = (br_uint_16)in->vertices[i];
        face->edges[i] = (br_uint_16)in->edges[i];
        face->n.v[i] = (br_fraction)in->normal[i];
    }
    face->material = NULL;
    face->smoothing = (br_uint_16)in->smoothing;
    face->flags = (br_uint_8)in->flags;
    face->_pad0 = (br_uint_8)in->pad;
    face->d = in->distance;
}

static void get_face(void *context, void *handle, MMI32 index, MMModelFace *out)
{
    const br_model *model = (const br_model *)handle;
    const br_face *face = model->prepared_faces + index;
    unsigned i;
    (void)context;
    for (i = 0; i < 3; ++i)
    {
        out->vertices[i] = face->vertices[i];
        out->edges[i] = face->edges[i];
        out->normal[i] = face->n.v[i];
    }
    out->smoothing = face->smoothing;
    out->flags = face->flags;
    out->pad = face->_pad0;
    out->distance = face->d;
}

static void publish_prepared(void *context, void *handle, const MMModelDescription *description,
                             unsigned flags, MMI32 vertex_groups, MMI32 face_groups)
{
    br_model *model = (br_model *)handle;
    (void)context;
    model->flags = (br_uint_16)flags;
    model->nprepared_vertices = (br_uint_16)description->vertices;
    model->nprepared_faces = (br_uint_16)description->faces;
    model->nvertex_groups = (br_uint_16)vertex_groups;
    model->nface_groups = (br_uint_16)face_groups;
}

static void bind_group(void *context, void *handle, int face_group)
{
    br_model *model = (br_model *)handle;
    (void)context;
    if (face_group)
    {
        model->face_groups->material = NULL;
        model->face_groups->faces = model->prepared_faces;
        model->face_groups->nfaces = model->nprepared_faces;
    }
    else
    {
        model->vertex_groups->material = NULL;
        model->vertex_groups->vertices = model->prepared_vertices;
        model->vertex_groups->nvertices = model->nprepared_vertices;
    }
}

static void describe(void *context, void *handle, MMModelDescription *out)
{
    const br_model *model = (const br_model *)handle;
    unsigned i;
    (void)context;
    out->vertices = model->nprepared_vertices;
    out->faces = model->nprepared_faces;
    out->radius = model->radius;
    for (i = 0; i < 3; ++i)
    {
        out->bounds.min[i] = model->bounds.min.v[i];
        out->bounds.max[i] = model->bounds.max.v[i];
        out->pivot[i] = model->pivot.v[i];
    }
}

static void geometry(void *context, void *handle, const MMModelDescription *in)
{
    br_model *model = (br_model *)handle;
    unsigned i;
    (void)context;
    model->radius = in->radius;
    for (i = 0; i < 3; ++i)
    {
        model->bounds.min.v[i] = in->bounds.min[i];
        model->bounds.max.v[i] = in->bounds.max[i];
        model->pivot.v[i] = in->pivot[i];
    }
}

static void get_position(void *context, void *handle, MMI32 index, MMI32 out[3])
{
    const br_model *model = (const br_model *)handle;
    unsigned i;
    (void)context;
    for (i = 0; i < 3; ++i)
        out[i] = model->prepared_vertices[index].p.v[i];
}

static void set_position(void *context, void *handle, MMI32 index, const MMI32 in[3])
{
    br_model *model = (br_model *)handle;
    unsigned i;
    (void)context;
    for (i = 0; i < 3; ++i)
        model->prepared_vertices[index].p.v[i] = in[i];
}

static void *load_dat(void *context, const void *path)
{
    br_model *model = BrModelLoad((char *)path);
    char slot[sizeof(void *) + 1], *identifier;
    (void)context;
    if (!model)
        return NULL;
    if (!model->identifier || strlen(model->identifier) < sizeof(void *))
    {
        model_owner_slot(slot);
        identifier = BrResStrDup(model, slot);
        if (!identifier)
        {
            BrModelFree(model);
            return NULL;
        }
        if (model->identifier)
            BrResFree(model->identifier);
        model->identifier = identifier;
    }
    return model;
}

static void or_flags(void *context, void *handle, unsigned flags)
{ (void)context; ((br_model *)handle)->flags |= (br_uint_16)flags; }
static void prepare(void *context, void *handle, unsigned flags)
{ (void)context; BrModelPrepare((br_model *)handle, (br_uint_16)flags); }

void mm_brender_model_services(MMModelServices *s)
{
    s->face_bytes = sizeof(br_face);
    s->vertex_group_bytes = sizeof(br_vertex_group);
    s->face_group_bytes = sizeof(br_face_group);
    s->prepared_flag = BR_MODF_PREPREPARED;
    s->keep_original_flag = BR_MODF_KEEP_ORIGINAL;
    s->prepare_all = BR_MPREP_ALL;
    s->allocate_model = allocate_model;
    s->identify = identify;
    s->copy_input = copy_input;
    s->add = add_model;
    s->remove = remove_model;
    s->free_model = free_model;
    s->component = component;
    s->vertex_data = vertex_data;
    s->set_face = set_face;
    s->get_face = get_face;
    s->publish_prepared = publish_prepared;
    s->bind_group = bind_group;
    s->describe = describe;
    s->geometry = geometry;
    s->position = get_position;
    s->set_position = set_position;
    s->load_dat = load_dat;
    s->or_flags = or_flags;
    s->prepare = prepare;
}

typedef struct ModelLighting
{
    br_camera camera;
    br_light light;
} ModelLighting;

static void *lighting_create(void *context, int kind, const MMModelLightingSpec *spec)
{
    ModelLighting *lighting = (ModelLighting *)context;
    br_actor *actor;
    br_material *material;
    switch (kind)
    {
    case MM_MODEL_LIGHT_WORLD:
        return BrActorAllocate(BR_ACTOR_NONE, NULL);
    case MM_MODEL_LIGHT_CAMERA:
        lighting->camera.identifier = "bah";
        lighting->camera.type = BR_CAMERA_PERSPECTIVE;
        lighting->camera.field_of_view = (br_angle)spec->fov;
        lighting->camera.hither_z = spec->hither;
        lighting->camera.yon_z = spec->yon;
        lighting->camera.aspect = spec->aspect;
        lighting->camera.width = spec->camera_width;
        lighting->camera.height = spec->camera_height;
        return BrActorAllocate(BR_ACTOR_CAMERA, &lighting->camera);
    case MM_MODEL_LIGHT_ACTOR:
        lighting->light.colour = spec->colour;
        lighting->light.type = BR_LIGHT_DIRECT;
        lighting->light.attenuation_c = spec->attenuation;
        actor = BrActorAllocate(BR_ACTOR_LIGHT, NULL);
        if (actor)
            actor->type_data = &lighting->light;
        return actor;
    case MM_MODEL_LIGHT_COLOUR:
        return BrPixelmapAllocate(BR_PMT_INDEX_8, (br_uint_16)spec->width, (br_uint_16)spec->height, NULL, 0);
    case MM_MODEL_LIGHT_DEPTH:
        return BrPixelmapAllocate(BR_PMT_DEPTH_16, (br_uint_16)spec->width, (br_uint_16)spec->height, NULL, 0);
    case MM_MODEL_LIGHT_MATERIAL:
        material = BrMaterialAllocate("Prelighting material");
        if (material)
        {
            material->colour = spec->colour;
            material->ka = (br_ufraction)spec->ambient;
            material->kd = (br_ufraction)spec->diffuse;
            material->ks = (br_ufraction)spec->specular;
            material->power = spec->power;
            material->index_base = (br_uint_8)spec->index_base;
            material->index_range = (br_uint_8)spec->index_range;
            material->flags = (spec->material_lit ? BR_MATF_LIGHT : 0) | (spec->material_gouraud ? BR_MATF_GOURAUD : 0);
        }
        return material;
    default:
        return NULL;
    }
}

static void attach(void *context, void *world, void *actor)
{ (void)context; BrActorAdd((br_actor *)world, (br_actor *)actor); }
static void enable(void *context, void *actor) { (void)context; BrLightEnable((br_actor *)actor); }
static void disable(void *context, void *actor) { (void)context; BrLightDisable((br_actor *)actor); }
static void add_material(void *context, void *material) { (void)context; BrMaterialAdd((br_material *)material); }
static void remove_material(void *context, void *material) { (void)context; BrMaterialRemove((br_material *)material); }
static void lighting_begin(void *context, void *world, void *camera, void *colour, void *depth)
{
    (void)context;
    BrZbSceneRenderBegin((br_actor *)world, (br_actor *)camera, (br_pixelmap *)colour, (br_pixelmap *)depth);
}
static void light_model(void *context, void *model, void *material, void *world)
{ (void)context; BrSceneModelLight((br_model *)model, (br_material *)material, (br_actor *)world, NULL); }
static void lighting_end(void *context) { (void)context; BrZbSceneRenderEnd(); }
static void lighting_free(void *context, int kind, void *handle)
{
    (void)context;
    if (kind == MM_MODEL_LIGHT_WORLD)
        BrActorFree((br_actor *)handle);
    else if (kind == MM_MODEL_LIGHT_MATERIAL)
        BrMaterialFree((br_material *)handle);
    else
        BrPixelmapFree((br_pixelmap *)handle);
}

int mm_brender_model_prelight(const MMModelResource *model, MMI32 lights, void (*multiple_lights)(void *))
{
    ModelLighting lighting;
    MMModelLightingServices services = {
        NULL, lighting_create, attach, enable, disable, add_material, remove_material,
        lighting_begin, light_model, lighting_end, lighting_free, NULL
    };
    memset(&lighting, 0, sizeof(lighting));
    services.context = &lighting;
    services.multiple_lights = multiple_lights;
    return mm_model_prelight(model, lights, &services);
}
