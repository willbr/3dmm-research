#include "scene_renderer.h"
#include "mm/background_preview.h"
#include "body_renderer.h"
#include "model_renderer.h"
#include "brender.h"
#include <stdlib.h>
#include <string.h>

typedef char SceneMatrixSize[sizeof(br_matrix34) == sizeof(MMActorMatrix) &&
                                 sizeof(br_matrix34) == sizeof(((MMRenderCamera *)0)->matrix) ? 1 : -1];
typedef char ProjectionMatrixSize[sizeof(br_matrix4) == 16 * sizeof(MMI32) ? 1 : -1];
typedef char ScenePointSize[sizeof(br_vector3) == sizeof(MMActorPoint) ? 1 : -1];
typedef char SceneLightTypes[(int)BR_LIGHT_POINT == (int)MM_BACKGROUND_LIGHT_POINT &&
                            (int)BR_LIGHT_DIRECT == (int)MM_BACKGROUND_LIGHT_DIRECT ? 1 : -1];

void mm_brender_world_init(br_actor *world, br_actor *camera, br_camera *data,
                           void *owner, MMI32 width, MMI32 height)
{
    world->type = BR_ACTOR_NONE;
    world->t.type = BR_TRANSFORM_MATRIX34;
    BrMatrix34Identity(&world->t.t.mat);
    world->identifier = (char *)owner;
    camera->type = BR_ACTOR_CAMERA;
    camera->t.type = BR_TRANSFORM_MATRIX34;
    camera->type_data = data;
    data->type = BR_CAMERA_PERSPECTIVE;
    data->aspect = BR_SCALAR((double)width / (double)height);
    BrActorAdd(world, camera);
}

static void camera_pose(br_actor *actor, const void *matrix, MMI32 near_z, MMI32 far_z, unsigned short fov)
{
    br_camera *camera = (br_camera *)actor->type_data;
    actor->t.type = BR_TRANSFORM_MATRIX34;
    memcpy(&actor->t.t.mat, matrix, sizeof(actor->t.t.mat));
    camera->field_of_view = fov;
    camera->hither_z = near_z;
    camera->yon_z = far_z;
}

void mm_brender_world_camera_pose(br_actor *actor, const MMWorldCamera *camera)
{
    camera_pose(actor, &camera->matrix, camera->near_z, camera->far_z, camera->fov);
}

void mm_brender_world_camera_view(const br_actor *actor, const MMWorldCamera *state, MMRenderCamera *out)
{
    const br_camera *camera = (const br_camera *)actor->type_data;
    memset(out, 0, sizeof(*out));
    memcpy(out->matrix, &state->matrix, sizeof(out->matrix));
    out->near_z = state->near_z;
    out->far_z = state->far_z;
    out->fov = state->fov;
    out->aspect = camera->aspect;
}

static void update_camera(void *context, void *p, const MMRenderCamera *source)
{
    br_actor *actor = (br_actor *)p;
    br_camera *camera = (br_camera *)actor->type_data;
    (void)context;
    camera_pose(actor, source->matrix, source->near_z, source->far_z, source->fov);
    camera->type = source->parallel ? BR_CAMERA_PARALLEL : BR_CAMERA_PERSPECTIVE_FOV;
    camera->aspect = source->aspect;
    camera->width = source->width;
    camera->height = source->height;
}

br_actor *mm_brender_preview_camera(void *world, br_camera *data, const MMRenderCamera *source)
{
    br_actor *actor = BrActorAllocate(BR_ACTOR_CAMERA, data);
    if (actor)
    {
        BrActorAdd((br_actor *)world, actor);
        update_camera(NULL, actor, source);
    }
    return actor;
}

void mm_brender_pixelmap_bind(br_pixelmap *pixelmap, const MMWorldPixelView *view)
{
    pixelmap->type = (br_uint_8)view->type;
    pixelmap->row_bytes = (br_int_16)view->row;
    pixelmap->width = (br_uint_16)view->width;
    pixelmap->height = (br_uint_16)view->height;
    pixelmap->origin_x = (br_int_16)view->x;
    pixelmap->origin_y = (br_int_16)view->y;
    pixelmap->pixels = view->pixels;
}

