#include "scene_pick.h"
#include "scene_renderer.h"
#include "brender.h"
#include "mm/preview_pick.h"
#include <string.h>

typedef char PickMatrixSize[sizeof(br_matrix4) == 16 * sizeof(MMI32) ? 1 : -1];

typedef struct WorldHitContext
{
    MMBRenderActorHit *visit;
    void *context;
} WorldHitContext;

static int BR_CALLBACK visit_world_hit(br_actor *actor, br_model *model, br_material *material,
                                       br_vector3 *position, br_vector3 *direction, br_scalar near_z,
                                       br_scalar far_z, void *context)
{
    WorldHitContext *hit = (WorldHitContext *)context;
    (void)model;
    (void)material;
    (void)position;
    (void)direction;
    (void)far_z;
    return hit->visit(hit->context, actor, near_z);
}

static void visit_world(const MMWorldBuffers *buffers, const MMRenderFrame *frame, MMI32 x, MMI32 y,
                        MMBRenderActorHit *visit, void *context)
{
    WorldHitContext hit;
    hit.visit = visit;
    hit.context = context;
    mm_brender_pixelmap_bind((br_pixelmap *)frame->colour, &buffers->rgb);
    mm_brender_pixelmap_bind((br_pixelmap *)frame->depth, &buffers->z);
    BrScenePick2D((br_actor *)frame->world, (br_actor *)frame->camera, (br_pixelmap *)frame->colour, x, y,
                  visit_world_hit, &hit);
}

void mm_brender_world_visit(const MMWorldBuffers *buffers, const MMRenderFrame *frame, MMI32 x, MMI32 y,
                           MMBRenderActorHit *visit, void *context)
{
    if (mm_world_pick_point(buffers, x, y, &x, &y))
        visit_world(buffers, frame, x, y, visit, context);
}

typedef struct WorldPickContext
{
    MMWorldCamera *state;
    const MMWorldBuffers *buffers;
    const MMRenderFrame *frame;
} WorldPickContext;

static int closest_world_hit(void *context, void *actor, MMI32 near_z)
{
    return mm_world_pick_visit((MMWorldCamera *)context, actor, near_z);
}

static void find_world_hit(void *context, MMI32 x, MMI32 y)
{
    WorldPickContext *pick = (WorldPickContext *)context;
    visit_world(pick->buffers, pick->frame, x, y, closest_world_hit, pick->state);
}

void *mm_brender_world_pick(MMWorldCamera *state, const MMWorldBuffers *buffers, const MMRenderFrame *frame,
                            MMI32 x, MMI32 y)
{
    WorldPickContext pick;
    MMWorldCameraServices services = {0};
    pick.state = state;
    pick.buffers = buffers;
    pick.frame = frame;
    services.context = &pick;
    services.pick = find_world_hit;
    return mm_world_pick(state, buffers, x, y, &services);
}

static int BR_CALLBACK visit_preview_face(br_model *model, br_material *material, br_vector3 *position,
                                          br_vector3 *direction, br_scalar distance, int face, int edge, int vertex,
                                          br_vector3 *hit, br_vector2 *map, void *context)
{
    (void)model;
    (void)position;
    (void)direction;
    (void)face;
    (void)edge;
    (void)vertex;
    (void)hit;
    mm_preview_pick_face((MMPreviewPickState *)context, BrScalarToFloat(distance), material, map);
    return 0;
}

static int BR_CALLBACK visit_preview_model(br_actor *actor, br_model *model, br_material *material,
                                           br_vector3 *position, br_vector3 *direction, br_scalar near_z,
                                           br_scalar far_z, void *context)
{
    mm_preview_pick_model((MMPreviewPickState *)context, actor, model, material, position, direction, near_z, far_z);
    return 0;
}

static int camera_invertible(void *context, void *camera)
{
    br_matrix34 inverse;
    (void)context;
    return BrMatrix34Inverse(&inverse, &((br_actor *)camera)->t.t.mat) != 0;
}

static void perspective(void *context, const MMPreviewCamera *camera, MMI32 out[16])
{
    br_matrix4 matrix;
    (void)context;
    BrMatrix4Perspective(&matrix, camera->fov, camera->aspect, -camera->near_z, -camera->far_z);
    memcpy(out, &matrix, sizeof(matrix));
}

static void pick_preview_scene(void *context, void *world, void *camera, MMU32 width, MMU32 height, MMI32 x, MMI32 y,
                               MMPreviewPickState *state)
{
    br_pixelmap viewport = {0};
    (void)context;
    viewport.width = (br_uint_16)width;
    viewport.height = (br_uint_16)height;
    BrScenePick2D((br_actor *)world, (br_actor *)camera, &viewport, x, y, visit_preview_model, state);
}

static MMU32 face_count(void *context, void *model)
{
    (void)context;
    return ((br_model *)model)->nprepared_faces;
}

static void prepare_face(void *context, void *p, MMU32 index, void *out)
{
    br_model *model = (br_model *)p;
    br_face *face = (br_face *)out;
    br_vector3 normal;
    int axis;
    (void)context;
    *face = model->prepared_faces[index];
    for (axis = 0; axis < 3; ++axis)
        normal.v[axis] = BrFractionToScalar(face->n.v[axis]);
    face->d = BrVector3Dot(&normal, &model->prepared_vertices[face->vertices[0]].p);
}

static void pick_preview_model(void *context, void *p, void *faces, void *material, void *position, void *direction,
                               MMI32 near_z, MMI32 far_z, MMPreviewPickState *state)
{
    br_model *model = (br_model *)p;
    br_model prepared = *model;
    (void)context;
    prepared.faces = (br_face *)faces;
    prepared.vertices = model->prepared_vertices;
    prepared.nfaces = model->nprepared_faces;
    prepared.nvertices = model->nprepared_vertices;
    BrModelPick2D(&prepared, (br_material *)material, (br_vector3 *)position, (br_vector3 *)direction, near_z, far_z,
                  visit_preview_face, state);
}

static int pick_texture(void *context, void *p, MMPreviewPickTexture *out)
{
    const br_material *material = (const br_material *)p;
    const br_pixelmap *texture = material->colour_map;
    (void)context;
    if (!texture)
        return 0;
    out->opaque = (material->flags & BR_MATF_MAP_OPAQUE) != 0;
    out->indexed = texture->type == BR_PMT_INDEX_8;
    out->pixels = (const unsigned char *)texture->pixels;
    out->width = texture->width;
    out->height = texture->height;
    out->stride = texture->row_bytes;
    out->base_x = texture->base_x;
    out->base_y = texture->base_y;
    return 1;
}

static void map_texture(void *context, void *p, void *map, float out[2])
{
    br_material *material = (br_material *)p;
    br_vector2 uv;
    (void)context;
    BrMatrix23ApplyP(&uv, (br_vector2 *)map, &material->map_transform);
    out[0] = BrScalarToFloat(uv.v[0]);
    out[1] = BrScalarToFloat(uv.v[1]);
}

void mm_brender_preview_pick_services(MMPreviewPickServices *s)
{
    s->invertible = camera_invertible;
    s->perspective = perspective;
    s->scene_pick = pick_preview_scene;
    s->face_bytes = sizeof(br_face);
    s->face_count = face_count;
    s->prepare_face = prepare_face;
    s->model_pick = pick_preview_model;
    s->texture = pick_texture;
    s->map = map_texture;
}
