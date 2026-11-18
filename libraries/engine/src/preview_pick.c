#include "mm/preview_pick.h"
#include "preview_pose.h"
#include <float.h>
#include <math.h>
#include <string.h>

typedef struct PickOwner
{
    void *actor;
    MMI32 object;
} PickOwner;
struct MMPreviewPickState
{
    const MMPreviewPickServices *s;
    MMCollectionStorage owners, faces, scratch;
    MMArray entries;
    MMPreviewPose pose;
    MMI32 candidate, object;
    float near_clip, closest, scale, origin;
    int failed;
};
static int finite_float(float x)
{
    return x >= -FLT_MAX && x <= FLT_MAX;
}
static int owner(void *v, void *actor, MMI32 object)
{
    MMPreviewPickState *p = (MMPreviewPickState *)v;
    PickOwner entry;
    entry.actor = actor;
    entry.object = object;
    return mm_array_add(&p->entries, &entry, 0) == MM_STORAGE_OK;
}
static void *tree_first(void *v, const void *actor)
{
    const MMPreviewTreeServices *t = &((MMPreviewPickState *)v)->s->render->tree;
    return t->first(t->context, actor);
}
static void *tree_next(void *v, const void *actor)
{
    const MMPreviewTreeServices *t = &((MMPreviewPickState *)v)->s->render->tree;
    return t->next(t->context, actor);
}
static void *tree_model(void *v, const void *actor)
{
    const MMPreviewTreeServices *t = &((MMPreviewPickState *)v)->s->render->tree;
    return t->model(t->context, actor);
}
static void *tree_clone(void *v, void *parent, const void *actor)
{
    const MMPreviewTreeServices *t = &((MMPreviewPickState *)v)->s->render->tree;
    return t->clone_attach(t->context, parent, actor);
}
static MMI32 wrap(float value, MMI32 extent)
{
    MMI32 coordinate = (MMI32)floor(value * extent);
    return (coordinate % extent + extent) % extent;
}
void mm_preview_pick_face(MMPreviewPickState *p, float distance, void *material, void *map)
{
    MMPreviewPickTexture t;
    float depth = distance * p->scale + p->origin;
    float uv[2];
    MMI32 x, y;
    if (p->failed || depth < p->near_clip || depth > p->closest)
        return;
    if (p->s->texture(p->s->context, material, &t) && !t.opaque && t.indexed && t.pixels && t.width > 0 && t.height > 0)
    {
        p->s->map(p->s->context, material, map, uv);
        x = wrap(uv[0], t.width);
        y = wrap(uv[1], t.height);
        if (!t.pixels[(y + t.base_y) * t.stride + x + t.base_x])
            return;
    }
    p->closest = depth;
    p->object = p->candidate;
}
void mm_preview_pick_model(MMPreviewPickState *p, void *actor, void *model, void *material, void *position,
                           void *direction, MMI32 near_clip, MMI32 far_clip)
{
    const MMPreviewPickServices *s = p->s;
    PickOwner *entries = (PickOwner *)p->owners.buffers[0].data;
    MMU32 i, n;
    unsigned char *faces;
    if (p->failed)
        return;
    p->candidate = -1;
    for (i = 0; i < p->entries.count; ++i)
        if (entries[i].actor == actor)
        {
            p->candidate = entries[i].object;
            break;
        }
    n = s->face_count(s->context, model);
    if (s->face_bytes == 0 || n > MM_STORAGE_LIMIT / s->face_bytes ||
        mm_storage_ensure(&p->faces, n * s->face_bytes, 0, 0) != MM_STORAGE_OK)
    {
        p->failed = 1;
        return;
    }
    faces = p->faces.buffers[0].data;
    for (i = 0; i < n; ++i)
        s->prepare_face(s->context, model, i, faces + i * s->face_bytes);
    s->model_pick(s->context, model, faces, material, position, direction, near_clip, far_clip, p);
}
static int pick(const MMPreviewProjection *p, int authored, int output_resolution, MMU32 width, MMU32 height, MMI32 x,
                MMI32 y, MMPreviewPickState *state, MMRenderFrame *frame)
{
    const MMPreviewPickServices *s = state->s;
    MMPreviewRenderServices r = *s->render;
    const MMPreviewMath *m = &r.math;
    const MMPreviewData *d = &state->pose.scene;
    MMPreviewMesh mesh;
    MMPreviewCamera camera;
    MMI32 matrix[16], fw, fh, z;
    MMU32 rw, rh, sx, sy;
    float scale;
    double denominator, distance;
    int bg, result;
    if (!r.ready(r.context))
        return MM_PREVIEW_PICK_INVALID;
    mm_preview_pose_describe(&state->pose, &r);
    if (!d->scene || !p->valid || width < 64 || height < 64 || width > 4096 || height > 4096 ||
        !finite_float(p->vertical_fov) || p->vertical_fov <= 0 || p->vertical_fov >= 179 || !finite_float(p->aspect) ||
        p->aspect <= 0 || !finite_float(p->near_clip) || !finite_float(p->far_clip) || p->near_clip <= 0 ||
        p->far_clip <= p->near_clip || p->far_clip > 32767)
        return MM_PREVIEW_PICK_INVALID;
    for (sx = 0; sx < 12; ++sx)
        if (!finite_float(p->transform[sx]) || fabs(p->transform[sx]) > 32767)
            return MM_PREVIEW_PICK_RANGE;
    if (x < 0 || y < 0 || x >= (MMI32)width || y >= (MMI32)height)
        return MM_PREVIEW_PICK_OK;
    bg = authored && d->background;
    rw = bg ? d->width : width;
    rh = bg ? d->height : height;
    if (bg && output_resolution)
    {
        if (width * d->height <= height * d->width)
        {
            rw = width;
            rh = d->height * width / d->width;
            if (!rh)
                rh = 1;
        }
        else
        {
            rh = height;
            rw = d->width * height / d->height;
            if (!rw)
                rw = 1;
        }
    }
    scale = (float)width / rw;
    if ((float)height / rh < scale)
        scale = (float)height / rh;
    fw = (MMI32)(rw * scale);
    fh = (MMI32)(rh * scale);
    x -= ((MMI32)width - fw) / 2;
    y -= ((MMI32)height - fh) / 2;
    if (x < 0 || y < 0 || x >= fw || y >= fh)
        return MM_PREVIEW_PICK_OK;
    x = (MMI32)(x / scale);
    y = (MMI32)(y / scale);
    if (x >= (MMI32)rw)
        x = (MMI32)rw - 1;
    if (y >= (MMI32)rh)
        y = (MMI32)rh - 1;
    mm_render_frame_allocate_world(frame, r.context, &r.frame_factory);
    memset(&camera, 0, sizeof(camera));
    camera.parallel = p->orthographic;
    if (p->orthographic)
    {
        if (!finite_float(p->ortho_height) || p->ortho_height <= 0 || p->ortho_height > 60000 ||
            !finite_float(p->ortho_height * p->aspect) || p->ortho_height * p->aspect > 60000)
            return MM_PREVIEW_PICK_SPAN;
        camera.height = m->scalar(p->ortho_height * .5f);
        camera.width = m->scalar(p->ortho_height * p->aspect * .5f);
    }
    camera.fov = m->angle(p->vertical_fov);
    camera.aspect = m->scalar(p->aspect);
    camera.near_z = m->scalar(p->near_clip);
    camera.far_z = m->scalar(p->far_clip);
    for (sx = 0; sx < 12; ++sx)
        camera.matrix[sx] = m->scalar(p->transform[sx]);
    if (frame->world)
        frame->camera = r.camera(r.context, frame->world, &camera);
    if (!frame->camera)
        return MM_PREVIEW_PICK_CAMERA;
    if (!s->invertible(s->context, frame->camera))
        return MM_PREVIEW_PICK_SINGULAR;
    r.tree.context = state;
    r.tree.owner = owner;
    r.tree.first = tree_first;
    r.tree.next = tree_next;
    r.tree.model = tree_model;
    r.tree.clone_attach = tree_clone;
    if (!mm_preview_pose_bodies(&state->pose, &r) ||
        mm_preview_pose_copy(&state->pose, frame->world, &state->scratch, &r) != MM_PREVIEW_OK ||
        (d->model && !r.add_model(r.context, frame->world, d->model)))
        return MM_PREVIEW_PICK_MODELS;
    if (!bg && d->background)
    {
        memset(&mesh, 0, sizeof(mesh));
        if (!r.mesh(r.context, &mesh) || !r.add_mesh(r.context, frame->world))
            return MM_PREVIEW_PICK_MODELS;
    }
    state->near_clip = p->near_clip;
    state->closest = p->far_clip;
    state->scale = p->orthographic ? p->far_clip - p->near_clip : 1;
    state->origin = p->orthographic ? p->near_clip : 0;
    if (bg)
    {
        sx = (MMU32)x * d->width / rw;
        sy = (MMU32)y * d->height / rh;
        z = d->depths[sy * d->width + sx];
        if (z != 0xffff)
        {
            if (z >= 32768)
                z -= 65536;
            s->perspective(s->context, &camera, matrix);
            denominator = m->scalar_float(matrix[10]) - z / 32768.0;
            distance = m->scalar_float(matrix[14]) / denominator;
            if (distance >= -DBL_MAX && distance <= DBL_MAX && distance > 0 && (float)distance < state->closest)
                state->closest = (float)distance;
        }
    }
    result = s->scene_pick(s->context, frame->world, frame->camera, rw, rh,
                          x - (MMI32)rw / 2, y - (MMI32)rh / 2, state);
    if (state->failed)
        return MM_PREVIEW_PICK_MEMORY;
    return result ? MM_PREVIEW_PICK_OK : MM_PREVIEW_PICK_RENDERER;
}
int mm_preview_pick(const MMPreviewProjection *p, int authored, int output_resolution, MMU32 width, MMU32 height,
                    MMI32 x, MMI32 y, MMI32 *object, float *distance, const MMPreviewPickServices *s)
{
    MMPreviewPickState state;
    const MMPreviewRenderServices *r = s->render;
    MMRenderFrame frame;
    int result;
    *object = -1;
    *distance = (float)HUGE_VAL;
    memset(&state, 0, sizeof(state));
    memset(&frame, 0, sizeof(frame));
    state.s = s;
    state.object = -1;
    mm_storage_init(&state.owners, r->allocator.resize ? &r->allocator : 0);
    mm_storage_init(&state.faces, r->allocator.resize ? &r->allocator : 0);
    mm_storage_init(&state.scratch, r->allocator.resize ? &r->allocator : 0);
    mm_preview_pose_init(&state.pose, r);
    state.entries.storage = &state.owners;
    state.entries.width = sizeof(PickOwner);
    state.entries.free_count = -1;
    result = pick(p, authored, output_resolution, width, height, x, y, &state, &frame);
    mm_render_frame_dispose(&frame);
    mm_preview_pose_dispose(&state.pose, r);
    mm_storage_dispose(&state.scratch);
    mm_storage_dispose(&state.faces);
    mm_storage_dispose(&state.owners);
    if (result == MM_PREVIEW_PICK_OK && state.object >= 0)
    {
        *object = state.object;
        *distance = state.closest;
    }
    return result;
}