static void draw(const MMRenderFrame *frame, br_pixelmap *colour, br_pixelmap *depth)
{
    BrZbSceneRender((br_actor *)frame->world, (br_actor *)frame->camera, colour, depth);
}

static void render(void *context, const MMRenderFrame *frame)
{
    update_camera(context, frame->camera, &frame->scene->camera);
    draw(frame, (br_pixelmap *)frame->colour, (br_pixelmap *)frame->depth);
}

void mm_brender_world_raster(void *context, const MMRenderFrame *frame, const MMWorldPixelView *rgb,
                             const MMWorldPixelView *z)
{
    br_pixelmap colour, depth;
    (void)context;
    memset(&colour, 0, sizeof(colour));
    memset(&depth, 0, sizeof(depth));
    mm_brender_pixelmap_bind(&colour, rgb);
    mm_brender_pixelmap_bind(&depth, z);
    draw(frame, &colour, &depth);
}

static void light_init(br_actor *actor, br_light *light, const MMBackgroundLight *source)
{
    memset(light, 0, sizeof(*light));
    light->type = (br_uint_8)source->type;
    light->colour = BR_COLOUR_RGB(255, 255, 255);
    light->attenuation_c = source->intensity;
    actor->type = BR_ACTOR_LIGHT;
    actor->type_data = light;
    actor->t.type = BR_TRANSFORM_MATRIX34;
    memcpy(&actor->t.t.mat, &source->matrix, sizeof(actor->t.t.mat));
}

int mm_brender_background_light(void *context, MMBackgroundResource *background, MMI32 i,
                                const MMBackgroundLight *source)
{
    (void)context;
    light_init((br_actor *)background->actors + i, (br_light *)background->lights + i, source);
    return 1;
}

static void enable_light(void *context, void *actor)
{
    (void)context;
    BrLightEnable((br_actor *)actor);
}

static void disable_light(void *context, void *actor)
{
    (void)context;
    BrLightDisable((br_actor *)actor);
}

void mm_brender_background_light_enable(void *context, MMBackgroundResource *background, MMI32 i)
{
    enable_light(context, (br_actor *)background->actors + i);
}

void mm_brender_background_light_disable(void *context, MMBackgroundResource *background, MMI32 i)
{
    disable_light(context, (br_actor *)background->actors + i);
}

void mm_brender_background_light_remove(void *context, MMBackgroundResource *background, MMI32 i)
{
    (void)context;
    BrActorRemove((br_actor *)background->actors + i);
}

static void *attach_light(void *context, void *world, void *data, const MMBackgroundLight *source)
{
    br_actor *actor = BrActorAllocate(BR_ACTOR_LIGHT, data);
    (void)context;
    if (actor)
    {
        BrActorAdd((br_actor *)world, actor);
        light_init(actor, (br_light *)data, source);
    }
    return actor;
}

static void *actor_parent(void *actor)
{
    return ((br_actor *)actor)->parent;
}

static void *actor_owner(void *actor)
{
    return ((br_actor *)actor)->identifier;
}

void *mm_brender_world_owner(void *actor)
{
    return mm_world_actor_owner(actor, actor_parent, actor_owner);
}

static void *first_child(void *context, const void *actor)
{
    (void)context;
    return ((const br_actor *)actor)->children;
}

static void *next_sibling(void *context, const void *actor)
{
    (void)context;
    return ((const br_actor *)actor)->next;
}

static void *world_child(void *context, void *actor)
{
    return first_child(context, actor);
}

static void *world_next(void *context, void *actor)
{
    return next_sibling(context, actor);
}

static int actor_type(void *context, void *actor)
{
    (void)context;
    return ((br_actor *)actor)->type;
}

