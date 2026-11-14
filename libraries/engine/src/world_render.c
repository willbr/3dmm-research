/* World rendering control converted from bren/bwld.cpp. */
#include "three_mm/world_render.h"
#include <string.h>
#define MAXIMUM 2147483647
#define MINIMUM (-2147483647 - 1)
#define WORK THREE_MM_WORLD_DIRTY_WORKING
#define SCREEN THREE_MM_WORLD_DIRTY_SCREEN
typedef struct MaterialContext
{
    ThreeMMWorldMaterials *scope;
    const ThreeMMWorldMaterialServices *services;
} MaterialContext;
static int apply_material(void *argument, void *object)
{
    MaterialContext *c = (MaterialContext *)argument;
    ThreeMMWorldMaterials *m = c->scope;
    const ThreeMMWorldMaterialServices *s = c->services;
    ThreeMMWorldMaterial entry;
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
void three_mm_world_materials_end(ThreeMMWorldMaterials *m, const ThreeMMWorldMaterialServices *s)
{
    size_t i;
    for (i = 0; i < m->count; ++i)
        s->write(s->context, &m->saved[i]);
    if (m->saved)
        s->free(s->context, m->saved);
    m->saved = NULL;
    m->count = m->capacity = 0;
}
int three_mm_world_materials_begin(ThreeMMWorldMaterials *m, int flat, int no_texture,
                                   const ThreeMMWorldMaterialServices *s)
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
        m->saved = (ThreeMMWorldMaterial *)s->allocate(s->context, m->capacity * sizeof(*m->saved));
        if (!m->saved)
            return 0;
    }
    c.scope = m;
    c.services = s;
    s->enumerate(s->context, apply_material, &c);
    if (!m->failed)
        return 1;
    three_mm_world_materials_end(m, s);
    return 0;
}
void three_mm_world_render_init(ThreeMMWorldRender *r)
{
    r->changed = 0;
}
void three_mm_world_render_dirty(ThreeMMWorldRender *r)
{
    r->changed = 1;
}
void three_mm_world_render_styles(void *root, int style, const ThreeMMWorldRenderServices *s)
{
    void *actor;
    int old;
    for (actor = s->child(s->context, root); actor; actor = s->next(s->context, actor))
    {
        old = s->style(s->context, actor);
        if (s->type(s->context, actor) == s->actor_model && old != s->bounding_points && old != s->bounding_edges &&
            old != s->bounding_faces)
            s->set_style(s->context, actor, style);
        three_mm_world_render_styles(actor, style, s);
    }
}
static int valid_pixels(const ThreeMMWorldPixels *p, const ThreeMMWorldBuffers *b)
{
    return p->pixels && p->row >= b->buffer_width && p->row > 0 && b->buffer_height > 0 &&
           (size_t)b->buffer_height <= (size_t)-1 / (size_t)p->row &&
           p->bytes >= (size_t)p->row * (size_t)b->buffer_height;
}
int three_mm_world_render_clean(const ThreeMMWorldBuffers *b, const ThreeMMWorldRenderServices *s)
{
    ThreeMMI32 rect[4], y, left, right;
    ThreeMMWorldPixels source, destination;
    int ok = 1;
    if (!s->region_bounds(s->context, WORK, rect))
        return 1;
    if (rect[0] < 0)
        rect[0] = 0;
    if (rect[1] < 0)
        rect[1] = 0;
    if (rect[2] > b->buffer_width)
        rect[2] = b->buffer_width;
    if (rect[3] > b->buffer_height)
        rect[3] = b->buffer_height;
    if (rect[0] >= rect[2] || rect[1] >= rect[3])
        return 1;
    s->depth_clean(s->context, rect);
    s->scan_init(s->context, rect);
    if (!s->lock(s->context, THREE_MM_WORLD_RGB_BACKGROUND, &source))
    {
        s->scan_end(s->context);
        return 0;
    }
    if (!s->lock(s->context, THREE_MM_WORLD_RGB_WORKING, &destination))
    {
        s->unlock(s->context, THREE_MM_WORLD_RGB_BACKGROUND);
        s->scan_end(s->context);
        return 0;
    }
    if (!valid_pixels(&source, b) || !valid_pixels(&destination, b))
        ok = 0;
    for (y = rect[1]; ok && y < rect[3]; ++y)
    {
        while (s->scan_span(s->context, &left, &right))
        {
            if (left < 0 || right < left || right > rect[2] - rect[0])
            {
                ok = 0;
                break;
            }
            memcpy(destination.pixels + (size_t)y * destination.row + rect[0] + left,
                   source.pixels + (size_t)y * source.row + rect[0] + left, (size_t)(right - left));
        }
        if (ok)
            s->scan_next(s->context);
    }
    s->unlock(s->context, THREE_MM_WORLD_RGB_WORKING);
    s->unlock(s->context, THREE_MM_WORLD_RGB_BACKGROUND);
    s->scan_end(s->context);
    return ok;
}
int three_mm_world_render(ThreeMMWorldRender *r, const ThreeMMWorldBuffers *b, const ThreeMMWorldRenderServices *s)
{
    void *actor;
    ThreeMMI32 rect[4];
    ThreeMMWorldMaterials materials;
    int wire, flat, no_texture, i;
    if (!r->changed)
        return 1;
    if (s->has(s->context, THREE_MM_WORLD_BEGIN))
        for (actor = s->child(s->context, s->root); actor; actor = s->next(s->context, actor))
            if (s->type(s->context, actor) == s->actor_none)
                s->begin(s->context, actor);
    if (!three_mm_world_render_clean(b, s))
        return 0;
    s->region_union(s->context, SCREEN, WORK);
    s->region_clear(s->context, WORK);
    s->options(s->context, &wire, &flat, &no_texture);
    three_mm_world_render_styles(s->root, wire ? s->edges : s->faces, s);
    if (!three_mm_world_materials_begin(&materials, flat, no_texture, &s->materials))
        return 0;
    s->render(s->context);
    three_mm_world_materials_end(&materials, &s->materials);
    for (actor = s->child(s->context, s->root); actor; actor = s->next(s->context, actor))
        if (s->type(s->context, actor) == s->actor_none && s->has(s->context, THREE_MM_WORLD_RECT))
        {
            s->rect(s->context, actor, rect);
            if (b->scale > 1)
                for (i = 0; i < 4; ++i)
                {
                    if (rect[i] > MAXIMUM / b->scale || rect[i] < MINIMUM / b->scale)
                        return 0;
                    rect[i] *= b->scale;
                }
            s->region_add(s->context, WORK, rect);
        }
    s->region_union(s->context, SCREEN, WORK);
    r->changed = 0;
    s->publish(s->context, r);
    return 1;
}
int three_mm_world_prerender(ThreeMMWorldRender *r, const ThreeMMWorldBuffers *b, const ThreeMMWorldRenderServices *s)
{
    if (!three_mm_world_render(r, b, s))
        return 0;
    s->snapshot_depth(s->context);
    s->detach_depth(s->context);
    s->snapshot_rgb(s->context);
    s->flush(s->context);
    return 1;
}
void three_mm_world_unprerender(const ThreeMMWorldRenderServices *s)
{
    s->restore_background(s->context);
}
void three_mm_world_render_mark(const ThreeMMWorldBuffers *b, ThreeMMI32 x, ThreeMMI32 y,
                                const ThreeMMWorldRenderServices *s)
{
    if (b->scale > 1)
        s->screen_scale(s->context, 1, b->scale, 1, b->scale);
    else
        s->screen_scale(s->context, b->half_x ? 2 : 1, 1, b->half_y ? 2 : 1, 1);
    s->screen_offset(s->context, x, y);
    s->screen_mark(s->context);
    s->region_clear(s->context, SCREEN);
}
int three_mm_world_render_draw(const ThreeMMWorldBuffers *b, ThreeMMI32 x, ThreeMMI32 y,
                               const ThreeMMWorldRenderServices *s)
{
    ThreeMMI32 source[4], destination[4];
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
