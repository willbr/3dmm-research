/* Preview background image ownership and two-entry mesh cache. */
#include "mm/background_preview.h"
#include "mm/depth_bitmap.h"
#include "mm/masked_bitmap.h"
#include "mm/movie_session.h"
#include <stdlib.h>
#include <string.h>
#define MINIMUM (-2147483647 - 1)
typedef char depth_sample_must_be_two_bytes[sizeof(unsigned short) == 2 ? 1 : -1];

static int graph_payload(void *context, MMU32 type, MMU32 number, MMReadReceiver receive, void *out, int *result)
{
    return mm_background_graph_payload(((const MMBackgroundPreviewGraph *)context)->graph,
                                       type, number, receive, out, result);
}

static int image_child(void *context, MMU32 camera, int depth, MMU32 *number)
{
    const MMMovieSessionGraph *graph = ((const MMBackgroundPreviewGraph *)context)->graph;
    MMChunkAccess chunks;
    graph->access(graph->context, &chunks);
    return mm_background_camera_image_link(&chunks, camera, depth, number) == MM_CHUNK_OK;
}

static int read_colour(void *context, const MMReadStream *stream)
{
    MMMaskedOwner **out = (MMMaskedOwner **)context;
    *out = mm_masked_owner_read_stream(stream, NULL);
    return *out != NULL;
}

static int read_depth(void *context, const MMReadStream *stream)
{
    MMDepthOwner **out = (MMDepthOwner **)context;
    *out = mm_depth_owner_read_stream(stream, mm_depth_native_big_endian(), NULL);
    return *out != NULL;
}

static void release_bitmap(void *context, void *owner, int depth)
{
    (void)context;
    if (depth)
        mm_depth_owner_release((MMDepthOwner *)owner);
    else
        mm_masked_owner_release((MMMaskedOwner *)owner);
}

static void *fetch_bitmap(void *context, MMU32 number, int depth)
{
    MMMaskedOwner *colour = NULL;
    int decoded;
    if (depth)
    {
        MMDepthOwner *owner = NULL;
        if (graph_payload(context, 0x5a424d50UL, number, read_depth, &owner, &decoded) == MM_CHUNK_OK && decoded)
            return owner;
        mm_depth_owner_release(owner);
        return NULL;
    }
    if (graph_payload(context, 0x4d424d50UL, number, read_colour, &colour, &decoded) == MM_CHUNK_OK && decoded)
        return colour;
    mm_masked_owner_release(colour);
    return NULL;
}

static void bitmap_bounds(void *context, void *owner, MMI32 out[4])
{
    MMMaskedRect rect;
    (void)context;
    mm_masked_bounds(mm_masked_owner_bitmap((const MMMaskedOwner *)owner), mm_masked_native_big_endian(), &rect);
    out[0] = rect.left;
    out[1] = rect.top;
    out[2] = rect.right;
    out[3] = rect.bottom;
}

static void draw_colour(void *context, void *owner, unsigned char *pixels, MMU32 w, MMU32 h, MMI32 x, MMI32 y)
{
    MMMaskedRect rect;
    int big = mm_masked_native_big_endian();
    (void)context;
    mm_masked_bounds(mm_masked_owner_bitmap((const MMMaskedOwner *)owner), big, &rect);
    mm_masked_blit8(mm_masked_owner_bitmap((const MMMaskedOwner *)owner), big, pixels, w, h, rect.left + x, rect.top + y, NULL, NULL);
}

static void draw_depth(void *context, void *owner, unsigned short *pixels, MMU32 w, MMU32 h)
{
    MMDepthRect clip;
    (void)context;
    clip.left = clip.top = 0;
    clip.right = (MMI32)w;
    clip.bottom = (MMI32)h;
    mm_depth_draw(mm_depth_owner_bitmap((const MMDepthOwner *)owner), pixels, w * 2, h, 0, 0, &clip, NULL, 0);
}