static int actor_style(void *context, void *actor)
{
    (void)context;
    return ((br_actor *)actor)->render_style;
}

static void actor_set_style(void *context, void *actor, int style)
{
    (void)context;
    ((br_actor *)actor)->render_style = (br_uint_8)style;
}

static void *actor_model(void *context, const void *p)
{
    const br_actor *actor = (const br_actor *)p;
    (void)context;
    return actor->type == BR_ACTOR_MODEL ? actor->model : NULL;
}

static void *clone_actor(void *context, void *parent, const void *p)
{
    const br_actor *source = (const br_actor *)p;
    br_actor *actor = BrActorAllocate(source->type, NULL);
    (void)context;
    if (actor)
    {
        BrActorAdd((br_actor *)parent, actor);
        actor->model = source->model;
        actor->material = source->material;
        actor->render_style = source->render_style;
        actor->t = source->t;
    }
    return actor;
}

static void apply_material(br_material *material, br_pixelmap *map, MMU32 flags,
                           unsigned char base, unsigned char range)
{
    material->flags = flags;
    material->colour_map = map;
    material->index_base = base;
    material->index_range = range;
    BrMaterialUpdate(material, BR_MATU_ALL);
}

struct MaterialEnumeration
{
    int (*visit)(void *, void *);
    void *argument;
};

static br_uint_32 BR_CALLBACK visit_material(br_material *material, void *context)
{
    struct MaterialEnumeration *e = (struct MaterialEnumeration *)context;
    return e->visit(e->argument, material);
}

static size_t material_count(void *context)
{
    (void)context;
    return BrMaterialCount(NULL);
}

static void enumerate_materials(void *context, int (*visit)(void *, void *), void *argument)
{
    struct MaterialEnumeration e;
    (void)context;
    e.visit = visit;
    e.argument = argument;
    BrMaterialEnum(NULL, visit_material, &e);
}

static void read_world_material(void *context, void *p, MMWorldMaterial *out)
{
    br_material *material = (br_material *)p;
    (void)context;
    out->object = material;
    out->flags = material->flags;
    out->texture = material->colour_map;
    out->base = material->index_base;
    out->range = material->index_range;
}

static void write_world_material(void *context, const MMWorldMaterial *source)
{
    (void)context;
    apply_material((br_material *)source->object, (br_pixelmap *)source->texture, source->flags,
                   source->base, source->range);
}

static void *allocate_materials(void *context, size_t bytes)
{
    (void)context;
    return malloc(bytes);
}

static void free_materials(void *context, void *p)
{
    (void)context;
    free(p);
}

void mm_brender_world_services(MMWorldRenderServices *s)
{
    s->actor_none = BR_ACTOR_NONE;
    s->actor_model = BR_ACTOR_MODEL;
    s->bounding_points = BR_RSTYLE_BOUNDING_POINTS;
    s->bounding_edges = BR_RSTYLE_BOUNDING_EDGES;
    s->bounding_faces = BR_RSTYLE_BOUNDING_FACES;
    s->faces = BR_RSTYLE_FACES;
    s->edges = BR_RSTYLE_EDGES;
    s->child = world_child;
    s->next = world_next;
    s->type = actor_type;
    s->style = actor_style;
    s->set_style = actor_set_style;
    s->render = render;
    s->materials.gouraud = BR_MATF_GOURAUD;
    s->materials.count = material_count;
    s->materials.enumerate = enumerate_materials;
    s->materials.read = read_world_material;
    s->materials.write = write_world_material;
    s->materials.allocate = allocate_materials;
    s->materials.free = free_materials;
}

void mm_brender_preview_tree(MMPreviewTreeServices *s)
{
    s->first = first_child;
    s->next = next_sibling;
    s->model = actor_model;
    s->clone_attach = clone_actor;
}

static void *new_colour(void *context, MMU32 width, MMU32 height)
{
    (void)context;
    return BrPixelmapAllocate(BR_PMT_INDEX_8, (br_uint_16)width, (br_uint_16)height, NULL, BR_PMAF_NORMAL);
}

