/* Preview frame control converted from archive/preview.cpp. */
#include "preview_pose.h"
#include "mm/model_resource.h"
#include "mm/preview_actor.h"
#include "mm/preview_camera.h"
#include "mm/preview_live.h"
#include <float.h>
#include <math.h>
#include <string.h>

typedef struct PreviewArray
{
    MMCollectionStorage storage;
    MMArray array;
} PreviewArray;
typedef struct PreviewFrame
{
    const MMPreviewRenderServices *s;
    MMRenderFrame targets;
    MMPreviewPose pose;
    PreviewArray lights, light_data, materials, models, pending, vertices;
    int sink;
} PreviewFrame;
static void array_init(PreviewArray *a, MMU32 width, const MMStorageAllocator *allocator)
{
    mm_storage_init(&a->storage, allocator && allocator->resize ? allocator : 0);
    memset(&a->array, 0, sizeof(a->array));
    a->array.storage = &a->storage;
    a->array.width = width;
    a->array.free_count = -1;
}
static void *data(PreviewArray *a)
{
    return a->storage.buffers[0].data;
}
static int add(PreviewArray *a, const void *value)
{
    return mm_array_add(&a->array, value, 0) == MM_STORAGE_OK;
}
static int count(PreviewArray *a, MMU32 n)
{
    return mm_array_set_count(&a->array, n) == MM_STORAGE_OK;
}
static float minf(float a, float b)
{
    return b < a ? b : a;
}
static float maxf(float a, float b)
{
    return a < b ? b : a;
}
static int finitef(float a)
{
    return a <= FLT_MAX && a >= -FLT_MAX;
}
static int finited(double a)
{
    return a <= DBL_MAX && a >= -DBL_MAX;
}
static void frame_init(PreviewFrame *f, const MMPreviewRenderServices *s)
{
    memset(f, 0, sizeof(*f));
    f->s = s;
    array_init(&f->lights, sizeof(void *), &s->allocator);
    array_init(&f->light_data, s->light_bytes, &s->allocator);
    array_init(&f->materials, sizeof(MMPreviewMaterial), &s->allocator);
    array_init(&f->models, sizeof(void *), &s->allocator);
    array_init(&f->pending, sizeof(void *), &s->allocator);
    mm_preview_pose_init(&f->pose, s);
    array_init(&f->vertices, 3 * sizeof(double), &s->allocator);
}
static void frame_dispose(PreviewFrame *f)
{
    const MMPreviewRenderServices *s = f->s;
    MMU32 i;
    MMPreviewMaterial *m = (MMPreviewMaterial *)data(&f->materials);
    void **lights = (void **)data(&f->lights);
    if (f->sink)
        s->sink_off(s->context);
    for (i = 0; i < f->materials.array.count; ++i)
        s->write_material(s->context, m + i);
    for (i = 0; i < f->lights.array.count; ++i)
        s->disable_light(s->context, lights[i]);
    mm_render_frame_dispose(&f->targets);
    mm_storage_dispose(&f->vertices.storage);
    mm_preview_pose_dispose(&f->pose, s);
    mm_storage_dispose(&f->pending.storage);
    mm_storage_dispose(&f->models.storage);
    mm_storage_dispose(&f->materials.storage);
    mm_storage_dispose(&f->light_data.storage);
    mm_storage_dispose(&f->lights.storage);
}
static void projection(MMPreviewProjection *p, const MMPreviewCamera *c, const MMPreviewMath *m)
{
    unsigned i;
    memset(p, 0, sizeof(*p));
    p->valid = 1;
    for (i = 0; i < 12; ++i)
        p->transform[i] = m->scalar_float(c->matrix[i]);
    p->vertical_fov = (float)c->fov * (360.f / 65536.f);
    p->aspect = m->scalar_float(c->aspect);
    p->near_clip = m->scalar_float(c->near_z);
    p->far_clip = m->scalar_float(c->far_z);
    p->orthographic = c->parallel;
    p->ortho_height = c->parallel ? m->scalar_float(c->height) * 2 : 0;
}
static int collect_models(PreviewFrame *f)
{
    const MMPreviewTreeServices *s = &f->s->tree;
    void *actor, *child;
    if (!add(&f->pending, &f->targets.world))
        return 0;
    while (f->pending.array.count)
    {
        mm_array_pop(&f->pending.array, &actor);
        if (s->model(s->context, actor) && !add(&f->models, &actor))
            return 0;
        for (child = s->first(s->context, actor); child; child = s->next(s->context, child))
            if (!add(&f->pending, &child))
                return 0;
    }
    return 1;
}
static int material(PreviewFrame *f, void *pointer, int display, unsigned char grey)
{
    const MMPreviewRenderServices *s = f->s;
    MMPreviewMaterial *old = (MMPreviewMaterial *)data(&f->materials), m;
    MMU32 i;
    if (!pointer)
        return 1;
    for (i = 0; i < f->materials.array.count; ++i)
        if (old[i].material == pointer)
            return 1;
    s->read_material(s->context, pointer, &m);
    if (!add(&f->materials, &m))
        return 0;
    if (display == 2)
        m.flags &= ~s->smooth_flag;
    if (display == 3)
        m.flags |= s->smooth_flag;
    if (display == 1 || display == 4)
    {
        m.map = 0;
        m.flags &= ~(s->light_flag | s->prelit_flag);
        m.base = grey;
        m.range = 0;
    }
    s->write_material(s->context, &m);
    return 1;
}
static int overrides(PreviewFrame *f, int display, const unsigned char *palette)
{
    const MMPreviewRenderServices *s = f->s;
    void **actors = (void **)data(&f->models), *model;
    MMU32 i, j, best = ~(MMU32)0, delta;
    unsigned char grey = 0;
    int r, g, b;
    for (i = 0; i < 256; ++i)
    {
        r = (int)palette[i * 4] - 160;
        g = (int)palette[i * 4 + 1] - 160;
        b = (int)palette[i * 4 + 2] - 160;
        delta = (MMU32)(r * r + g * g + b * b);
        if (delta < best)
        {
            best = delta;
            grey = (unsigned char)i;
        }
    }
    for (i = 0; i < f->models.array.count; ++i)
    {
        if (!material(f, s->material(s->context, actors[i]), display, grey))
            return 0;
        model = s->tree.model(s->tree.context, actors[i]);
        for (j = 0; j < s->material_count(s->context, model); ++j)
            if (!material(f, s->face_material(s->context, model, j), display, grey))
                return 0;
    }
    return 1;
}
static int fit_depth(PreviewFrame *f, MMPreviewCamera *camera, MMU32 width, MMU32 height)
{
    const MMPreviewRenderServices *s = f->s;
    const MMPreviewMath *m = &s->math;
    double slope = tan((float)camera->fov * (6.283185307179586 / 65536) * .5), value, near_z, far_z, triangle[3][3];
    MMPreviewDepthRange range;
    MMI32 matrix[12], vertex[3];
    unsigned short face[3];
    MMU32 i, j, n, axis, source, k;
    void **actors = (void **)data(&f->models), *model;
    double (*vertices)[3];
    mm_preview_depth_init(
        &range,
        (camera->parallel ? m->scalar_float(camera->width) : slope * m->scalar_float(camera->aspect)) *
            (1 + 2.0 / width),
        (camera->parallel ? m->scalar_float(camera->height) : slope) * (1 + 2.0 / height),
        m->scalar_float(camera->near_z), m->scalar_float(camera->far_z), camera->parallel);
    for (i = 0; i < f->models.array.count; ++i)
    {
        model = s->tree.model(s->tree.context, actors[i]);
        s->relative(s->context, actors[i], f->targets.camera, matrix);
        n = s->vertices(s->context, model);
        if (!count(&f->vertices, n))
            return 0;
        vertices = (double (*)[3])data(&f->vertices);
        for (j = 0; j < n; ++j)
        {
            s->vertex(s->context, model, j, vertex);
            for (axis = 0; axis < 3; ++axis)
            {
                value = m->scalar_float(matrix[9 + axis]);
                for (source = 0; source < 3; ++source)
                    value += (double)m->scalar_float(vertex[source]) * m->scalar_float(matrix[source * 3 + axis]);
                vertices[j][axis] = axis == 2 ? -value : value;
            }
        }
        for (j = 0; j < s->faces(s->context, model); ++j)
        {
            s->face(s->context, model, j, face);
            for (k = 0; k < 3; ++k)
                memcpy(triangle[k], vertices[face[k]], sizeof(triangle[k]));
            mm_preview_depth_include(&range, (const double (*)[3])triangle);
        }
    }
    mm_preview_depth_fit(&range, &near_z, &far_z);
    camera->near_z = m->scalar_double(near_z);
    camera->far_z = m->scalar_double(far_z);
    s->update_camera(s->context, f->targets.camera, camera);
    return 1;
}
/* Stable merging preserves the visibility clock's draw order, including ties.
 * The isolated body precedes actor instances, as in the original host. */