void mm_background_preview_bind_graph(MMBackgroundPreviewServices *s, MMBackgroundPreviewGraph *context)
{
    s->context = context;
    s->payload = graph_payload;
    s->child = image_child;
    s->fetch = fetch_bitmap;
    s->release = release_bitmap;
    s->bounds = bitmap_bounds;
    s->draw_rgb = draw_colour;
    s->draw_depth = draw_depth;
}

static int ensure(MMCollectionStorage *s, unsigned section, MMU32 count, MMU32 stride)
{
    MMU32 bytes, capacity;
    if (count >= MM_STORAGE_LIMIT / stride)
        return 0;
    bytes = count * stride;
    capacity = s->buffers[section].capacity;
    if (capacity >= bytes)
        return 1;
    if (capacity < 1024)
        capacity = 1024;
    while (capacity < bytes)
    {
        if (capacity >= MM_STORAGE_LIMIT / 2)
        {
            capacity = bytes;
            break;
        }
        capacity *= 2;
    }
    return mm_storage_resize_exact(s, section, capacity, MM_STORAGE_NORMAL) == MM_STORAGE_OK;
}
int mm_background_preview_init(MMBackgroundPreview *p, MMBackgroundCameraState *placement,
                               const MMStorageAllocator *allocator)
{
    memset(p, 0, sizeof(*p));
    p->placement = placement;
    return mm_storage_init(&p->metadata, allocator) == MM_STORAGE_OK &&
           mm_storage_init(&p->images, allocator) == MM_STORAGE_OK;
}
void mm_background_preview_dispose(MMBackgroundPreview *p, const MMBackgroundPreviewServices *s)
{
    MMU32 i;
    for (i = 0; i < p->mesh_count; ++i)
        s->mesh_release(s->context, p->meshes[i]);
    p->mesh_count = 0;
    p->placement = NULL;
    mm_storage_dispose(&p->images);
    mm_storage_dispose(&p->metadata);
    p->camera_count = p->light_count = p->rgb_count = p->depth_count = p->width = p->height = 0;
}
int mm_background_preview_camera(MMBackgroundPreview *p, MMU32 number)
{
    if (!ensure(&p->metadata, 0, p->camera_count + 1, sizeof(number)))
        return 0;
    ((MMU32 *)p->metadata.buffers[0].data)[p->camera_count++] = number;
    return 1;
}
int mm_background_preview_light(MMBackgroundPreview *p, const MMBackgroundLight *light)
{
    if (!ensure(&p->metadata, 1, p->light_count + 1, sizeof(*light)))
        return 0;
    ((MMBackgroundLight *)p->metadata.buffers[1].data)[p->light_count++] = *light;
    return 1;
}
static int select_images(MMBackgroundPreview *p, MMU32 slot, const MMBackgroundCameraBinding *binding,
                         const MMBackgroundPreviewServices *s)
{
    MMU32 width, height, n;
    MMI32 r[4];
    void *bitmap;
    p->camera = *binding;
    s->publish(s->context, p);
    if (binding->data.near_z <= 0 || binding->data.far_z <= binding->data.near_z)
        return 0;
    if (!mm_background_camera_binding_image(&p->camera, 0, s->child, s->context) ||
        !(bitmap = s->fetch(s->context, p->camera.images.colour, 0)))
        return 0;
    s->bounds(s->context, bitmap, r);
    if (r[2] <= r[0] || r[3] <= r[1])
    {
        s->release(s->context, bitmap, 0);
        return 0;
    }
    width = (MMU32)r[2] - (MMU32)r[0];
    height = (MMU32)r[3] - (MMU32)r[1];
    if (width > 1024U || height > 1024U || r[0] == MINIMUM || r[1] == MINIMUM)
    {
        s->release(s->context, bitmap, 0);
        return 0;
    }
    p->width = width;
    p->height = height;
    n = width * height;
    s->publish(s->context, p);
    if (!ensure(&p->images, 0, n, 1))
    {
        s->release(s->context, bitmap, 0);
        return 0;
    }
    memset(p->images.buffers[0].data, 0, n);
    p->rgb_count = n;
    s->publish(s->context, p);
    s->draw_rgb(s->context, bitmap, p->images.buffers[0].data, width, height, -r[0], -r[1]);
    s->release(s->context, bitmap, 0);
    if (!mm_background_camera_binding_image(&p->camera, 1, s->child, s->context) ||
        !(bitmap = s->fetch(s->context, p->camera.images.depth, 1)))
        return 0;
    if (!ensure(&p->images, 1, n, 2))
    {
        s->release(s->context, bitmap, 1);
        return 0;
    }
    memset(p->images.buffers[1].data, 255, n * 2);
    p->depth_count = n;
    s->publish(s->context, p);
    s->draw_depth(s->context, bitmap, (unsigned short *)p->images.buffers[1].data, width, height);
    s->release(s->context, bitmap, 1);
    p->active = slot;
    s->publish(s->context, p);
    if (s->selected)
        s->selected(s->context, slot);
    return 1;
}
static int stage_selection(MMBackgroundPreview *p, MMU32 slot, const MMBackgroundCameraBinding *binding,
                            const MMReadStream *stream, int update_placement)
{
    int status;
    if (slot >= p->camera_count || binding->number != ((const MMU32 *)p->metadata.buffers[0].data)[slot])
        return 0;
    if (update_placement)
    {
        status = mm_background_placement_stage(p->placement, stream, &binding->data);
        if (status == MM_STORAGE_MEMORY)
            return MM_BACKGROUND_PREVIEW_PLACEMENT_MEMORY;
        if (status != MM_STORAGE_OK)
            return 0;
    }
    return 1;
}