static void *new_depth(void *context, void *colour)
{
    (void)context;
    return BrPixelmapMatch((br_pixelmap *)colour, BR_PMMATCH_DEPTH_16);
}

static void *new_world(void *context)
{
    (void)context;
    return BrActorAllocate(BR_ACTOR_NONE, NULL);
}

static void free_world(void *context, void *world)
{
    (void)context;
    BrActorFree((br_actor *)world);
}

static void free_pixelmap(void *context, void *map)
{
    (void)context;
    BrPixelmapFree((br_pixelmap *)map);
}

static unsigned char *background_map_row(void *context, void *object, MMU32 y)
{
    br_pixelmap *map = object;
    (void)context;
    return (unsigned char *)map->pixels + y * map->row_bytes;
}

static void background_map_add(void *context, void *map)
{
    (void)context;
    BrMapAdd(map);
}

static void background_map_remove(void *context, void *map)
{
    (void)context;
    BrMapRemove(map);
}

static void *background_material_new(void *context)
{
    (void)context;
    return BrMaterialAllocate(NULL);
}

static void background_material_configure(void *context, void *object, void *map)
{
    br_material *material = object;
    (void)context;
    material->flags = BR_MATF_TWO_SIDED | BR_MATF_PERSPECTIVE | BR_MATF_MAP_OPAQUE;
    material->colour_map = map;
}

static void background_material_add(void *context, void *material)
{
    (void)context;
    BrMaterialAdd(material);
}

static void background_material_remove(void *context, void *material)
{
    (void)context;
    BrMaterialRemove(material);
}

static void background_material_free(void *context, void *material)
{
    (void)context;
    BrMaterialFree(material);
}

static void *background_model_new(void *context, MMU32 vertices, MMU32 faces)
{
    (void)context;
    return BrModelAllocate(NULL, (br_uint_16)vertices, (br_uint_16)faces);
}

static void background_model_configure(void *context, void *model)
{
    (void)context;
    ((br_model *)model)->flags = BR_MODF_QUICK_UPDATE;
}

static void background_vertex(void *context, void *object, MMU32 index, const MMBackgroundVertex *value)
{
    br_vertex *vertex = &((br_model *)object)->vertices[index];
    (void)context;
    vertex->p.v[0] = BrFloatToScalar(value->x);
    vertex->p.v[1] = BrFloatToScalar(value->y);
    vertex->p.v[2] = BrFloatToScalar(value->z);
    vertex->map.v[0] = BrFloatToScalar(value->u);
    vertex->map.v[1] = BrFloatToScalar(value->v);
}

static void background_face(void *context, void *object, MMU32 index, const MMU32 indices[3], void *material)
{
    br_face *face = &((br_model *)object)->faces[index];
    unsigned i;
    (void)context;
    face->material = material;
    for (i = 0; i < 3; ++i)
        face->vertices[i] = (br_uint_16)indices[i];
}

static void background_model_add(void *context, void *model)
{
    (void)context;
    BrModelAdd(model);
}

static void background_model_remove(void *context, void *model)
{
    (void)context;
    BrModelRemove(model);
}

static void background_model_free(void *context, void *model)
{
    (void)context;
    BrModelFree(model);
}

static void *background_actor_new(void *context)
{
    (void)context;
    return BrActorAllocate(BR_ACTOR_MODEL, NULL);
}

static void background_actor_configure(void *context, void *object, void *model, void *material)
{
    br_actor *actor = object;
    (void)context;
    actor->model = model;
    actor->material = material;
    actor->t.type = BR_TRANSFORM_IDENTITY;
}

static void background_actor_add(void *context, void *world, void *actor)
{
    (void)context;
    BrActorAdd(world, actor);
}