static int body_before(const MMPreviewBody *a, const MMPreviewBody *b)
{
    return a->owner < 0 || (b->owner >= 0 && (a->order_high < b->order_high ||
                                              (a->order_high == b->order_high && a->order_low <= b->order_low)));
}
static int sort_bodies(MMPreviewPose *pose, MMCollectionStorage *storage)
{
    MMPreviewBody *b = (MMPreviewBody *)pose->render.storage.buffers[0].data, *scratch;
    MMU32 n = pose->render.bodies.count, width, start, middle, end, i, j, k;
    if (n < 2)
        return 1;
    /* The pending traversal buffer is unused until after bodies are copied. */
    if (mm_storage_ensure(storage, n * sizeof(*b), 0, 0) != MM_STORAGE_OK)
        return 0;
    scratch = (MMPreviewBody *)storage->buffers[0].data;
    for (width = 1; width < n; width *= 2)
    {
        for (start = 0; start < n; start = end)
        {
            middle = start + width;
            if (middle > n)
                middle = n;
            end = middle + width;
            if (end > n)
                end = n;
            i = start;
            j = middle;
            for (k = start; k < end; ++k)
            {
                if (i < middle && (j == end || body_before(b + i, b + j)))
                    scratch[k] = b[i++];
                else
                    scratch[k] = b[j++];
            }
        }
        memcpy(b, scratch, n * sizeof(*b));
    }
    return 1;
}
typedef struct PreviewDraw
{
    const MMPreviewRenderServices *services;
    void *world, **nodes;
    MMI32 owner;
} PreviewDraw;

static int attach_part(void *context, MMI32 index, MMI32 parent, const MMBodyPartBinding *binding)
{
    PreviewDraw *draw = (PreviewDraw *)context;
    const MMPreviewRenderServices *s = draw->services;
    void *node = s->attach_body_part(s->context, parent < 0 ? draw->world : draw->nodes[parent], index, binding);
    if (!node)
        return 0;
    draw->nodes[index] = node;
    return !binding->model_handle || !s->tree.owner || s->tree.owner(s->tree.context, node, draw->owner);
}

