#include "mm/world_buffers.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Object
{
    int kind, width, height, row, bytes, refs, locks;
    unsigned char pixels[512];
} Object;
typedef struct Fixture
{
    MMWorldBufferServices s;
    MMWorldBuffers *world;
    int phase, fail, objects, locks, setups, changed, priorities, short_depth;
} Fixture;
static int fail(Fixture *f)
{
    return ++f->phase == f->fail;
}
static Object *object(Fixture *f, int kind, int w, int h)
{
    Object *o = (Object *)calloc(1, sizeof(*o));
    assert(o);
    o->kind = kind;
    o->width = w;
    o->height = h;
    o->row = w * ((kind == 0 || kind == 1) ? 2 : 1);
    o->bytes = o->row * h;
    assert(o->bytes <= 512);
    o->refs = 1;
    memset(o->pixels, kind < 2 ? 255 : 0, (size_t)o->bytes);
    ++f->objects;
    return o;
}
static void *create(void *p, int kind, MMI32 w, MMI32 h)
{
    Fixture *f = (Fixture *)p;
    if (fail(f))
        return NULL;
    return object(f, kind, w, h);
}
static void retain(void *p, void *v)
{
    (void)p;
    assert(v);
    ++((Object *)v)->refs;
}
static void release(void *p, void *v)
{
    Fixture *f = (Fixture *)p;
    Object *o = (Object *)v;
    assert(o && o->refs > 0);
    if (!--o->refs)
    {
        assert(!o->locks);
        --f->objects;
        free(o);
    }
}
static void *lock(void *p, void *v)
{
    Fixture *f = (Fixture *)p;
    Object *o = (Object *)v;
    ++o->locks;
    ++f->locks;
    return o->pixels;
}
static void unlock(void *p, void *v)
{
    Fixture *f = (Fixture *)p;
    Object *o = (Object *)v;
    assert(o->locks > 0);
    --o->locks;
    --f->locks;
}
static void span(void *p, void *v, MMWorldDepthSpan *out)
{
    Object *o = (Object *)v;
    (void)p;
    out->pixels = o->pixels;
    out->row = o->row;
    out->bytes = o->bytes;
}
static void scene(void *p)
{
    Fixture *f = (Fixture *)p;
    assert(f->world->rgb.pixels && f->world->z.pixels && f->locks == 1);
    assert(mm_region_empty(&f->world->dirty_working) && mm_region_empty(&f->world->dirty_screen));
    ++f->setups;
}
static void changed(void *p)
{
    Fixture *f = (Fixture *)p;
    assert(f->world->dirty_working.bounds.right == f->world->buffer_width);
    ++f->changed;
}
static void *fetch(void *p, void *cache, int kind, MMU32 type, MMU32 number)
{
    Fixture *f = (Fixture *)p;
    Object *o;
    int i;
    assert(((Object *)cache)->refs > 0);
    assert(type == (kind == 0 ? 33U : 11U) && number == (kind == 0 ? 44U : 22U));
    if (fail(f))
        return NULL;
    o = object(f, kind, 4, kind == MM_WORLD_DEPTH_BACKGROUND && f->short_depth ? 3 : 4);
    for (i = 0; i < o->bytes; ++i)
        o->pixels[i] = (unsigned char)(i * 7 + 1);
    return o;
}
static void priority(void *p, void *o)
{
    assert(o);
    ++((Fixture *)p)->priorities;
}
static void bitmap(void *p, void *port, void *image)
{
    Object *o = (Object *)port;
    (void)p;
    assert(image);
    memset(o->pixels, 0x31, (size_t)o->bytes);
}
static void copy(void *p, void *sv, void *dv, MMI32 sw, MMI32 sh, MMI32 dw, MMI32 dh)
{
    Object *s = (Object *)sv, *d = (Object *)dv;
    int x, y;
    (void)p;
    assert(s->width == sw && s->height == sh && d->width == dw && d->height == dh);
    for (y = 0; y < dh; ++y)
        for (x = 0; x < dw; ++x)
            d->pixels[y * d->row + x] = s->pixels[(y * sh / dh) * s->row + x * sw / dw];
}
static void depth(void *p, void *sv, void *dv, MMI32 w, MMI32 h)
{
    Object *s = (Object *)sv, *d = (Object *)dv;
    (void)p;
    assert(s->width == w && s->height == h && d->width == w && d->height == h);
    memcpy(d->pixels, s->pixels, (size_t)s->bytes);
}
static void setup(Fixture *f, MMWorldBuffers *w)
{
    memset(f, 0, sizeof(*f));
    memset(w, 0, sizeof(*w));
    f->world = w;
    f->s.context = f;
    f->s.rgb_type = 8;
    f->s.depth_type = 16;
    f->s.create = create;
    f->s.retain_cache = retain;
    f->s.release_cache = release;
    f->s.release = release;
    f->s.release_depth = release;
    f->s.release_bitmap = release;
    f->s.lock = lock;
    f->s.unlock = unlock;
    f->s.depth_span = span;
    f->s.setup_scene = scene;
    f->s.changed = changed;
    f->s.fetch = fetch;
    f->s.priority = priority;
    f->s.priority_depth = priority;
    f->s.draw_bitmap = bitmap;
    f->s.copy_rgb = copy;
    f->s.draw_depth = depth;
}
static void balanced(Fixture *f)
{
    assert(!f->objects && !f->locks);
}
static void initialization(void)
{
    Fixture f;
    MMWorldBuffers w;
    int hx, hy, scale, n, ok;
    for (hx = 0; hx < 2; ++hx)
        for (hy = 0; hy < 2; ++hy)
            for (scale = 1; scale <= 4; scale *= 2)
                for (n = 0; n < 9; ++n)
                {
                    setup(&f, &w);
                    f.fail = n;
                    ok = mm_world_buffers_init(&w, 4, 4, scale, hx, hy, &f.s);
                    assert(ok == (n == 0 || n > 4 + (scale == 1 && !hx && hy)));
                    if (ok)
                    {
                        assert(w.buffer_width == 4 * scale / (scale == 1 && hx ? 2 : 1));
                        assert(w.buffer_height == 4 * scale / (scale == 1 && hy ? 2 : 1));
                        assert(w.rgb.row == w.buffer_width && w.z.row == w.buffer_width * 2 && f.locks == 1 &&
                               f.setups == 1);
                        assert((w.owners[4] != NULL) == (scale == 1 && !hx && hy));
                    }
                    mm_world_buffers_dispose(&w, &f.s);
                    balanced(&f);
                }
    setup(&f, &w);
    assert(!mm_world_buffers_init(&w, 8192, 4, 1, 0, 0, &f.s) && !f.phase);
    mm_world_buffers_dispose(&w, &f.s);
    balanced(&f);
}
static void check_depth(const MMWorldBuffers *w)
{
    Object *o = (Object *)w->owners[0];
    int x, y, sx, sy;
    for (y = 0; y < o->height; ++y)
        for (x = 0; x < o->width; ++x)
        {
            sy = w->scale > 1 ? y / w->scale : y * (w->half_y ? 2 : 1);
            sx = w->scale > 1 ? x / w->scale : x * (w->half_x ? 2 : 1);
            assert(o->pixels[y * o->row + x * 2] == (unsigned char)((sy * 8 + sx * 2) * 7 + 1));
            assert(o->pixels[y * o->row + x * 2 + 1] == (unsigned char)((sy * 8 + sx * 2 + 1) * 7 + 1));
        }
}
static void modes(void)
{
    Fixture f;
    MMWorldBuffers w, old;
    Object *cache;
    const MMPixelRect viewport = {0, 0, 4, 4}, hole = {1, 1, 3, 3};
    int target, n, ok, count, changed;
    unsigned i;
    for (target = 0; target < 5; ++target)
        for (n = 1; n < 13; ++n)
        {
            setup(&f, &w);
            assert(mm_world_buffers_init(&w, 4, 4, 1, 0, 0, &f.s));
            cache = object(&f, MM_WORLD_CACHE, 0, 0);
            assert(mm_world_buffers_background(&w, cache, 11, 22, 33, 44, &f.s));
            check_depth(&w);
            mm_region_set_rect(&w.dirty_screen, &viewport);
            assert(mm_region_apply_rect(&w.dirty_working, MM_REGION_DIFFERENCE, &hole, NULL, 0, NULL) ==
                   MM_REGION_UPDATED);
            assert(mm_region_apply_rect(&w.dirty_screen, MM_REGION_DIFFERENCE, &hole, NULL, 0, NULL) ==
                   MM_REGION_UPDATED);
            old = w;
            count = f.objects;
            changed = f.changed;
            f.fail = f.phase + n;
            ok = mm_world_buffers_mode(&w, target < 3 ? (target != 1) : 0, target < 3 ? (target != 0) : 0,
                                             target < 3    ? 1
                                             : target == 3 ? 2
                                                           : 4,
                                             &f.s);
            if (!ok)
            {
                for (i = 0; i < MM_WORLD_OWNERS; ++i)
                    assert(w.owners[i] == old.owners[i]);
                assert(w.rgb.pixels == old.rgb.pixels && w.z.pixels == old.z.pixels && w.scale == old.scale &&
                       w.half_x == old.half_x && w.half_y == old.half_y && w.buffer_width == old.buffer_width &&
                       w.buffer_height == old.buffer_height && f.objects == count && f.changed == changed);
                assert(w.dirty_working.bands == old.dirty_working.bands && w.dirty_screen.bands == old.dirty_screen.bands);
            }
            else
            {
                check_depth(&w);
                assert(f.changed == changed + 2 && w.dirty_screen.bounds.right == w.buffer_width);
                assert(!w.dirty_working.bands && !w.dirty_screen.bands);
            }
            assert(cache->refs == 2 && f.locks == 1);
            mm_world_buffers_dispose(&w, &f.s);
            assert(cache->refs == 1);
            release(&f, cache);
            balanced(&f);
        }
}
static void noops(void)
{
    Fixture f;
    MMWorldBuffers w;
    int phase;
    setup(&f, &w);
    assert(mm_world_buffers_init(&w, 4, 4, 1, 0, 0, &f.s));
    phase = f.phase;
    assert(mm_world_buffers_mode(&w, 0, 0, 1, &f.s) && f.phase == phase);
    assert(!mm_world_buffers_mode(&w, 0, 0, 3, &f.s));
    assert(!mm_world_buffers_resolution(&w, 2, 1, &f.s));
    assert(!mm_world_buffers_restore_background(&w, &f.s));
    assert(f.phase == phase && !f.changed);
    mm_world_buffers_dispose(&w, &f.s);
    balanced(&f);
}
static void restore_background(void)
{
    Fixture f;
    MMWorldBuffers w;
    Object *cache, *rgb;
    setup(&f, &w);
    assert(mm_world_buffers_init(&w, 4, 4, 1, 0, 0, &f.s));
    cache = object(&f, MM_WORLD_CACHE, 0, 0);
    assert(mm_world_buffers_background(&w, cache, 11, 22, 33, 44, &f.s));
    rgb = (Object *)w.owners[MM_WORLD_RGB_BACKGROUND];
    rgb->pixels[0] = 0x99;
    f.fail = f.phase + 2;
    assert(!mm_world_buffers_restore_background(&w, &f.s));
    assert(cache->refs == 2 && w.owners[MM_WORLD_CACHE] == cache && rgb->pixels[0] == 0x99 && f.changed == 1);
    assert(mm_world_buffers_restore_background(&w, &f.s));
    assert(cache->refs == 2 && w.owners[MM_WORLD_CACHE] == cache && rgb->pixels[0] == 0x31 && f.changed == 2);
    mm_world_buffers_dispose(&w, &f.s);
    release(&f, cache);
    balanced(&f);
}
static void partial_background(void)
{
    Fixture f;
    MMWorldBuffers w;
    Object *cache, *bg;
    int count;
    setup(&f, &w);
    assert(mm_world_buffers_init(&w, 4, 4, 2, 0, 0, &f.s));
    cache = object(&f, MM_WORLD_CACHE, 0, 0);
    count = f.objects;
    f.fail = f.phase + 4;
    assert(!mm_world_buffers_background(&w, cache, 11, 22, 33, 44, &f.s));
    assert(!w.owners[MM_WORLD_CACHE] && cache->refs == 1 && f.objects == count && !f.changed);
    bg = (Object *)w.owners[2];
    assert(bg->pixels[0] == 0x31 && bg->pixels[bg->bytes - 1] == 0x31);
    mm_world_buffers_dispose(&w, &f.s);
    release(&f, cache);
    balanced(&f);
    setup(&f, &w);
    assert(mm_world_buffers_init(&w, 4, 4, 1, 1, 1, &f.s));
    cache = object(&f, MM_WORLD_CACHE, 0, 0);
    count = f.objects;
    f.fail = f.phase + 3;
    assert(!mm_world_buffers_background(&w, cache, 11, 22, 33, 44, &f.s) && f.objects == count && !w.owners[MM_WORLD_CACHE] &&
           !f.changed);
    mm_world_buffers_dispose(&w, &f.s);
    release(&f, cache);
    balanced(&f);
}
static void short_depth(void)
{
    Fixture f;
    MMWorldBuffers w;
    Object *cache, *bg;
    int count, x, y;
    setup(&f, &w);
    assert(mm_world_buffers_init(&w, 4, 4, 1, 1, 0, &f.s));
    cache = object(&f, MM_WORLD_CACHE, 0, 0);
    count = f.objects;
    f.short_depth = 1;
    assert(!mm_world_buffers_background(&w, cache, 11, 22, 33, 44, &f.s));
    assert(!w.owners[MM_WORLD_CACHE] && !f.changed && f.objects == count && cache->refs == 1);
    bg = (Object *)w.owners[MM_WORLD_DEPTH_BACKGROUND];
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 4; ++x)
            assert(bg->pixels[y * 4 + x] == (y < 3 ? (unsigned char)((y * 8 + (x / 2) * 4 + x % 2) * 7 + 1) : 255));
    for (x = bg->bytes; x < 512; ++x)
        assert(!bg->pixels[x]);
    mm_world_buffers_dispose(&w, &f.s);
    release(&f, cache);
    balanced(&f);
}
int main(void)
{
    initialization();
    modes();
    noops();
    restore_background();
    partial_background();
    short_depth();
    puts("C89 World buffers: ownership/locks, transactional modes, background failures, exact depth samples and "
         "cleanup passed.");
    return 0;
}