static int finish_selection(MMBackgroundPreview *p, MMU32 slot, const MMBackgroundCameraBinding *binding,
                             int update_placement, const MMBackgroundPreviewServices *s)
{
    if (!select_images(p, slot, binding, s))
        return 0;
    if (update_placement)
        mm_background_placement_commit(p->placement, (MMI32)slot, &p->camera, 1);
    return 1;
}

int mm_background_preview_apply(MMBackgroundPreview *p, MMU32 slot, const MMBackgroundCameraBinding *binding,
                                 const MMReadStream *stream, int update_placement, const MMBackgroundPreviewServices *s)
{
    int result = stage_selection(p, slot, binding, stream, update_placement);
    return result > 0 ? finish_selection(p, slot, binding, update_placement, s) : result;
}

typedef struct GraphSelection
{
    MMBackgroundPreview *preview;
    MMBackgroundCameraBinding binding;
    MMU32 slot;
    int update_placement;
} GraphSelection;

static int capture_selection(void *context, const MMBackgroundCameraBinding *binding, const MMReadStream *stream)
{
    GraphSelection *selection = (GraphSelection *)context;
    int result = stage_selection(selection->preview, selection->slot, binding, stream, selection->update_placement);
    if (result > 0)
        selection->binding = *binding;
    return result == MM_BACKGROUND_PREVIEW_PLACEMENT_MEMORY ? MM_MOVIE_MEMORY :
               result > 0 ? MM_MOVIE_OK : MM_MOVIE_BAD_FILE;
}

int mm_background_preview_select_graph(MMBackgroundPreview *p, const MMMovieSessionGraph *graph, MMU32 background,
                                        MMU32 slot, int update_placement, const MMBackgroundPreviewServices *s)
{
    GraphSelection selection;
    int result;
    selection.preview = p;
    selection.slot = slot;
    selection.update_placement = update_placement;
    result = mm_movie_camera_graph(graph, background, slot, capture_selection, &selection, NULL);
    if (result != MM_MOVIE_OK)
        return result == MM_MOVIE_MEMORY ? MM_BACKGROUND_PREVIEW_PLACEMENT_MEMORY : 0;
    return finish_selection(p, slot, &selection.binding, update_placement, s);
}
typedef struct CameraSelection
{
    MMBackgroundPreview *preview;
    const MMBackgroundPreviewServices *services;
    MMU32 slot, number;
    int update_placement;
} CameraSelection;