int mm_preview_pose_submit(MMPreviewPose *pose, void *world, MMCollectionStorage *scratch,
                           const MMPreviewRenderServices *s)
{
    const MMPreviewBody *bodies;
    PreviewDraw draw;
    MMU32 i, nodes;
    if (!sort_bodies(pose, scratch))
        return MM_PREVIEW_MEMORY;
    bodies = (const MMPreviewBody *)pose->render.storage.buffers[0].data;
    draw.services = s;
    draw.world = world;
    for (i = 0; i < pose->render.bodies.count; ++i)
    {
        if (bodies[i].draw.part_count < 0 || !bodies[i].draw.bindings)
            return MM_PREVIEW_MODELS;
        nodes = (MMU32)bodies[i].draw.part_count + 2;
        if (nodes > MM_STORAGE_LIMIT / sizeof(*draw.nodes) ||
            mm_storage_ensure(scratch, nodes * sizeof(*draw.nodes), 0, 0) != MM_STORAGE_OK)
            return MM_PREVIEW_MEMORY;
        draw.nodes = (void **)scratch->buffers[0].data;
        draw.owner = bodies[i].owner;
        if (!mm_body_draw(&bodies[i].draw, &draw, attach_part))
            return MM_PREVIEW_MODELS;
    }
    return MM_PREVIEW_OK;
}
static void retain_body(void *context, void *resource)
{
    (void)context;
    mm_body_owner_retain((MMBodyOwner *)resource);
}

static void release_body(void *context, void *resource)
{
    (void)context;
    mm_body_owner_release((MMBodyOwner *)resource);
}

int mm_preview_ready(const MMPreviewRenderServices *s)
{
    return s->live ? s->live->status.ready : s->ready(s->context);
}

const MMMovieLiveFrame *mm_preview_position(const MMPreviewRenderServices *s)
{
    if (!s->live)
        return s->position;
    return s->live->publication.phase && s->live->publication.frame == s->live->status.frame ?
        &s->live->publication : NULL;
}

void mm_preview_pose_init(MMPreviewPose *pose, const MMPreviewRenderServices *s)
{
    memset(pose, 0, sizeof(*pose));
    mm_movie_render_scene_init(&pose->render, s->movie, s->allocator.resize ? &s->allocator : NULL,
                               s->context, s->live ? retain_body : s->retain_pose_resource,
                               s->live ? release_body : s->release_pose_resource);
}

void mm_preview_describe(MMPreviewData *d, const MMPreviewRenderServices *s)
{
    const unsigned char *palette;
    const MMBackgroundPreviewOwner *background;
    const MMBackgroundPreview *b;
    const MMBackgroundCameraData *camera;
    MMU32 i;
    if (s->live)
    {
        d->scene = s->live->status.scene;
        if (s->live->assets.model)
        {
            MMModelDescription model;
            d->model_resource = s->live->assets.model;
            d->model = mm_model_owner_resource(s->live->assets.model)->handle;
            mm_model_owner_describe(s->live->assets.model, &model);
            for (i = 0; i < 3; ++i)
            {
                d->model_low[i] = s->math.scalar_float(model.bounds.min[i]);
                d->model_high[i] = s->math.scalar_float(model.bounds.max[i]);
            }
        }
    }
    else
        s->describe(s->context, d);
    if (!s->live || !s->base_palette)
        return;
    background = s->live->assets.background;
    palette = background ? mm_background_preview_palette(background) : s->base_palette;
    for (i = 0; i < sizeof(d->palette); i += 4)
    {
        d->palette[i] = palette[i + 2];
        d->palette[i + 1] = palette[i + 1];
        d->palette[i + 2] = palette[i];
        d->palette[i + 3] = 255;
    }
    d->background = background && s->only_object < 0;
    if (!d->background)
        return;
    b = mm_background_preview_state(background);
    camera = &b->camera.data;
    d->width = b->width;
    d->height = b->height;
    d->light_count = b->light_count;
    d->lights = (const MMBackgroundLight *)b->metadata.buffers[1].data;
    d->colours = b->images.buffers[0].data;
    d->depths = (const unsigned short *)b->images.buffers[1].data;
    memcpy(d->camera.matrix, camera->matrix.m, sizeof(d->camera.matrix));
    d->camera.fov = camera->fov;
    d->camera.near_z = camera->near_z;
    d->camera.far_z = camera->far_z;
    d->camera.aspect = s->math.scalar((float)b->width / b->height);
}

void mm_preview_pose_describe(MMPreviewPose *pose, const MMPreviewRenderServices *s)
{
    mm_preview_describe(&pose->scene, s);
    if (s->live && pose->scene.model_resource)
        mm_model_owner_retain((MMModelOwner *)pose->scene.model_resource);
    else if (pose->scene.model_resource && s->retain_model && s->release_model)
        s->retain_model(s->context, pose->scene.model_resource);
}

int mm_preview_pose_mesh(MMPreviewPose *pose, MMPreviewMesh *out, const MMPreviewRenderServices *s)
{
    if (!s->live)
        return s->mesh(s->context, out) ? MM_PREVIEW_OK : MM_PREVIEW_HOST;
    pose->mesh = mm_background_preview_owner_mesh(s->live->assets.background);
    if (!pose->mesh)
        return MM_PREVIEW_MEMORY;
    out->empty = pose->mesh->triangle_count == 0;
    memcpy(out->low, pose->mesh->low, sizeof(out->low));
    memcpy(out->high, pose->mesh->high, sizeof(out->high));
    out->infinite = pose->mesh->infinite_count ? pose->mesh->pixels.buffers[1].data : NULL;
    return MM_PREVIEW_OK;
}