const MMBackgroundMeshServices *mm_brender_background_mesh_services(void)
{
    static const MMBackgroundMeshServices services = {
        NULL, new_colour, background_map_row, background_map_add, background_map_remove, free_pixelmap,
        background_material_new, background_material_configure, background_material_add, background_material_remove,
        background_material_free, background_model_new, background_model_configure, background_vertex, background_face,
        background_model_add, background_model_remove, background_model_free, background_actor_new,
        background_actor_configure, background_actor_add
    };
    return &services;
}

static float background_scalar_float(void *context, MMI32 value)
{
    (void)context;
    return BrScalarToFloat(value);
}

static MMI32 background_float_scalar(void *context, float value)
{
    (void)context;
    return BrFloatToScalar(value);
}

static void background_perspective(void *context, unsigned short fov, MMI32 aspect, MMI32 near_z, MMI32 far_z,
                                   MMI32 out[16])
{
    br_matrix4 projection;
    (void)context;
    BrMatrix4Perspective(&projection, fov, aspect, near_z, far_z);
    memcpy(out, &projection, sizeof(projection));
}

void mm_brender_preview_background_services(MMBackgroundPreviewServices *services)
{
    services->mesh_renderer = mm_brender_background_mesh_services();
    services->scalar_float = background_scalar_float;
    services->float_scalar = background_float_scalar;
    services->perspective = background_perspective;
}

static void fill_pixelmap(void *context, void *map, MMU32 value)
{
    (void)context;
    BrPixelmapFill((br_pixelmap *)map, value);
}

static void preview_buffers(void *context, void *c, void *d, MMPreviewBuffer *out)
{
    br_pixelmap *colour = (br_pixelmap *)c, *depth = (br_pixelmap *)d;
    (void)context;
    out->colour = (unsigned char *)colour->pixels;
    out->depth = (unsigned char *)depth->pixels;
    out->colour_stride = colour->row_bytes;
    out->depth_stride = depth->row_bytes;
}

static int add_model(void *context, void *world, void *model)
{
    br_actor *actor = BrActorAllocate(BR_ACTOR_MODEL, NULL);
    (void)context;
    if (!actor)
        return 0;
    BrActorAdd((br_actor *)world, actor);
    actor->model = (br_model *)model;
    return 1;
}

static void *actor_material(void *context, void *actor)
{
    (void)context;
    return ((br_actor *)actor)->material;
}

static void *face_material(void *context, void *model, MMU32 i)
{
    (void)context;
    return ((br_model *)model)->face_groups[i].material;
}

static MMU32 face_material_count(void *context, void *model)
{
    (void)context;
    return ((br_model *)model)->nface_groups;
}

static void read_preview_material(void *context, void *p, MMPreviewMaterial *out)
{
    br_material *material = (br_material *)p;
    (void)context;
    out->material = material;
    out->flags = material->flags;
    out->map = material->colour_map;
    out->base = material->index_base;
    out->range = material->index_range;
}

static void write_preview_material(void *context, const MMPreviewMaterial *source)
{
    (void)context;
    apply_material((br_material *)source->material, (br_pixelmap *)source->map, source->flags,
                   source->base, source->range);
}

static void draw_edges(void *context, void *actor)
{
    actor_set_style(context, actor, BR_RSTYLE_EDGES);
}

static MMU32 vertex_count(void *context, void *model)
{
    (void)context;
    return ((br_model *)model)->nprepared_vertices;
}

static MMU32 face_count(void *context, void *model)
{
    (void)context;
    return mm_brender_model_faces(model);
}

static void read_vertex(void *context, void *model, MMU32 i, MMI32 out[3])
{
    const br_vertex *vertex = ((br_model *)model)->prepared_vertices + i;
    (void)context;
    memcpy(out, vertex->p.v, 3 * sizeof(*out));
}

static void read_face(void *context, void *model, MMU32 i, unsigned short out[3])
{
    const br_face *face = ((br_model *)model)->prepared_faces + i;
    (void)context;
    memcpy(out, face->vertices, 3 * sizeof(*out));
}

