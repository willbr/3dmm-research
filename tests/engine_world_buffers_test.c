#include "engine_world_background_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Fixture
{
    MMWorldBufferServices s;
    MMWorldBuffers *world;
    MMDepthOwnerHost host;
    MMStorageAllocator allocator;
    WorldTestAssets assets;
    int phase, fail, allocations, setups, changed;
} Fixture;
static void *allocate(void *p, void *old, MMU32 before, MMU32 bytes, int priority)
{
    Fixture *f = (Fixture *)p;
    void *allocation;
    assert(!old && !before && bytes && priority == MM_STORAGE_NORMAL);
    if (++f->phase == f->fail)
        return NULL;
    allocation = malloc(bytes);
    assert(allocation);
    ++f->allocations;
    return allocation;
}
static void release(void *p, void *memory, MMU32 bytes)
{
    Fixture *f = (Fixture *)p;
    assert(f->allocations > 0 && bytes);
    --f->allocations;
    free(memory);
}
static void scene(void *p)
{
    Fixture *f = (Fixture *)p;
    assert(f->world->rgb.pixels == f->world->working.surface.pixels);
    assert(f->world->z.pixels == mm_depth_owner_bitmap(f->world->depth_working)->pixels);
    assert(mm_region_empty(&f->world->dirty_working) && mm_region_empty(&f->world->dirty_screen));
    ++f->setups;
}
static void changed(void *p)
{
    Fixture *f = (Fixture *)p;
    assert(f->world->dirty_working.bounds.right == f->world->buffer_width);
    ++f->changed;
}
static void setup(Fixture *f, MMWorldBuffers *w)
{
    memset(f, 0, sizeof(*f));
    memset(w, 0, sizeof(*w));
    f->world = w;
    f->s.context = f;
    f->s.rgb_type = 8;
    f->s.depth_type = 16;
    f->s.depth_host = &f->host;
    f->s.read_colour = world_test_read_colour;
    f->s.read_depth = world_test_read_depth;
    f->s.setup_scene = scene;
    f->s.changed = changed;
    f->allocator.context = f;
    f->allocator.resize = allocate;
    f->allocator.release = release;
    f->host.objects = f->host.pixels = &f->allocator;
}
static MMResourceCache *make_cache(Fixture *f)
{
    size_t i;
    f->assets.width = f->assets.colour_height = f->assets.depth_height = 4;
    memset(f->assets.colour, 0x31, sizeof(f->assets.colour));
    for (i = 0; i < sizeof(f->assets.depth); ++i)
        f->assets.depth[i] = (unsigned char)(i * 7 + 1);
    world_test_cache_init(&f->assets, 0, &f->phase, &f->fail);
    return f->assets.cache;
}
static void balanced(Fixture *f)
{
    assert(!f->allocations);
}
static void initialization(void)
{
    Fixture f;
    MMWorldBuffers w;
    MMWorldPixels background, working;
    int hx, hy, scale, n, ok;
    for (hx = 0; hx < 2; ++hx)
        for (hy = 0; hy < 2; ++hy)
            for (scale = 1; scale <= 4; scale *= 2)
                for (n = 0; n < 9; ++n)
                {
                    setup(&f, &w);
                    f.fail = n;
                    ok = mm_world_buffers_init(&w, 4, 4, scale, hx, hy, &f.s);
                    assert(ok == (n == 0 || n > 4));
                    if (ok)
                    {
                        assert(w.buffer_width == 4 * scale / (scale == 1 && hx ? 2 : 1));
                        assert(w.buffer_height == 4 * scale / (scale == 1 && hy ? 2 : 1));
                        assert(w.rgb.row == w.buffer_width && w.z.row == w.buffer_width * 2 && f.setups == 1);
                        background = mm_world_buffer_pixels(&w.background);
                        working = mm_world_buffer_pixels(&w.working);
                        assert(mm_world_pixels_valid(&background, w.buffer_width, w.buffer_height));
                        assert(mm_world_pixels_valid(&working, w.buffer_width, w.buffer_height));
                        assert(!(background.row % 4) && !background.pixels[0] &&
                               !background.pixels[background.bytes - 1]);
                        assert(working.row == background.row && working.bytes == background.bytes &&
                               working.pixels != background.pixels && working.pixels == w.rgb.pixels &&
                               !working.pixels[working.bytes - 1]);
                    }
                    mm_world_buffers_dispose(&w);
                    balanced(&f);
                }
    setup(&f, &w);
    assert(!mm_world_buffers_init(&w, 8192, 4, 1, 0, 0, &f.s) && !f.phase);
    mm_world_buffers_dispose(&w);
    balanced(&f);
    setup(&f, &w);
    assert(!mm_world_buffers_init(&w, 1, 4, 1, 1, 0, &f.s) && !f.phase && !f.setups);
    mm_world_buffers_dispose(&w);
    balanced(&f);
}
static void check_depth(const MMWorldBuffers *w)
{
    const MMDepthBitmap *depth = mm_depth_owner_bitmap(w->depth_background);
    const unsigned char *pixels = (const unsigned char *)depth->pixels;
    int x, y, sx, sy;
    for (y = 0; y < w->buffer_height; ++y)
        for (x = 0; x < w->buffer_width; ++x)
        {
            sy = w->scale > 1 ? y / w->scale : y * (w->half_y ? 2 : 1);
            sx = w->scale > 1 ? x / w->scale : x * (w->half_x ? 2 : 1);
            assert(pixels[y * depth->row_bytes + x * 2] == (unsigned char)((sy * 8 + sx * 2) * 7 + 1));
            assert(pixels[y * depth->row_bytes + x * 2 + 1] == (unsigned char)((sy * 8 + sx * 2 + 1) * 7 + 1));
        }
}
static void modes(void)
{
    Fixture f;
    MMWorldBuffers w, old;
    MMResourceCache *cache;
    MMWorldPixels retained, working;
    const MMPixelRect viewport = {0, 0, 4, 4}, hole = {1, 1, 3, 3};
    int target, n, ok, count, changed;
    for (target = 0; target < 5; ++target)
        for (n = 1; n < 13; ++n)
        {
            setup(&f, &w);
            assert(mm_world_buffers_init(&w, 4, 4, 2, 0, 0, &f.s));
            cache = make_cache(&f);
            assert(mm_world_buffers_background(&w, cache, 11, 22, 33, 44, &f.s));
            check_depth(&w);
            mm_region_set_rect(&w.dirty_screen, &viewport);
            assert(mm_region_apply_rect(&w.dirty_working, MM_REGION_DIFFERENCE, &hole, NULL, 0, NULL) ==
                   MM_REGION_UPDATED);
            assert(mm_region_apply_rect(&w.dirty_screen, MM_REGION_DIFFERENCE, &hole, NULL, 0, NULL) ==
                   MM_REGION_UPDATED);
            retained = mm_world_buffer_pixels(&w.background);
            working = mm_world_buffer_pixels(&w.working);
            retained.pixels[0] = 0x99;
            working.pixels[0] = 0x77;
            old = w;
            count = f.allocations;
            changed = f.changed;
            f.fail = f.phase + n;
            ok = mm_world_buffers_mode(&w, target < 3 ? (target != 1) : 0, target < 3 ? (target != 0) : 0,
                                       target == 4 ? 4 : 1, &f.s);
            assert(ok == (n > 6));
            if (!ok)
            {
                assert(w.depth_background == old.depth_background && w.depth_working == old.depth_working &&
                       w.cache == old.cache);
                assert(w.rgb.pixels == old.rgb.pixels && w.z.pixels == old.z.pixels && w.scale == old.scale &&
                       w.half_x == old.half_x && w.half_y == old.half_y && w.buffer_width == old.buffer_width &&
                       w.buffer_height == old.buffer_height && f.allocations == count && f.changed == changed);
                assert(w.dirty_working.bands == old.dirty_working.bands && w.dirty_screen.bands == old.dirty_screen.bands);
                assert(w.background.surface.pixels == retained.pixels && retained.pixels[0] == 0x99 &&
                       w.background.bytes == retained.bytes);
                assert(w.working.surface.pixels == working.pixels && working.pixels[0] == 0x77 &&
                       w.working.bytes == working.bytes);
            }
            else
            {
                check_depth(&w);
                assert(w.background.surface.width == w.buffer_width && w.background.surface.height == w.buffer_height &&
                       mm_world_buffer_pixels(&w.background).pixels[0] == 0x31);
                assert(w.working.surface.pixels == w.rgb.pixels && !mm_world_buffer_pixels(&w.working).pixels[0]);
                assert(f.changed == changed + 2 && w.dirty_screen.bounds.right == w.buffer_width);
                assert(!w.dirty_working.bands && !w.dirty_screen.bands);
            }
            assert(cache->references->count == 2);
            mm_world_buffers_dispose(&w);
            assert(cache->references->count == 1);
            mm_resource_cache_release(cache);
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
    mm_world_buffers_dispose(&w);
    balanced(&f);
}
static void restore_background(void)
{
    Fixture f;
    MMWorldBuffers w;
    MMResourceCache *cache;
    MMWorldPixels rgb;
    setup(&f, &w);
    assert(mm_world_buffers_init(&w, 6, 4, 1, 0, 0, &f.s));
    cache = make_cache(&f);
    assert(mm_world_buffers_background(&w, cache, 11, 22, 33, 44, &f.s));
    rgb = mm_world_buffer_pixels(&w.background);
    rgb.pixels[0] = 0x99;
    assert(mm_depth_owner_binding(w.depth_background)->attached &&
           mm_depth_owner_binding(w.depth_background)->priority == MM_RESOURCE_TOSS_FIRST_PRIORITY);
    mm_depth_owner_detach(w.depth_background);
    assert(!cache->count);
    f.fail = f.phase + 2;
    assert(!mm_world_buffers_restore_background(&w, &f.s));
    assert(cache->references->count == 3 && w.cache == cache && rgb.pixels[0] == 0x99 && f.changed == 1);
    assert(mm_world_buffers_restore_background(&w, &f.s));
    assert(cache->references->count == 2 && w.cache == cache && rgb.pixels[0] == 0x31 && f.changed == 2);
    f.assets.colour[5] = 0;
    memset(rgb.pixels, 0x99, rgb.bytes);
    assert(mm_world_buffers_restore_background(&w, &f.s));
    assert(rgb.row == 8 && rgb.pixels[0] == 0x31 && rgb.pixels[8] == 0x31 && rgb.pixels[9] == 0x99);
    assert(rgb.pixels[4] == 0x99 && rgb.pixels[6] == 0x99 && rgb.pixels[31] == 0x99);
    mm_world_buffers_dispose(&w);
    mm_resource_cache_release(cache);
    balanced(&f);
}
static void partial_background(void)
{
    Fixture f;
    MMWorldBuffers w;
    MMResourceCache *cache;
    MMWorldPixels bg;
    int count, phase;
    setup(&f, &w);
    assert(mm_world_buffers_init(&w, 4, 4, 2, 0, 0, &f.s));
    cache = make_cache(&f);
    mm_depth_owner_release(w.depth_background);
    w.depth_background = mm_depth_owner_new(8, 7, NULL, &f.host);
    assert(w.depth_background);
    count = f.allocations;
    phase = f.phase;
    assert(!mm_world_buffers_background(&w, cache, 11, 22, 33, 44, &f.s));
    assert(!w.cache && cache->references->count == 1 && f.allocations == count && !f.changed && f.phase == phase + 2);
    bg = mm_world_buffer_pixels(&w.background);
    assert(bg.pixels[0] == 0x31 && bg.pixels[bg.bytes - 1] == 0x31);
    mm_world_buffers_dispose(&w);
    mm_resource_cache_release(cache);
    balanced(&f);
    setup(&f, &w);
    assert(mm_world_buffers_init(&w, 4, 4, 1, 1, 1, &f.s));
    cache = make_cache(&f);
    count = f.allocations;
    --w.background.bytes;
    assert(!mm_world_buffers_background(&w, cache, 11, 22, 33, 44, &f.s) && f.allocations == count && !w.cache &&
           !f.changed);
    mm_world_buffers_dispose(&w);
    mm_resource_cache_release(cache);
    balanced(&f);
}
static void short_depth(void)
{
    Fixture f;
    MMWorldBuffers w;
    MMResourceCache *cache;
    const MMDepthBitmap *bg;
    const unsigned char *pixels;
    int count, x, y;
    setup(&f, &w);
    assert(mm_world_buffers_init(&w, 4, 4, 1, 1, 0, &f.s));
    cache = make_cache(&f);
    count = f.allocations;
    f.assets.depth_height = 3;
    assert(!mm_world_buffers_background(&w, cache, 11, 22, 33, 44, &f.s));
    assert(!w.cache && !f.changed && f.allocations == count && cache->references->count == 1);
    bg = mm_depth_owner_bitmap(w.depth_background);
    pixels = (const unsigned char *)bg->pixels;
    for (y = 0; y < 4; ++y)
        for (x = 0; x < 4; ++x)
            assert(pixels[y * 4 + x] == (y < 3 ? (unsigned char)((y * 8 + (x / 2) * 4 + x % 2) * 7 + 1) : 255));
    assert(bg->bytes == 16);
    mm_world_buffers_dispose(&w);
    mm_resource_cache_release(cache);
    balanced(&f);
}

static void indexed_copy(void)
{
    unsigned char source[16], target[12];
    MMWorldPixels src = {NULL, 4, sizeof(source)}, dst = {NULL, 3, 6};
    static const unsigned char reduced[] = {6, 8, 0xcc, 14, 16, 0xcc};
    static const unsigned char near[] = {1, 2, 3, 0xcc, 5, 6, 7, 0xcc, 9, 10, 11, 0xcc};
    int i;

    src.pixels = source;
    dst.pixels = target;
    for (i = 0; i < 16; ++i)
        source[i] = (unsigned char)(i + 1);
    memset(target, 0xcc, sizeof(target));
    assert(mm_world_copy_rgb(&src, 4, 4, &dst, 2, 2));
    assert(!memcmp(target, reduced, sizeof(reduced)));
    dst.row = 4;
    dst.bytes = sizeof(target);
    memset(target, 0xcc, sizeof(target));
    assert(mm_world_copy_rgb(&src, 4, 4, &dst, 3, 3));
    assert(!memcmp(target, near, sizeof(near)));
    --src.bytes;
    assert(!mm_world_copy_rgb(&src, 4, 4, &dst, 2, 2));
    assert(!memcmp(target, near, sizeof(near)));
}

static void scaled_transparency(void)
{
    static const unsigned char source[] = {1, 0, 2, 3};
    static const unsigned char expected[] = {
        1, 1, 0, 0, 0xcc, 0xcc, 1, 1, 0, 0, 0xcc, 0xcc,
        2, 2, 3, 3, 0xcc, 0xcc, 2, 2, 3, 3, 0xcc, 0xcc
    };
    const MMMaskedRect bounds = {0, 0, 2, 2};
    unsigned char target[sizeof(expected)];
    MMWorldPixels view = {NULL, 6, sizeof(target)};
    MMMaskedOwner *mask;

    mask = mm_masked_owner_new(source, 2, 2, &bounds, 2, -3, 0, 0, 0, NULL);
    assert(mask);
    view.pixels = target;
    memset(target, 0xcc, sizeof(target));
    assert(mm_world_fill_background_rgb(mm_masked_owner_bitmap(mask), 2, 2, &view, 4, 4));
    assert(!memcmp(target, expected, sizeof(target)));
    --view.bytes;
    assert(!mm_world_fill_background_rgb(mm_masked_owner_bitmap(mask), 2, 2, &view, 4, 4));
    assert(!memcmp(target, expected, sizeof(target)));
    mm_masked_owner_release(mask);
}

int main(void)
{
    initialization();
    modes();
    noops();
    restore_background();
    partial_background();
    short_depth();
    indexed_copy();
    scaled_transparency();
    puts("C89 World buffers: pixel ownership, transactional modes, background failures, exact depth samples and "
         "cleanup passed.");
    return 0;
}