int mm_preview_pose_add_mesh(const MMPreviewPose *pose, void *world, const MMPreviewRenderServices *s)
{
    if (pose->mesh)
        return mm_background_preview_add_mesh(s->live->assets.background, pose->mesh, world);
    return s->add_mesh(s->context, world);
}

MMU32 mm_preview_body_count(const MMPreviewRenderServices *s)
{
    return s->live ? s->live->actors.count + 1 : s->body_count(s->context);
}

int mm_preview_read_body(MMPreviewBody *out, MMU32 index, int bounds, const MMPreviewRenderServices *s)
{
    MMBodyOwner *body;
    const MMBodyShape *shape;
    memset(out, 0, sizeof(*out));
    if (!s->live)
        return s->body(s->context, index, out);
    if (s->only_object >= 0 && index != (MMU32)s->only_object + 1)
        return 0;
    if (index)
    {
        const MMPreviewActor *actor = mm_preview_live_actor_view(s->live, index - 1);
        if (!actor || !actor->state.onStage)
            return 0;
        body = (MMBodyOwner *)actor->body;
        out->order_low = actor->draw_order.low;
        out->order_high = (MMU32)actor->draw_order.high;
    }
    else
        body = s->live->assets.body;
    if (!body)
        return 0;
    shape = &mm_body_owner_state(body)->shape;
    out->draw.bindings = shape->bindings;
    out->draw.part_count = shape->part_count;
    out->resource = body;
    out->owner = index ? (MMI32)(index - 1) : -1;
    if (bounds)
    {
        MMBodyBounds box;
        int axis;
        mm_body_bounds(shape, 1, &box, s->body_geometry);
        for (axis = 0; axis < 3; ++axis)
        {
            out->low[axis] = s->math.scalar_float(box.min[axis]);
            out->high[axis] = s->math.scalar_float(box.max[axis]);
        }
    }
    return 1;
}

int mm_preview_pose_bodies(MMPreviewPose *pose, int bounds, const MMPreviewRenderServices *s)
{
    MMPreviewBody body;
    MMU32 i, n = mm_preview_body_count(s);
    for (i = 0; i < n; ++i)
        if (mm_preview_read_body(&body, i, bounds, s) && !mm_movie_render_scene_add(&pose->render, &body))
            return 0;
    return 1;
}

void mm_preview_pose_dispose(MMPreviewPose *pose, const MMPreviewRenderServices *s)
{
    mm_movie_render_scene_dispose(&pose->render);
    if (s->live && pose->scene.model_resource)
        mm_model_owner_release((MMModelOwner *)pose->scene.model_resource);
    else if (pose->scene.model_resource && s->retain_model && s->release_model)
        s->release_model(s->context, pose->scene.model_resource);
    pose->scene.model_resource = NULL;
}