static int apply_camera_payload(void *context, const MMReadStream *stream)
{
    CameraSelection *selection = (CameraSelection *)context;
    MMBackgroundCameraBinding binding;
    if (!mm_background_camera_binding_read(stream, selection->number, &binding))
        return 0;
    return mm_background_preview_apply(selection->preview, selection->slot, &binding, stream,
                                       selection->update_placement, selection->services);
}
static int select_camera(MMBackgroundPreview *p, MMU32 slot, int update_placement,
                         const MMBackgroundPreviewServices *s)
{
    CameraSelection selection;
    int result;
    if (slot >= p->camera_count)
        return 0;
    selection.preview = p;
    selection.services = s;
    selection.slot = slot;
    selection.number = ((const MMU32 *)p->metadata.buffers[0].data)[slot];
    selection.update_placement = update_placement;
    if (s->payload(s->context, MM_BACKGROUND_CAMERA, selection.number, apply_camera_payload, &selection, &result) !=
        MM_CHUNK_OK)
        return 0;
    return result;
}
int mm_background_preview_select(MMBackgroundPreview *p, MMU32 slot, const MMBackgroundPreviewServices *s)
{
    return select_camera(p, slot, 0, s);
}
int mm_background_preview_select_placement(MMBackgroundPreview *p, MMU32 slot, const MMBackgroundPreviewServices *s)
{
    return select_camera(p, slot, 1, s);
}
int mm_background_preview_place(MMBackgroundPreview *p, MMActorPoint *out)
{
    if (!out || !p->placement || p->placement->current < 0)
        return 0;
    mm_background_place(p->placement, out);
    return 1;
}
void mm_background_preview_reuse_place(MMBackgroundPreview *p)
{
    if (p->placement)
        mm_background_reuse_place(p->placement);
}
void *mm_background_preview_mesh(MMBackgroundPreview *p, const MMBackgroundPreviewServices *s)
{
    const MMBackgroundCameraData *camera = &p->camera.data;
    MMU32 i, row, axis, key;
    MMI32 native_projection[16], aspect;
    double transform[16], projection[16];
    void *mesh;
    for (i = 0; i < p->mesh_count; ++i)
        if (p->keys[i] == p->active)
        {
            mesh = p->meshes[i];
            key = p->keys[i];
            if (i + 1 < p->mesh_count)
            {
                p->meshes[i] = p->meshes[i + 1];
                p->keys[i] = p->keys[i + 1];
            }
            p->meshes[p->mesh_count - 1] = mesh;
            p->keys[p->mesh_count - 1] = key;
            return mesh;
        }
    mesh = s->mesh_new(s->context);
    if (!mesh)
        return NULL;
    memset(transform, 0, sizeof(transform));
    transform[15] = 1;
    for (row = 0; row < 4; ++row)
        for (axis = 0; axis < 3; ++axis)
            transform[row * 4 + axis] = s->scalar_float(s->context, camera->matrix.m[row][axis]);
    if (!p->height || camera->near_z <= 0 || camera->far_z <= camera->near_z)
    {
        s->mesh_release(s->context, mesh);
        return NULL;
    }
    aspect = s->float_scalar(s->context, (float)p->width / p->height);
    s->perspective(s->context, camera->fov, aspect, -camera->near_z, -camera->far_z, native_projection);
    for (i = 0; i < 16; ++i)
        projection[i] = s->scalar_float(s->context, native_projection[i]);
    if (!s->mesh_build(s->context, mesh, p, transform, projection))
    {
        s->mesh_release(s->context, mesh);
        return NULL;
    }
    if (p->mesh_count == 2)
    {
        s->mesh_release(s->context, p->meshes[0]);
        p->meshes[0] = p->meshes[1];
        p->keys[0] = p->keys[1];
        --p->mesh_count;
    }
    p->meshes[p->mesh_count] = mesh;
    p->keys[p->mesh_count++] = p->active;
    return mesh;
}