static void relative_matrix(void *context, void *actor, void *camera, MMI32 out[12])
{
    br_matrix34 matrix;
    (void)context;
    BrActorToActorMatrix34(&matrix, (br_actor *)actor, (br_actor *)camera);
    memcpy(out, &matrix, sizeof(matrix));
}

static void depth_projection(void *context, void *actor, MMI32 out[16])
{
    br_actor *camera = (br_actor *)actor;
    br_matrix4 matrix;
    (void)context;
    BrActorToScreenMatrix4(&matrix, camera, camera);
    memcpy(out, &matrix, sizeof(matrix));
}

static void preview_light_rotation(unsigned short pitch, unsigned short yaw, MMActorMatrix *out)
{
    br_matrix34 matrix;
    BrMatrix34RotateX(&matrix, pitch);
    BrMatrix34PostRotateY(&matrix, yaw);
    memcpy(out->m, matrix.m, sizeof(out->m));
}

void mm_brender_background_light_rotation(unsigned short pitch, unsigned short yaw, MMActorMatrix *out)
{
    /* Exported light matrices include the zero-roll step and its fixed-point rounding. */
    mm_brender_camera_matrix(out, 0, 0, 0, yaw, pitch, 0);
}

void mm_brender_camera_matrix(MMActorMatrix *out, float x, float y, float z, unsigned short yaw,
                              unsigned short pitch, unsigned short roll)
{
    br_matrix34 matrix;
    BrMatrix34RotateZ(&matrix, roll);
    BrMatrix34PostRotateX(&matrix, pitch);
    BrMatrix34PostRotateY(&matrix, yaw);
    matrix.m[3][0] = BrFloatToScalar(x);
    matrix.m[3][1] = BrFloatToScalar(y);
    matrix.m[3][2] = BrFloatToScalar(z);
    memcpy(out, &matrix, sizeof(matrix));
}

int mm_brender_matrix_inverse(MMActorMatrix *out, const MMActorMatrix *in)
{
    br_matrix34 matrix, inverse;
    memcpy(&matrix, in, sizeof(matrix));
    if (!BrMatrix34Inverse(&inverse, &matrix))
        return 0;
    memcpy(out, &inverse, sizeof(inverse));
    return 1;
}

void mm_brender_matrix_apply_point(MMActorPoint *out, const MMActorPoint *in, const MMActorMatrix *transform)
{
    br_matrix34 matrix;
    br_vector3 point, result;
    memcpy(&matrix, transform, sizeof(matrix));
    memcpy(&point, in, sizeof(point));
    BrMatrix34ApplyP(&result, &point, &matrix);
    memcpy(out, &result, sizeof(result));
}

void mm_brender_preview_services(MMPreviewRenderServices *s)
{
    mm_brender_preview_tree(&s->tree);
    s->attach_body_part = mm_brender_body_attach;
    s->frame_factory.colour = new_colour;
    s->frame_factory.depth = new_depth;
    s->frame_factory.world = new_world;
    s->frame_factory.free_pixel = free_pixelmap;
    s->frame_factory.free_world = free_world;
    s->fill = fill_pixelmap;
    s->buffers = preview_buffers;
    s->update_camera = update_camera;
    s->light_bytes = sizeof(br_light);
    s->light_rotation = preview_light_rotation;
    s->light = attach_light;
    s->enable_light = enable_light;
    s->disable_light = disable_light;
    s->add_model = add_model;
    s->material = actor_material;
    s->face_material = face_material;
    s->material_count = face_material_count;
    s->read_material = read_preview_material;
    s->write_material = write_preview_material;
    s->light_flag = BR_MATF_LIGHT;
    s->prelit_flag = BR_MATF_PRELIT;
    s->smooth_flag = BR_MATF_SMOOTH;
    s->edges = draw_edges;
    s->vertices = vertex_count;
    s->faces = face_count;
    s->vertex = read_vertex;
    s->face = read_face;
    s->relative = relative_matrix;
    s->render = render;
    s->depth_projection = depth_projection;
}