int mm_preview_copy_bodies(void *world, const MMPreviewRenderServices *s)
{
    MMPreviewPose pose;
    MMCollectionStorage scratch;
    int ok;
    mm_preview_pose_init(&pose, s);
    mm_storage_init(&scratch, s->allocator.resize ? &s->allocator : 0);
    ok = mm_preview_pose_bodies(&pose, 0, s) && mm_preview_pose_submit(&pose, world, &scratch, s) == MM_PREVIEW_OK;
    mm_storage_dispose(&scratch);
    mm_preview_pose_dispose(&pose, s);
    return ok;
}
static void blit(const MMPreviewData *d, const MMPreviewBuffer *b, MMU32 width, MMU32 height,
                 int diagnostic)
{
    MMU32 x, y, sx, sy;
    unsigned char *colours;
    unsigned short *depths;
    for (y = 0; y < height; ++y)
    {
        colours = b->colour + y * b->colour_stride;
        depths = (unsigned short *)(b->depth + y * b->depth_stride);
        if (width == d->width && height == d->height)
        {
            if (!diagnostic)
                memcpy(colours, d->colours + y * width, width);
            memcpy(depths, d->depths + y * width, width * sizeof(*depths));
            continue;
        }
        sy = y * d->height / height;
        for (x = 0; x < width; ++x)
        {
            sx = x * d->width / width;
            if (!diagnostic)
                colours[x] = d->colours[sy * d->width + sx];
            depths[x] = d->depths[sy * d->width + sx];
        }
    }
}
static void output_pixels(MMPreviewImage *image, const MMPreviewView *v, const MMPreviewData *d,
                          const MMPreviewMesh *mesh, const MMPreviewBuffer *b,
                          const MMPreviewRenderServices *s, void *camera, MMU32 width, MMU32 height,
                          int bg, int infinite)
{
    float scale = minf((float)v->width / width, (float)v->height / height);
    MMU32 fw = (MMU32)(width * scale), fh = (MMU32)(height * scale), left = (v->width - fw) / 2,
               top = (v->height - fh) / 2, x, y, sx, sy, bx, by, index, offset;
    MMI32 matrix[16], z;
    const unsigned char *gpu, *row;
    const unsigned short *depths;
    unsigned char *out, *indices;
    double depth_scale, depth_offset, denominator, distance;
    memset(matrix, 0, sizeof(matrix));
    if (v->depth)
        s->depth_projection(s->context, camera, matrix);
    depth_scale = s->math.scalar_float(matrix[10]);
    depth_offset = s->math.scalar_float(matrix[14]);
    gpu = v->rasterizer ? s->raster_rgba(s->context) : 0;
    indices = !gpu && s->palette_indices ? s->palette_indices(s->context, v->width, v->height) : NULL;
    if (indices)
        memset(indices, 0, v->width * v->height);
    memset(image->rgba, 0, v->width * v->height * 4);
    for (y = 0; y < fh; ++y)
    {
        sy = (MMU32)(y / scale);
        if (sy >= height)
            sy = height - 1;
        row = b->colour + sy * b->colour_stride;
        depths = (const unsigned short *)(b->depth + sy * b->depth_stride);
        for (x = 0; x < fw; ++x)
        {
            sx = (MMU32)(x / scale);
            if (sx >= width)
                sx = width - 1;
            offset = (y + top) * v->width + x + left;
            out = image->rgba + offset * 4;
            index = row[sx];
            if (infinite && depths[sx] == 0xffff)
            {
                bx = sx * d->width / width;
                by = sy * d->height / height;
                index = mesh->infinite[by * d->width + bx];
            }
            memcpy(out, d->palette + index * 4, 3);
            if (indices)
                indices[offset] = (unsigned char)index;
            out[3] = !bg && !infinite && depths[sx] == 0xffff ? 0 : 255;
            if (gpu && !(infinite && depths[sx] == 0xffff))
                memcpy(out, gpu + (sy * width + sx) * 4, 4);
            if (out[3])
                ++image->covered;
            if (v->depth && depths[sx] != 0xffff)
            {
                z = depths[sx];
                if (z >= 32768)
                    z -= 65536;
                denominator = depth_scale - z / 32768.0;
                distance = image->projection.orthographic ? (z / 32768.0 + depth_offset) / depth_scale
                                                          : depth_offset / denominator;
                if (finited(distance) && distance > 0)
                    image->depth[offset] = (float)distance;
            }
        }
    }
}
static int render(PreviewFrame *f, const MMPreviewView *v, MMPreviewOrbit *orbit, MMPreviewImage *image)
{
    const MMPreviewRenderServices *s = f->s;
    const MMPreviewMath *m = &s->math;
    const MMPreviewData *d = &f->pose.scene;
    const MMBackgroundLightRig *rig = s->light_override;
    MMPreviewMesh mesh;
    MMPreviewBuffer buffers;
    MMPreviewCamera camera;
    MMPreviewProjection scene_camera;
    const MMPreviewBody *bodies;
    MMBackgroundLight light;
    MMU32 width, height, i, j, n;
    int bg, has_mesh, diagnostic, infinite, first = 1, axis, corner, fits, object = -1, valid, axis_view;
    float low[3] = {0, 0, 0}, high[3] = {1, 1, 1}, guide_low[3], guide_high[3], center[3], anchor[3] = {0, 0, 0},
          corners[24], object_low[3], object_high[3];
    float half_height, coordinate, squared, radius, guide_radius, guide_distance, aspect, distance, yaw_degrees,
        pitch_degrees, yaw, pitch, roll, sy, cy, sp, cp, sr, cr, basis[3][3], position, x, y, z, cosine, sine;
    void *actor;
    void **actors;
    memset(&mesh, 0, sizeof(mesh));
    memset(&scene_camera, 0, sizeof(scene_camera));
    mm_preview_pose_describe(&f->pose, s);
    bg = v->authored && !v->free_camera && d->background;
    has_mesh = !bg && d->background;
    if (has_mesh)
    {
        int status = mm_preview_pose_mesh(&f->pose, &mesh, s);
        if (status != MM_PREVIEW_OK)
            return status;
    }
    diagnostic = v->display == 1 || v->display >= 4;
    infinite = !diagnostic && has_mesh && mesh.infinite;
    width = bg ? d->width : v->width;
    height = bg ? d->height : v->height;
    if (bg && v->output_resolution)
    {
        if (v->width * d->height <= v->height * d->width)
        {
            width = v->width;
            height = d->height * v->width / d->width;
            if (!height)
                height = 1;
        }
        else
        {
            height = v->height;
            width = d->width * v->height / d->height;
            if (!width)
                width = 1;
        }
    }
    if (!mm_render_frame_allocate(&f->targets, width, height, s->context, &s->frame_factory))
        return MM_PREVIEW_BUFFERS;
    s->fill(s->context, f->targets.colour, 0);
    s->fill(s->context, f->targets.depth, 0xffff);
    s->buffers(s->context, f->targets.colour, f->targets.depth, &buffers);
    if (bg)
        blit(d, &buffers, width, height, diagnostic);
    if (!mm_preview_pose_bodies(&f->pose, 1, s))
        return MM_PREVIEW_MEMORY;
    bodies = (const MMPreviewBody *)f->pose.render.storage.buffers[0].data;
    for (i = 0; i < f->pose.render.bodies.count; ++i)
    {
        for (axis = 0; axis < 3; ++axis)
        {
            low[axis] = first ? bodies[i].low[axis] : minf(low[axis], bodies[i].low[axis]);
            high[axis] = first ? bodies[i].high[axis] : maxf(high[axis], bodies[i].high[axis]);
        }
        first = 0;
    }
    if (d->model)
    {
        memcpy(low, d->model_low, sizeof(low));
        memcpy(high, d->model_high, sizeof(high));
    }
    if (has_mesh && !mesh.empty)
    {
        for (axis = 0; axis < 3; ++axis)
        {
            low[axis] = first && !d->model ? mesh.low[axis] : minf(low[axis], mesh.low[axis]);
            high[axis] = first && !d->model ? mesh.high[axis] : maxf(high[axis], mesh.high[axis]);
        }
        first = 0;
    }
    if (d->background)
    {
        projection(&scene_camera, &d->camera, m);
        scene_camera.aspect = (float)d->width / d->height;
        scene_camera.display_depth = 10.f;
    }
    else if (d->scene && v->frame_camera)
    {
        scene_camera.valid = 1;
        scene_camera.transform[0] = scene_camera.transform[4] = scene_camera.transform[8] = 1;
        scene_camera.transform[11] = 10;
        scene_camera.vertical_fov = 45;
        scene_camera.aspect = 544.f / 306;
        scene_camera.near_clip = .1f;
        scene_camera.far_clip = 30000;
        scene_camera.display_depth = 10;
    }
    if (scene_camera.valid && v->frame_camera && (!v->free_camera || v->stable_orbit) && !bg &&
        scene_camera.vertical_fov > 0 && scene_camera.vertical_fov < 179)
    {
        for (axis = 0; axis < 3; ++axis)
        {
            guide_low[axis] = first && !d->model ? scene_camera.transform[9 + axis] : low[axis];
            guide_high[axis] = first && !d->model ? scene_camera.transform[9 + axis] : high[axis];
        }
        half_height = m->tan(scene_camera.vertical_fov * .00872664626f) * scene_camera.display_depth;
        for (corner = 0; corner < 5; ++corner)
            for (axis = 0; axis < 3; ++axis)
            {
                coordinate = scene_camera.transform[9 + axis];
                if (corner < 4)
                    coordinate +=
                        scene_camera.transform[axis] * half_height * scene_camera.aspect * ((corner & 1) ? 1 : -1) +
                        scene_camera.transform[3 + axis] * half_height * ((corner & 2) ? 1 : -1) -
                        scene_camera.transform[6 + axis] * scene_camera.display_depth;
                guide_low[axis] = minf(guide_low[axis], coordinate);
                guide_high[axis] = maxf(guide_high[axis], coordinate);
            }
        squared = 0;
        for (axis = 0; axis < 3; ++axis)
            squared += (guide_high[axis] - guide_low[axis]) * (guide_high[axis] - guide_low[axis]) * .25f;
        guide_radius = maxf(.1f, m->sqrt(squared));
        guide_distance = guide_radius * 2.8f / minf(1.f, (float)width / height) / v->zoom;
        fits = guide_distance + guide_radius * 4 <= 30000;
        for (axis = 0; axis < 3; ++axis)
            fits = fits && (float)fabs((guide_low[axis] + guide_high[axis]) * .5f) + guide_distance <= 32700;
        if (fits)
        {
            memcpy(low, guide_low, sizeof(low));
            memcpy(high, guide_high, sizeof(high));
        }
    }
    squared = 0;
    for (axis = 0; axis < 3; ++axis)
    {
        center[axis] = (low[axis] + high[axis]) * .5f;
        squared += (high[axis] - low[axis]) * (high[axis] - low[axis]) * .25f;
    }
    radius = maxf(.1f, m->sqrt(squared));
    if (!bg && !v->authored && !v->free_camera && v->object >= 0)
    {
        if (!s->object_bounds(s->context, v->object, corners, &valid))
            return MM_PREVIEW_HOST;
        if (valid)
        {
            object = v->object;
            memcpy(object_low, corners, sizeof(object_low));
            memcpy(object_high, corners, sizeof(object_high));
            for (corner = 1; corner < 8; ++corner)
                for (axis = 0; axis < 3; ++axis)
                {
                    object_low[axis] = minf(object_low[axis], corners[corner * 3 + axis]);
                    object_high[axis] = maxf(object_high[axis], corners[corner * 3 + axis]);
                }
            squared = 0;
            for (axis = 0; axis < 3; ++axis)
            {
                center[axis] = anchor[axis] = (object_low[axis] + object_high[axis]) * .5f;
                squared += (object_high[axis] - object_low[axis]) * (object_high[axis] - object_low[axis]) * .25f;
            }
            radius = maxf(.1f, m->sqrt(squared));
        }
    }
    if (v->stable_orbit && !bg)
    {
        if (orbit->framed && orbit->includes_camera == v->frame_camera && orbit->object == object)
        {
            memcpy(center, orbit->center, sizeof(center));
            if (object >= 0)
                for (axis = 0; axis < 3; ++axis)
                    center[axis] += anchor[axis] - orbit->anchor[axis];
            radius = orbit->radius;
        }
        else
        {
            orbit->radius = radius;
            orbit->framed = 1;
            orbit->includes_camera = v->frame_camera;
        }
        memcpy(orbit->center, center, sizeof(center));
        memcpy(orbit->anchor, anchor, sizeof(anchor));
        orbit->object = object;
    }
    aspect = (float)width / height;
    distance = radius * 2.8f / minf(1.f, aspect) / v->zoom;
    if (!bg && !v->free_camera && distance + radius * 4 > 30000)
        return MM_PREVIEW_ASSET_RANGE;
    memset(&camera, 0, sizeof(camera));
    camera.fov = bg ? d->camera.fov : m->angle(v->vertical_fov);
    camera.near_z = bg ? d->camera.near_z : m->scalar(v->free_camera ? .1f : maxf(.01f, distance / 1000));
    camera.far_z = bg ? d->camera.far_z : m->scalar(30000.f);
    if (v->free_camera)
        camera.near_z = m->scalar(.1f);
    if (!bg && v->near_clip > 0)
        camera.near_z = m->scalar(v->near_clip);
    camera.aspect = m->scalar(bg ? (float)d->width / d->height : aspect);
    f->targets.camera = s->camera(s->context, f->targets.world, &camera);
    if (!f->targets.camera)
        return MM_PREVIEW_CAMERA;
    axis_view = !bg && v->axis > 0;
    yaw_degrees = axis_view ? (v->axis == 2 ? 90.f : 0.f) : v->yaw;
    pitch_degrees = axis_view ? (v->axis == 3 ? 90.f : 0.f) : v->pitch;
    yaw = m->fmod(yaw_degrees, 360.f) * .01745329252f;
    pitch = pitch_degrees * .01745329252f;
    sy = m->sin(yaw);
    cy = m->cos(yaw);
    sp = m->sin(pitch);
    cp = m->cos(pitch);
    roll = (axis_view ? 0 : v->roll) * .01745329252f;
    cr = m->cos(roll);
    sr = m->sin(roll);
    basis[0][0] = cy * cr - sy * sp * sr;
    basis[0][1] = cp * sr;
    basis[0][2] = -sy * cr - cy * sp * sr;
    basis[1][0] = -sy * sp * cr - cy * sr;
    basis[1][1] = cp * cr;
    basis[1][2] = -cy * sp * cr + sy * sr;
    basis[2][0] = sy * cp;
    basis[2][1] = sp;
    basis[2][2] = cy * cp;
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 3; ++j)
            camera.matrix[i * 3 + j] = m->scalar(basis[i][j]);
    for (axis = 0; axis < 3; ++axis)
    {
        position = v->free_camera && !axis_view ? v->position[axis] : center[axis] + basis[2][axis] * distance;
        if (!bg && (position < -32767.f || position > 32767.f))
            return MM_PREVIEW_CAMERA_RANGE;
        if (!bg)
            camera.matrix[9 + axis] = m->scalar(position);
    }
    if (bg)
        memcpy(camera.matrix, d->camera.matrix, sizeof(camera.matrix));
    if (!bg && v->orthographic)
    {
        camera.parallel = 1;
        camera.height = m->scalar(minf(15000.f, radius * 1.15f / v->zoom));
        camera.width = m->scalar(minf(30000.f, m->scalar_float(camera.height) * aspect));
    }
    s->update_camera(s->context, f->targets.camera, &camera);
    n = d->background ? (rig ? rig->count : d->light_count) : 1;
    if (!count(&f->light_data, n) || mm_array_reserve(&f->lights.array, n, 0) != MM_STORAGE_OK)
        return MM_PREVIEW_MEMORY;
    for (i = 0; i < n; ++i)
    {
        if (d->background)
            light = rig ? rig->lights[i] : d->lights[i];
        else
        {
            memset(&light, 0, sizeof(light));
            memcpy(&light.matrix, camera.matrix, sizeof(camera.matrix));
            light.type = MM_BACKGROUND_LIGHT_DIRECT;
            light.intensity = m->scalar(1.f);
        }
        actor = s->light(s->context, f->targets.world, (unsigned char *)data(&f->light_data) + i * s->light_bytes, &light);
        if (!actor)
            return MM_PREVIEW_LIGHTS;
        if (!add(&f->lights, &actor))
            return MM_PREVIEW_MEMORY;
        s->enable_light(s->context, actor);
    }
    {
        int submitted = mm_preview_pose_submit(&f->pose, f->targets.world, &f->pending.storage, s);
        if (submitted != MM_PREVIEW_OK)
            return submitted;
    }
    if (d->model && !s->add_model(s->context, f->targets.world, d->model))
        return MM_PREVIEW_MODELS;
    if (has_mesh && !mm_preview_pose_add_mesh(&f->pose, f->targets.world, s))
        return MM_PREVIEW_MESH;
    if (v->display == 1 || v->display == 2 || v->display == 3 || (v->display == 4 && !v->rasterizer) ||
        (!bg && v->fit_depth))
        if (!collect_models(f))
            return MM_PREVIEW_MEMORY;
    if (v->display == 1 || v->display == 2 || v->display == 3 || (v->display == 4 && !v->rasterizer))
        if (!overrides(f, v->display, d->palette))
            return MM_PREVIEW_MEMORY;
    if (v->display == 4 && !v->rasterizer)
    {
        actors = (void **)data(&f->models);
        for (i = 0; i < f->models.array.count; ++i)
            s->edges(s->context, actors[i]);
    }
    if (!bg && v->fit_depth && !fit_depth(f, &camera, width, height))
        return MM_PREVIEW_MEMORY;
    mm_movie_render_scene_publish(&f->pose.render, &camera, mm_preview_position(s), &f->targets);
    f->sink = 1;
    projection(&image->projection, &f->targets.scene->camera, m);
    if (v->rasterizer && !s->raster_begin(s->context, &image->projection, d->palette, bg || infinite, width, height))
        return MM_PREVIEW_HOST;
    s->render(s->context, &f->targets);
    s->sink_off(s->context);
    if (!s->renderer_ok(s->context))
        return MM_PREVIEW_RENDERER;
    if (v->rasterizer && !s->raster_end(s->context, &buffers))
        return MM_PREVIEW_HOST;
    image->width = v->width;
    image->height = v->height;
    for (axis = 0; axis < 3; ++axis)
        image->position[axis] = m->scalar_float(camera.matrix[9 + axis]);
    image->yaw = m->atan2(m->scalar_float(camera.matrix[6]), m->scalar_float(camera.matrix[8])) * 57.2957795f;
    image->pitch =
        maxf(-90.f, minf(90.f, m->asin(maxf(-1.f, minf(1.f, m->scalar_float(camera.matrix[7])))) * 57.2957795f));
    if (!bg)
    {
        image->yaw = m->fmod(yaw_degrees, 360.f);
        image->pitch = pitch_degrees;
    }
    image->move_speed = maxf(1.f, minf(1000.f, radius));
    image->roll = v->roll;
    if (bg)
    {
        yaw = image->yaw * .01745329252f;
        pitch = image->pitch * .01745329252f;
        x = m->scalar_float(camera.matrix[0]);
        y = m->scalar_float(camera.matrix[1]);
        z = m->scalar_float(camera.matrix[2]);
        cosine = x * m->cos(yaw) - z * m->sin(yaw);
        sine = -x * m->sin(yaw) * m->sin(pitch) + y * m->cos(pitch) - z * m->cos(yaw) * m->sin(pitch);
        image->roll = m->atan2(sine, cosine) * 57.2957795f;
    }
    image->vertical_fov = (float)camera.fov * (360.f / 65536.f);
    image->scene_camera = scene_camera;
    if (!s->output(s->context, v->width, v->height, v->depth, &image->rgba, &image->depth))
        return MM_PREVIEW_MEMORY;
    output_pixels(image, v, d, &mesh, &buffers, s, f->targets.camera, width, height, bg, infinite);
    return MM_PREVIEW_OK;
}
int mm_preview_prepare_lights(const MMPreviewLightValues *values, size_t count, int has_background,
                              MMBackgroundLightRig *out, const MMPreviewRenderServices *s)
{
    if (!has_background || count > MM_BACKGROUND_LIGHT_RIG_LIMIT)
        return MM_PREVIEW_LIGHT_RIG_INVALID;
    return mm_background_light_rig_prepare(values, count, s->light_rotation, out) ?
        MM_PREVIEW_OK : MM_PREVIEW_LIGHT_VALUE_INVALID;
}

