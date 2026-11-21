#include "mm/scene_thumbnail.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Scene thumbnail: %d\n", __LINE__);                                                        \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Port
{
    int refs, locked, width, height;
} Port;
typedef struct Host
{
    MMSceneOwner owner;
    int movie, background, view, actor, text, old_bitmap, new_bitmap, pixels;
    Port ports[2];
    int trace[128], traces, has_view, dirty, muted, mode, mode_changes, fail_port, port_calls, fail_seek, seek_calls,
        fail_bitmap;
    int old_refs, new_refs, copy_live, mark_calls, actor_hilited, text_selected;
    MMU32 initial_mask;
} Host;
static void event(Host *h, int n)
{
    CHECK(h->traces < 128);
    h->trace[h->traces++] = n;
}
static void expect(Host *h, const int *values, int n)
{
    CHECK(h->traces == n && !memcmp(h->trace, values, n * sizeof(*values)));
    h->traces = 0;
}
static MMI32 time(void *p)
{
    event((Host *)p, 3);
    return 123;
}
static void mute(void *p)
{
    Host *h = (Host *)p;
    event(h, 4);
    h->muted = 1;
}
static void resume(void *p, MMI32 value)
{
    Host *h = (Host *)p;
    CHECK(value == 123 && h->muted && !h->copy_live && !h->ports[0].refs && !h->ports[1].refs);
    CHECK(h->owner.runtime.disabled == h->initial_mask);
    event(h, 5);
    h->muted = 0;
}
static void *view(void *p)
{
    Host *h = (Host *)p;
    CHECK(h->muted);
    event(h, 6);
    return h->has_view ? &h->view : NULL;
}
static int dirty(void *p)
{
    Host *h = (Host *)p;
    event(h, 7);
    return h->dirty;
}
static void dimensions(void *p, MMI32 *w, MMI32 *h)
{
    event((Host *)p, 8);
    *w = 320;
    *h = 240;
}
static void *new_port(void *p, const MMMaskedRect *r)
{
    Host *h = (Host *)p;
    int i = h->port_calls++;
    CHECK(i < 2);
    event(h, 9 + i);
    CHECK(!r->left && !r->top);
    CHECK(r->right == (i ? 144 : 320) && r->bottom == (i ? 81 : 240));
    if (h->fail_port == i + 1)
        return NULL;
    h->ports[i].refs = 1;
    h->ports[i].width = r->right;
    h->ports[i].height = r->bottom;
    return h->ports + i;
}
static void palette(void *p, void *port)
{
    Host *h = (Host *)p;
    CHECK(port == h->ports + 1 && h->owner.runtime.disabled == h->initial_mask);
    event(h, 11);
}
static void release_port(void *p, void *port)
{
    Host *h = (Host *)p;
    Port *v = (Port *)port;
    CHECK(v->refs > 0);
    --v->refs;
    event(h, v == h->ports ? 12 : 13);
    CHECK(h->owner.runtime.disabled == (h->fail_port == 2 ? h->initial_mask : h->initial_mask | 3U));
}
static int seek(void *p, MMI32 frame)
{
    Host *h = (Host *)p;
    ++h->seek_calls;
    event(h, frame == h->owner.data.firstFrame ? 14 : 15);
    CHECK(h->muted && h->owner.runtime.disabled == (h->initial_mask | 3U));
    if (h->copy_live)
        CHECK(h->ports[0].refs == 1 && h->ports[1].refs == 1);
    h->owner.data.currentFrame = frame;
    if (h->seek_calls == 2 && h->fail_seek == 2)
        --h->owner.data.currentFrame;
    return h->seek_calls != h->fail_seek;
}
static int mode(void *p, void *v)
{
    Host *h = (Host *)p;
    CHECK(v == &h->view);
    event(h, 16);
    return h->mode;
}
static int selection_mode(void *p)
{
    return ((Host *)p)->mode;
}
static void hilite(void *p, void *object, int value)
{
    Host *h = (Host *)p;
    CHECK(object == &h->actor);
    h->actor_hilited = value;
}
static int text_selected(void *p, void *object)
{
    Host *h = (Host *)p;
    CHECK(object == &h->text);
    return h->text_selected;
}
static void select_text(void *p, void *object, int value)
{
    Host *h = (Host *)p;
    CHECK(object == &h->text);
    h->text_selected = value;
}
static void invalidate(void *p)
{
    Host *h = (Host *)p;
    CHECK(h->muted && h->owner.data.currentFrame == h->owner.data.firstFrame);
}
static void action_menu(void *p)
{
    Host *h = (Host *)p;
    if (!h->mode)
        event(h, h->owner.selected_actor ? 19 : 17);
}
static void text_notice(void *p)
{
    Host *h = (Host *)p;
    event(h, h->owner.selected_text ? 20 : 18);
}
static void draw(void *p, void *v, void *port, const MMMaskedRect *r)
{
    Host *h = (Host *)p;
    CHECK(v == &h->view && port == h->ports && r->right == 320 && r->bottom == 240);
    CHECK(h->muted && h->owner.data.currentFrame == h->owner.data.firstFrame);
    CHECK(!(h->mode ? h->owner.selected_text : h->owner.selected_actor));
    CHECK(!h->actor_hilited && !h->text_selected);
    event(h, 21);
    if (h->mode_changes)
        h->mode = !h->mode;
}
static void copy(void *p, void *src, void *dst, const MMMaskedRect *a, const MMMaskedRect *b)
{
    Host *h = (Host *)p;
    CHECK(src == h->ports && dst == h->ports + 1 && a->right == 320 && b->right == 144 && b->bottom == 81);
    if (h->mode)
        CHECK(h->owner.selected_text == &h->text && h->text_selected && !h->actor_hilited);
    else
        CHECK(h->owner.selected_actor == &h->actor && h->actor_hilited && !h->text_selected);
    CHECK(!h->copy_live);
    h->copy_live = 1;
    ++h->ports[0].refs;
    ++h->ports[1].refs;
    event(h, 22);
}
static void end_copy(void *p)
{
    Host *h = (Host *)p;
    CHECK(h->copy_live && h->owner.runtime.disabled == h->initial_mask);
    CHECK(h->ports[0].refs == 1 && h->ports[1].refs == 1);
    h->copy_live = 0;
    --h->ports[1].refs;
    --h->ports[0].refs;
    event(h, 23);
}
static void release_bitmap(void *p, void *bitmap)
{
    Host *h = (Host *)p;
    CHECK(bitmap == &h->old_bitmap && h->owner.owned[MM_SCENE_THUMBNAIL] == bitmap && h->old_refs == 1);
    --h->old_refs;
    event(h, 24);
}
static MMI32 stride(void *p, void *port)
{
    Host *h = (Host *)p;
    CHECK(port == h->ports + 1 && h->copy_live && !h->old_refs);
    event(h, 27);
    return 144;
}
static void *lock(void *p, void *port)
{
    Host *h = (Host *)p;
    CHECK(port == h->ports + 1 && !h->ports[1].locked);
    h->ports[1].locked = 1;
    event(h, 28);
    return &h->pixels;
}
static void *bitmap(void *p, const void *pixels, MMI32 row, const MMMaskedRect *r, int transparent)
{
    Host *h = (Host *)p;
    CHECK(pixels == &h->pixels && row == 144 && r->bottom == 81 && transparent == 250);
    CHECK(h->ports[1].locked && !h->owner.owned[MM_SCENE_THUMBNAIL] && !h->old_refs);
    event(h, 29);
    if (h->fail_bitmap)
        return NULL;
    h->new_refs = 1;
    return &h->new_bitmap;
}
static void unlock(void *p, void *port)
{
    Host *h = (Host *)p;
    CHECK(port == h->ports + 1 && h->ports[1].locked);
    CHECK(h->owner.owned[MM_SCENE_THUMBNAIL] == (h->fail_bitmap ? NULL : &h->new_bitmap));
    h->ports[1].locked = 0;
    event(h, 30);
}
static void mark(void *p, void *movie)
{
    Host *h = (Host *)p;
    CHECK(movie == &h->movie);
    ++h->mark_calls;
}
static MMSceneThumbnailServices init(Host *h)
{
    MMSceneThumbnailServices s;
    memset(h, 0, sizeof(*h));
    memset(&s, 0, sizeof(s));
    CHECK(mm_scene_owner_init(&h->owner, &h->movie, 1, NULL));
    h->owner.data.currentFrame = 5;
    h->owner.data.lastFrame = 9;
    h->initial_mask = h->owner.runtime.disabled = 128;
    h->has_view = h->dirty = h->old_refs = 1;
    h->owner.owned[MM_SCENE_BACKGROUND] = &h->background;
    h->owner.owned[MM_SCENE_THUMBNAIL] = &h->old_bitmap;
    h->owner.selected_actor = &h->actor;
    h->owner.selected_text = &h->text;
    h->actor_hilited = h->text_selected = 1;
    s.context = h;
    s.owner = &h->owner;
    s.owners.context = h;
    s.owners.release_thumbnail = release_bitmap;
    s.selection.context = h;
    s.selection.owner = &h->owner;
    s.selection.view_mode = selection_mode;
    s.selection.hilite = hilite;
    s.selection.text_selected = text_selected;
    s.selection.select_text = select_text;
    s.selection.invalidate = invalidate;
    s.selection.action_menu = action_menu;
    s.selection.text_notice = text_notice;
    s.sound_time = time;
    s.mute = mute;
    s.resume = resume;
    s.view = view;
    s.dirty = dirty;
    s.dimensions = dimensions;
    s.new_port = new_port;
    s.palette = palette;
    s.release_port = release_port;
    s.seek = seek;
    s.text_mode = mode;
    s.draw = draw;
    s.copy_context = h;
    s.copy = copy;
    s.end_copy = end_copy;
    s.row_bytes = stride;
    s.lock = lock;
    s.bitmap = bitmap;
    s.unlock = unlock;
    return s;
}
static void test_gates(void)
{
    Host h;
    MMSceneThumbnailServices s;
    int i;
    for (i = 0; i < 5; ++i)
    {
        s = init(&h);
        if (i == 0)
            h.owner.owned[MM_SCENE_BACKGROUND] = NULL;
        if (i == 1)
            h.has_view = 0;
        if (i == 2)
            h.dirty = 0;
        if (i == 3)
            h.fail_port = 1;
        if (i == 4)
            h.fail_port = 2;
        CHECK(mm_scene_thumbnail(&s) == &h.old_bitmap && h.old_refs == 1);
        CHECK(h.owner.data.currentFrame == 5 && !h.new_refs && !h.muted);
        CHECK(h.owner.selected_actor == &h.actor && h.owner.selected_text == &h.text);
        if (i < 2)
        {
            const int e[] = {3, 4, 6, 5};
            expect(&h, e, 4);
        }
        if (i == 2)
        {
            const int e[] = {3, 4, 6, 7, 5};
            expect(&h, e, 5);
        }
        if (i == 3)
        {
            const int e[] = {3, 4, 6, 7, 8, 9, 5};
            expect(&h, e, 7);
        }
        if (i == 4)
        {
            const int e[] = {3, 4, 6, 7, 8, 9, 10, 12, 5};
            expect(&h, e, 9);
        }
    }
}
static void test_render(void)
{
    Host h;
    MMSceneThumbnailServices s;
    int i;
    for (i = 0; i < 3; ++i)
    {
        s = init(&h);
        h.mode = i == 1;
        h.mode_changes = i == 2;
        CHECK(mm_scene_thumbnail(&s) == &h.new_bitmap && h.new_refs == 1 && !h.old_refs);
        CHECK(h.owner.data.currentFrame == 5 && h.seek_calls == 2 && !h.muted);
        {
            int e[] = {3,  4,  6,  7,  8,  9,  10, 11, 14, 16, 17, 21, 16,
                       19, 22, 24, 27, 28, 29, 30, 12, 13, 15, 23, 5};
            if (i == 1)
                e[10] = 18;
            if (i > 0)
                e[13] = 20;
            expect(&h, e, sizeof(e) / sizeof(*e));
        }
    }
    s = init(&h);
    h.fail_seek = 1;
    CHECK(mm_scene_thumbnail(&s) == &h.old_bitmap && h.old_refs == 1 && h.owner.data.currentFrame == 1);
    {
        const int e[] = {3, 4, 6, 7, 8, 9, 10, 11, 14, 12, 13, 5};
        expect(&h, e, 12);
    }
    s = init(&h);
    h.fail_bitmap = 1;
    CHECK(!mm_scene_thumbnail(&s) && !h.old_refs && !h.new_refs && h.owner.data.currentFrame == 5);
    CHECK(h.trace[h.traces - 2] == 23 && h.trace[h.traces - 1] == 5);
    s = init(&h);
    h.fail_seek = 2;
    CHECK(mm_scene_thumbnail(&s) == &h.new_bitmap && h.owner.data.currentFrame == 4 && h.seek_calls == 2);
    s = init(&h);
    h.owner.data.currentFrame = 1;
    CHECK(mm_scene_thumbnail(&s) == &h.new_bitmap && !h.seek_calls);
    s = init(&h);
    h.owner.data.currentFrame = 10;
    CHECK(mm_scene_thumbnail(&s) == &h.new_bitmap && h.owner.data.currentFrame == 1 && h.seek_calls == 1);
    s = init(&h);
    h.owner.data.currentFrame = 0;
    CHECK(mm_scene_thumbnail(&s) == &h.new_bitmap && h.owner.data.currentFrame == 1 && h.seek_calls == 1);
    s = init(&h);
    h.owner.data.currentFrame = 9;
    CHECK(mm_scene_thumbnail(&s) == &h.new_bitmap && h.owner.data.currentFrame == 9 && h.seek_calls == 2);
}
static void test_dirty(void)
{
    Host h;
    MMSceneThumbnailServices t = init(&h);
    MMSceneDirtyServices s;
    s.context = &h;
    s.mark = mark;
    mm_scene_mark_dirty(t.owner, 0, &s);
    CHECK(!h.mark_calls);
    mm_scene_mark_dirty(t.owner, 1, &s);
    CHECK(h.mark_calls == 1);
    h.owner.movie = NULL;
    mm_scene_mark_dirty(t.owner, 1, &s);
    CHECK(h.mark_calls == 1);
}
int main(void)
{
    test_gates();
    test_render();
    test_dirty();
    puts("Scene thumbnail policy passed");
    return 0;
}
