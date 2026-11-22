/* World rendering control converted from bren/bwld.cpp. */
#include "mm/world_render.h"
#include <string.h>
#define MAXIMUM 2147483647
#define MINIMUM (-2147483647 - 1)
typedef struct MaterialContext
{
    MMWorldMaterials *scope;
    const MMWorldMaterialServices *services;
} MaterialContext;
static int apply_material(void *argument, void *object)
{
    MaterialContext *c = (MaterialContext *)argument;
    MMWorldMaterials *m = c->scope;
    const MMWorldMaterialServices *s = c->services;
    MMWorldMaterial entry;
    int remove_texture, flatten;
    s->read(s->context, object, &entry);
    entry.object = object;
    remove_texture = m->no_texture && entry.texture;
    flatten = m->flat && (!entry.texture || remove_texture) && (entry.flags & s->gouraud);
    if (remove_texture || flatten)
    {
        if (m->count == m->capacity)
        {
            m->failed = 1;
            return 1;
        }
        m->saved[m->count++] = entry;
        if (remove_texture)
        {
            entry.texture = NULL;
            /* Texture lighting rows are not palette indices. Use the retail
             * default solid material's grey ramp, entries 15 through 30. */
            entry.base = entry.range = 15;
        }
        if (flatten)
            entry.flags &= ~s->gouraud;
        s->write(s->context, &entry);
    }
    return 0;
}
void mm_world_materials_end(MMWorldMaterials *m, const MMWorldMaterialServices *s)
{
    size_t i;
    for (i = 0; i < m->count; ++i)
        s->write(s->context, &m->saved[i]);
    if (m->saved)
        s->free(s->context, m->saved);
    m->saved = NULL;
    m->count = m->capacity = 0;
}
int mm_world_materials_begin(MMWorldMaterials *m, int flat, int no_texture,
                                   const MMWorldMaterialServices *s)
{
    MaterialContext c;
    memset(m, 0, sizeof(*m));
    m->flat = flat;
    m->no_texture = no_texture;
    if (!flat && !no_texture)
        return 1;
    m->capacity = s->count(s->context);
    if (m->capacity > (size_t)-1 / sizeof(*m->saved))
        return 0;
    if (m->capacity)
    {
        m->saved = (MMWorldMaterial *)s->allocate(s->context, m->capacity * sizeof(*m->saved));
        if (!m->saved)
            return 0;
    }
    c.scope = m;
    c.services = s;
    s->enumerate(s->context, apply_material, &c);
    if (!m->failed)
        return 1;
    mm_world_materials_end(m, s);
    return 0;
}
void mm_world_render_init(MMWorldRender *r)
{
    r->changed = 0;
}
void mm_world_render_dirty(MMWorldRender *r)
{
    r->changed = 1;
}
void mm_world_render_styles(void *root, int style, const MMWorldRenderServices *s)
{
    void *actor;
    int old;
    for (actor = s->child(s->context, root); actor; actor = s->next(s->context, actor))
    {
        old = s->style(s->context, actor);
        if (s->type(s->context, actor) == s->actor_model && old != s->bounding_points && old != s->bounding_edges &&
            old != s->bounding_faces)
            s->set_style(s->context, actor, style);
        mm_world_render_styles(actor, style, s);
    }
}
int mm_world_render_clean(const MMWorldBuffers *b, const MMWorldBufferServices *buffers)
{
    MMPixelRect rect = b->dirty_working.bounds;
    MMPixelRect viewport;
    MMRegionCursor cursor = {0};
    MMI32 y, left, right;
    MMWorldPixels source, destination;
    const MMDepthBitmap *depth;
    void *in = b->owners[MM_WORLD_RGB_BACKGROUND], *out = b->owners[MM_WORLD_RGB_WORKING];
    int ok = 0;
    viewport.left = viewport.top = 0;
    viewport.right = b->buffer_width;
    viewport.bottom = b->buffer_height;
    mm_pixel_rect_intersect(&rect, &viewport);
    if (rect.left >= rect.right || rect.top >= rect.bottom)
        return 1;
    depth = buffers->depth_bitmap(buffers->context, b->owners[MM_WORLD_DEPTH_BACKGROUND]);
    if (!depth)
        return 0;
    mm_depth_draw(depth, b->z.pixels, b->z.row, b->z.height, 0, 0, &rect, &b->dirty_working, 0);
    mm_region_cursor_open(&cursor, &b->dirty_working, &rect);
    if (!buffers->lock(buffers->context, in, &source))
        goto done;
    if (!buffers->lock(buffers->context, out, &destination))
        goto unlock_source;
    ok = mm_world_pixels_valid(&source, b->buffer_width, b->buffer_height) &&
         mm_world_pixels_valid(&destination, b->buffer_width, b->buffer_height);
    for (y = rect.top; ok && y < rect.bottom; ++y)
    {
        while (mm_region_scan_pair(&cursor.scan, &left, &right))
        {
            if (left < 0 || right < left || right > rect.right - rect.left)
            {
                ok = 0;
                break;
            }
            memcpy(destination.pixels + (size_t)y * destination.row + rect.left + left,
                   source.pixels + (size_t)y * source.row + rect.left + left, (size_t)(right - left));
        }
        if (ok)
            mm_region_scan_advance(&cursor.scan, 1);
    }
    buffers->unlock(buffers->context, out);
unlock_source:
    buffers->unlock(buffers->context, in);
done:
    mm_region_cursor_close(&cursor);
    return ok;
}
int mm_world_render(MMWorldRender *r, MMWorldBuffers *b, const MMWorldRenderServices *s,
                    const MMWorldBufferServices *buffers)
{
    void *actor;
    MMI32 rect[4];
    MMPixelRect damage;
    const MMRenderBody *bodies;
    MMRenderBody body;
    MMRenderFrame frame;
    MMMovieRenderScene scene;
    MMWorldMaterials materials;
    MMU32 index;
    int wire, flat, no_texture, i, ok = 0;
    if (!r->changed)
        return 1;
    frame = s->frame;
    mm_movie_render_scene_init(&scene, s->movie, NULL, s->context, s->retain_body, s->release_body);
    if (s->body)
        for (actor = s->child(s->context, frame.world); actor; actor = s->next(s->context, actor))
            if (s->type(s->context, actor) == s->actor_none)
            {
                memset(&body, 0, sizeof(body));
                if (s->body(s->context, actor, &body) && !mm_movie_render_scene_add(&scene, &body))
                    goto done;
            }
    mm_movie_render_scene_publish(&scene, &s->camera, s->position, &frame);
    if (s->has(s->context, MM_WORLD_BEGIN))
        for (actor = s->child(s->context, frame.world); actor; actor = s->next(s->context, actor))
            if (s->type(s->context, actor) == s->actor_none)
                s->begin(s->context, actor);
    bodies = (const MMRenderBody *)frame.scene->storage.buffers[0].data;
    if (s->prepare_body)
        for (index = 0; index < frame.scene->bodies.count; ++index)
            s->prepare_body(s->context, &bodies[index]);
    if (!mm_world_render_clean(b, buffers))
        goto done;
    mm_region_apply(&b->dirty_screen, MM_REGION_UNION, &b->dirty_working, NULL, s->region_allocator);
    mm_region_set_rect(&b->dirty_working, NULL);
    s->options(s->context, &wire, &flat, &no_texture);
    mm_world_render_styles(frame.world, wire ? s->edges : s->faces, s);
    if (!mm_world_materials_begin(&materials, flat, no_texture, &s->materials))
        goto done;
    s->render(s->context, &frame);
    mm_world_materials_end(&materials, &s->materials);
    for (actor = s->child(s->context, frame.world); actor; actor = s->next(s->context, actor))
        if (s->type(s->context, actor) == s->actor_none && s->has(s->context, MM_WORLD_RECT))
        {
            s->rect(s->context, actor, rect);
            if (b->scale > 1)
                for (i = 0; i < 4; ++i)
                {
                    if (rect[i] > MAXIMUM / b->scale || rect[i] < MINIMUM / b->scale)
                        goto done;
                    rect[i] *= b->scale;
                }
            damage.left = rect[0];
            damage.top = rect[1];
            damage.right = rect[2];
            damage.bottom = rect[3];
            mm_region_apply_rect(&b->dirty_working, MM_REGION_UNION, &damage, NULL, 0, s->region_allocator);
        }
    mm_region_apply(&b->dirty_screen, MM_REGION_UNION, &b->dirty_working, NULL, s->region_allocator);
    r->changed = 0;
    ok = 1;
done:
    mm_movie_render_scene_dispose(&scene);
    return ok;
}
int mm_world_prerender(MMWorldRender *r, MMWorldBuffers *b, const MMWorldRenderServices *s,
                        const MMWorldBufferServices *buffers)
{
    void *target = b->owners[MM_WORLD_DEPTH_BACKGROUND];
    const MMDepthBitmap *src, *dst;
    int copied;
    if (!mm_world_render(r, b, s, buffers))
        return 0;
    src = buffers->depth_bitmap(buffers->context, b->owners[MM_WORLD_DEPTH_WORKING]);
    dst = buffers->depth_bitmap(buffers->context, target);
    copied = mm_world_draw_depth(src, dst, b->buffer_width, b->buffer_height);
    if (copied)
    {
        s->detach_depth(s->context, target);
        copied = mm_world_copy_rgb(buffers, b->owners[MM_WORLD_RGB_WORKING], b->buffer_width, b->buffer_height,
                                  b->owners[MM_WORLD_RGB_BACKGROUND], b->buffer_width, b->buffer_height);
    }
    s->flush(s->context);
    return copied;
}
void mm_world_render_mark(MMWorldBuffers *b, MMI32 x, MMI32 y, const MMWorldRenderServices *s)
{
    MMRegion *screen = &b->dirty_screen;
    if (b->scale > 1)
        mm_region_scale(screen, 1, b->scale, 1, b->scale);
    else
        mm_region_scale(screen, b->half_x ? 2 : 1, 1, b->half_y ? 2 : 1, 1);
    mm_region_offset(screen, x, y);
    s->screen_mark(s->context, screen);
    mm_region_set_rect(screen, NULL);
}
int mm_world_render_draw(const MMWorldBuffers *b, MMI32 x, MMI32 y,
                               const MMWorldRenderServices *s)
{
    MMI32 source[4], destination[4];
    if (x == MINIMUM || y == MINIMUM || (x < 0 && b->width > MAXIMUM + x) || (y < 0 && b->height > MAXIMUM + y))
        return 0;
    source[0] = source[1] = 0;
    source[2] = b->buffer_width;
    source[3] = b->buffer_height;
    destination[0] = -x;
    destination[1] = -y;
    destination[2] = b->width - x;
    destination[3] = b->height - y;
    s->draw(s->context, source, destination);
    return 1;
}