int mm_preview_render(const MMPreviewView *v, MMPreviewOrbit *orbit, MMPreviewImage *output,
                            const MMPreviewRenderServices *s)
{
    PreviewFrame frame;
    MMPreviewImage image;
    MMPreviewOrbit framing;
    int result, axis;
    memset(output, 0, sizeof(*output));
    if (!mm_preview_ready(s))
        return MM_PREVIEW_NOT_READY;
    if (v->width < 64 || v->height < 64 || v->width > 4096 || v->height > 4096 || !finitef(v->yaw) ||
        !finitef(v->pitch) || !finitef(v->roll) || !finitef(v->vertical_fov) || v->vertical_fov <= 0 ||
        v->vertical_fov >= 179 || !finitef(v->near_clip) || v->near_clip < 0 || v->near_clip > 100 ||
        !finitef(v->zoom) || v->pitch < -90 || v->pitch > 90 || v->zoom < .1f || v->zoom > 10 || v->display < 0 ||
        v->display > 6 || v->axis < 0 || v->axis > 3)
        return MM_PREVIEW_INVALID;
    if (v->free_camera)
        for (axis = 0; axis < 3; ++axis)
            if (!finitef(v->position[axis]) || fabs(v->position[axis]) > 32700)
                return MM_PREVIEW_INVALID;
    memset(&image, 0, sizeof(image));
    if (s->light_override)
    {
        framing = *orbit;
        orbit = &framing;
    }
    frame_init(&frame, s);
    result = render(&frame, v, orbit, &image);
    frame_dispose(&frame);
    if (result == MM_PREVIEW_OK)
        *output = image;
    return result;
}

