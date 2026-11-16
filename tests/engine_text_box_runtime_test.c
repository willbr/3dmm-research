#include "mm/text_box_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { printf("text-box check failed at %d: %s\n", __LINE__, #e); return 1; } } while (0)
typedef struct Event
{
    MMTextBoxOperation op;
    MMI32 first, end, current;
    int selected, scene;
} Event;
typedef struct Host
{
    MMTextBoxState state, *live;
    MMTextBoxRuntime runtime;
    MMTextBoxBounds bounds;
    Event events[32];
    int calls, failAt, error, views, playing, create, selected, mutate, freeOnRelease;
    int sceneA, sceneB, viewA, border;
    MMI32 frame;
} Host;
static int provide(void *context, MMTextBoxOperation op, MMTextBoxRequest *q)
{
    Host *h = (Host *)context;
    Event *e = &h->events[h->calls++];
    e->op = op; e->first = h->live->first; e->end = h->live->end; e->current = h->live->current;
    e->selected = h->runtime.selected; e->scene = h->runtime.scene == &h->sceneA;
    if (h->calls == h->failAt) return h->error;
    switch (op)
    {
    case MM_TBOX_VIEW_COUNT: q->value = h->views; break;
    case MM_TBOX_VIEW: q->object = h->views ? &h->viewA : NULL; break;
    case MM_TBOX_CREATE_VIEW:
        q->object = h->create ? &h->border : NULL;
        if (h->mutate) h->live->end = h->live->current;
        break;
    case MM_TBOX_PLAYING: q->value = h->playing; break;
    case MM_TBOX_SCENE_FRAME:
        if (q->object != &h->sceneB) return -81;
        q->value = h->frame++;
        break;
    case MM_TBOX_SCENE_LAST: q->value = 99; break;
    case MM_TBOX_IS_SELECTED: q->value = h->selected; break;
    case MM_TBOX_NEEDS_SCROLL: case MM_TBOX_TEXT_SELECTED: q->value = 1; break;
    case MM_TBOX_RELEASE_VIEW:
        if (q->object != &h->viewA) return -82;
        if (h->freeOnRelease) { free(h->live); h->live = NULL; }
        h->views = 0;
        break;
    case MM_TBOX_DEACTIVATE: if (q->object != &h->border) return -83; break;
    case MM_TBOX_DIRTY:
        if (h->mutate) { h->live->current = 12; h->bounds.left = -9; }
        break;
    default: break;
    }
    return 1;
}
static MMTextBoxRuntimeServices setup(Host *h)
{
    MMTextBoxRuntimeServices p;
    memset(h, 0, sizeof(*h));
    h->live = &h->state; h->state.first = 1; h->state.end = 10; h->state.current = 3;
    h->runtime.scene = &h->sceneA; h->create = h->playing = h->selected = 1;
    h->bounds.left = 7; h->bounds.top = 8; h->bounds.right = 100; h->bounds.bottom = 90;
    h->frame = 5; h->error = -73;
    p.context = h; p.call = provide;
    return p;
}
static int invoke(int kind, Host *h, const MMTextBoxRuntimeServices *p)
{
    MMTextBoxLifetime life;
    switch (kind)
    {
    case 0: return mm_text_box_attach(h->live, &h->runtime, &h->sceneB, p);
    case 1: return mm_text_box_dirty(&h->runtime, 1, p);
    case 2: return mm_text_box_set_type(h->live, 1, p);
    case 3: return mm_text_box_set_bounds(h->live, &h->runtime, &h->bounds, p);
    case 4: h->state.end = 2147483647L; return mm_text_box_lifetime(h->live, &life, p);
    case 5: return mm_text_box_select(&h->runtime, 1, p);
    case 6: h->views = 0; return mm_text_box_goto(h->live, 7, p);
    case 7: h->state.end = 2; return mm_text_box_show(h->live, p);
    case 8: return mm_text_box_hide(h->live, p);
    case 9: return mm_text_box_attach_mouse(p);
    case 10: h->state.current = 1; return mm_text_box_needs_scroll(h->live, p);
    case 11: return mm_text_box_scroll(p);
    default: return mm_text_box_text_selected(h->live, p);
    }
}
int main(void)
{
    const MMI32 values[] = {(-2147483647L - 1), -3, 0, 1, 3, 2147483647L};
    Host h, reference;
    MMTextBoxRuntimeServices p;
    MMTextBoxLifetime life;
    unsigned a, b, c;
    int views, playing, create, visible, expected, n, kind, failure, error, result;
    MMTextBoxOperation ops[6];
    for (a = 0; a < 6; ++a) for (b = 0; b < 6; ++b) for (c = 0; c < 6; ++c)
        for (views = 0; views != 2; ++views) for (playing = 0; playing != 2; ++playing)
            for (create = 0; create != 2; ++create)
            {
                p = setup(&h); h.state.first = values[a]; h.state.end = values[b];
                h.views = views; h.playing = playing; h.create = create;
                visible = values[c] >= values[a] && values[c] < values[b];
                CHECK(mm_text_box_visible_range(values[a], values[b], values[c]) == visible);
                CHECK(mm_text_box_scroll_frame(0, values[a], values[c]) == (values[a] == values[c]));
                CHECK(!mm_text_box_scroll_frame(1, values[a], values[c]));
                n = 0; ops[n++] = MM_TBOX_VIEW_COUNT;
                if (views) ops[n++] = MM_TBOX_VIEW;
                else if (visible)
                {
                    ops[n++] = MM_TBOX_CREATE_VIEW;
                    if (create) { ops[n++] = MM_TBOX_PLAYING; if (playing) ops[n++] = MM_TBOX_DEACTIVATE; }
                }
                if (views && !visible) ops[n++] = MM_TBOX_RELEASE_VIEW;
                expected = views || !visible || create;
                CHECK(mm_text_box_goto(&h.state, values[c], &p) == expected);
                CHECK(h.calls == n && h.state.current == values[c]);
                for (failure = 0; failure < n; ++failure)
                {
                    CHECK(h.events[failure].op == ops[failure]);
                    CHECK(h.events[failure].current == (failure < (views ? 2 : 1) ? 3 : values[c]));
                }
            }
    /* Every operation's error path must stop at the exact successful prefix,
     * before later effects, while preserving the original error code. */
    for (kind = 0; kind < 13; ++kind)
    {
        p = setup(&reference); reference.views = 1;
        CHECK(invoke(kind, &reference, &p) >= 0);
        for (error = -1; error >= -2; --error) for (failure = 1; failure <= reference.calls; ++failure)
        {
            p = setup(&h); h.views = 1; h.failAt = failure; h.error = error;
            result = invoke(kind, &h, &p);
            CHECK(result == error && h.calls == failure);
            CHECK(!memcmp(h.events, reference.events, failure * sizeof(Event)));
        }
    }
    p = setup(&h); CHECK(mm_text_box_attach(&h.state, &h.runtime, &h.sceneB, &p) == 1);
    CHECK(h.state.current == 5 && h.state.first == 6 && h.state.end == 2147483647L && h.runtime.scene == &h.sceneB);
    CHECK(h.events[1].current == 5 && h.events[1].first == 1 && h.events[1].scene);
    p = setup(&h); h.failAt = 2;
    CHECK(mm_text_box_attach(&h.state, &h.runtime, &h.sceneB, &p) == -73);
    CHECK(h.state.current == 5 && h.state.first == 1 && h.state.end == 10 && h.runtime.scene == &h.sceneA);
    p = setup(&h); h.state.end = 2147483647L; h.failAt = 1;
    life.first = -99; life.last = -98;
    CHECK(mm_text_box_lifetime(&h.state, &life, &p) == -73);
    CHECK(life.published == 1 && life.first == 1 && life.last == -98);
    p = setup(&h); h.state.first = h.state.end = 3;
    CHECK(mm_text_box_lifetime(&h.state, &life, &p) == 1 && life.first == 3 && life.last == 2);
    h.state.end = 2; CHECK(!mm_text_box_lifetime(&h.state, &life, &p) && !life.published);
    p = setup(&h); h.state.end = h.state.current;
    CHECK(mm_text_box_show(&h.state, &p) == 1 && h.state.end == 3 && !mm_text_box_visible(&h.state));
    CHECK(h.calls == 2 && h.events[0].op == MM_TBOX_DIRTY && h.events[1].op == MM_TBOX_VIEW_COUNT);
    p = setup(&h); h.mutate = 1; h.state.end = 8;
    CHECK(mm_text_box_show(&h.state, &p) == 1 && h.state.end == 12);
    /* DIRTY changed current to 12, so show first extended to 13; create then
     * changed end to 12. A new border is not released using the old null view. */
    CHECK(h.events[2].op == MM_TBOX_CREATE_VIEW && h.events[2].end == 13 && h.calls == 5);
    p = setup(&h); h.state.current = -2;
    CHECK(mm_text_box_show(&h.state, &p) == 1 && h.state.first == -2 && h.state.end == 10);
    p = setup(&h); h.state.current = 2147483647L;
    CHECK(mm_text_box_show(&h.state, &p) == 1 && h.state.end == (-2147483647L - 1));
    CHECK(h.calls == 3 && h.events[1].op == MM_TBOX_DIAGNOSTIC);
    p = setup(&h); h.mutate = 1;
    CHECK(mm_text_box_set_bounds(&h.state, &h.runtime, &h.bounds, &p) == 1 && h.state.left == -9);
    h.calls = 0; CHECK(mm_text_box_set_bounds(&h.state, &h.runtime, &h.bounds, &p) == 1 && !h.calls);
    h.runtime.scene = NULL; h.bounds.left = 77;
    CHECK(mm_text_box_set_bounds(&h.state, &h.runtime, &h.bounds, &p) == 1 && !h.calls && h.state.left == 77);
    p = setup(&h); h.views = 2;
    CHECK(mm_text_box_select(&h.runtime, 1, &p) == 1 && h.runtime.selected);
    CHECK(h.calls == 5 && h.events[1].op == MM_TBOX_DIAGNOSTIC && h.events[0].selected);
    h.calls = 0; CHECK(mm_text_box_select(&h.runtime, 1, &p) == 1 && !h.calls);
    p = setup(&h); h.views = 1; h.freeOnRelease = 1;
    h.live = (MMTextBoxState *)malloc(sizeof(*h.live)); CHECK(h.live != NULL); *h.live = h.state;
    CHECK(mm_text_box_goto(h.live, 10, &p) == 1 && h.live == NULL);
    puts("C89 text-box state, frame/view policy, lifetime, selection, live callbacks and errors passed");
    return 0;
}