int mm_preview_render_isolated(const MMPreviewView *view, const MMPreviewOrbit *orbit, MMI32 object,
                               const float bounds[24], int crop, int rect[4], MMPreviewImage *out,
                               const MMPreviewRenderServices *s)
{
    MMPreviewView isolated = *view;
    MMPreviewOrbit framing = *orbit;
    float fit;
    int result;
    if (view->width < 64 || view->height < 64 || !finitef(view->zoom) || view->zoom < .1f || view->zoom > 10)
        return MM_PREVIEW_ISOLATED_INVALID;
    if (!mm_preview_camera_fit_object(bounds, view->yaw, view->pitch, (float)view->width / view->height,
                                     view->vertical_fov, .04f, &fit, &s->math))
        return MM_PREVIEW_ISOLATED_FIT;
    isolated.authored = isolated.stable_orbit = isolated.free_camera = isolated.orthographic = isolated.axis = 0;
    isolated.object = object;
    isolated.roll = 0;
    isolated.near_clip = .01f;
    isolated.fit_depth = 1;
    isolated.zoom = fit * minf(1.f, view->zoom);
    mm_preview_orbit_reset(&framing);
    result = mm_preview_render(&isolated, &framing, out, s);
    if (result == MM_PREVIEW_OK && crop)
        mm_preview_camera_crop(bounds, out->projection.transform, out->projection.vertical_fov,
                               out->projection.aspect, out->width, out->height, rect, &s->math);
    return result;
}
