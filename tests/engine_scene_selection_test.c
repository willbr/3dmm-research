#include "three_mm/scene_selection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "scene selection: %d\n", __LINE__);                                                        \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Object
{
    int id, selected, visible;
    unsigned short name[256];
    ThreeMMI32 length;
    ThreeMMActorPoint position;
} Object;
typedef struct Host
{
    Object objects[5], *actors[8], *extras[8];
    ThreeMMSceneSelection selection;
    int mode, actor_count, extra_count, allocated, fail_allocate, fail_append, append_calls;
    int trace[256], trace_count;
} Host;
static void event(Host *h, int code)
{
    CHECK(h->trace_count < 256);
    h->trace[h->trace_count++] = code;
}
static void set_selected(void *p, int text, void *object)
{
    Host *h = (Host *)p;
    CHECK((text ? h->selection.text : h->selection.actor) == object);
    event(h, text ? 41 : 40);
}
static int mode(void *p)
{
    return ((Host *)p)->mode;
}
static ThreeMMI32 count(void *p, int extras)
{
    Host *h = (Host *)p;
    return extras ? h->extra_count : h->actor_count;
}
static void *at(void *p, int extras, ThreeMMI32 index)
{
    Host *h = (Host *)p;
    CHECK(index >= 0 && index < (extras ? h->extra_count : h->actor_count));
    return extras ? h->extras[index] : h->actors[index];
}
static int ensure(void *p)
{
    Host *h = (Host *)p;
    if (h->allocated)
        return 1;
    event(h, 60);
    if (h->fail_allocate)
        return 0;
    h->allocated = 1;
    return 1;
}
static int append(void *p, void *actor)
{
    Host *h = (Host *)p;
    event(h, 61);
    if (++h->append_calls == h->fail_append)
        return 0;
    CHECK(h->extra_count < 8);
    h->extras[h->extra_count++] = (Object *)actor;
    return 1;
}
static void erase(void *p, ThreeMMI32 index)
{
    Host *h = (Host *)p;
    CHECK(index >= 0 && index < h->extra_count);
    event(h, 62);
    memmove(h->extras + index, h->extras + index + 1, (h->extra_count - index - 1) * sizeof(h->extras[0]));
    --h->extra_count;
}
static void clear(void *p)
{
    Host *h = (Host *)p;
    event(h, 63);
    h->extra_count = 0;
}
static void hilite(void *p, void *object, int value)
{
    Object *o = (Object *)object;
    CHECK(o);
    o->selected = value;
    event((Host *)p, (value ? 100 : 200) + o->id);
}
static int text_selected(void *p, void *o)
{
    (void)p;
    return ((Object *)o)->selected;
}
static void select_text(void *p, void *o, int value)
{
    Object *object = (Object *)o;
    object->selected = value;
    event((Host *)p, (value ? 300 : 400) + object->id);
}
static void invalidate(void *p)
{
    event((Host *)p, 50);
}
static void menu(void *p)
{
    event((Host *)p, 51);
}
static void notice(void *p)
{
    event((Host *)p, 52);
}
static ThreeMMI32 name(void *p, void *o, unsigned short *out)
{
    Object *object = (Object *)o;
    (void)p;
    memcpy(out, object->name, object->length * sizeof(*out));
    return object->length;
}
static int position(void *p, void *o, ThreeMMActorPoint *out)
{
    Object *object = (Object *)o;
    (void)p;
    if (!object->visible)
        return 0;
    *out = object->position;
    return 1;
}
static ThreeMMSceneSelectionServices init(Host *h, int view)
{
    ThreeMMSceneSelectionServices s;
    int i;
    memset(h, 0, sizeof(*h));
    memset(&s, 0, sizeof(s));
    h->mode = view;
    for (i = 0; i < 5; ++i)
        h->objects[i].id = i + 1;
    s.context = h;
    s.state = &h->selection;
    s.publish = set_selected;
    s.view_mode = mode;
    s.count = count;
    s.at = at;
    s.ensure_extras = ensure;
    s.append_extra = append;
    s.erase_extra = erase;
    s.clear_extras = clear;
    s.hilite = hilite;
    s.text_selected = text_selected;
    s.select_text = select_text;
    s.invalidate = invalidate;
    s.action_menu = menu;
    s.text_notice = notice;
    s.name = name;
    s.visible_position = position;
    return s;
}
static void trace(Host *h, const int *expected, int n)
{
    CHECK(h->trace_count == n);
    CHECK(!memcmp(h->trace, expected, n * sizeof(*expected)));
    h->trace_count = 0;
}
static void named(Object *o, const char *text)
{
    ThreeMMI32 i;
    o->length = (ThreeMMI32)strlen(text);
    CHECK(o->length <= 255);
    for (i = 0; i < o->length; ++i)
        o->name[i] = (unsigned char)text[i];
}
static void test_views(void)
{
    Host h;
    ThreeMMSceneSelectionServices s;
    int view;
    const int actor_trace[] = {201, 202, 63, 103, 404, 50, 40, 51};
    const int quiet_trace[] = {50, 40, 51};
    const int text_trace[] = {50, 201, 51, 202, 63, 404, 305, 41, 52};
    const int early_trace[] = {50, 201, 51, 63};
    const int text_quiet[] = {50, 41, 52};
    for (view = -1; view <= 1; ++view)
    {
        s = init(&h, view);
        h.selection.actor = h.objects;
        h.selection.text = h.objects + 3;
        h.extras[0] = h.objects + 1;
        h.extras[1] = NULL;
        h.extra_count = 2;
        h.allocated = 1;
        h.objects[0].selected = h.objects[1].selected = h.objects[3].selected = 1;
        three_mm_scene_select_actor(h.objects + 2, &s);
        CHECK(h.selection.actor == h.objects + 2 && h.selection.text == h.objects + 3);
        if (view == 0)
        {
            trace(&h, actor_trace, 8);
            CHECK(h.extra_count == 0 && !h.objects[3].selected);
        }
        else
        {
            trace(&h, quiet_trace, 3);
            CHECK(h.extra_count == 2 && h.objects[3].selected);
        }
        s = init(&h, view);
        h.selection.actor = h.objects;
        h.selection.text = h.objects + 3;
        h.extras[0] = h.objects + 1;
        h.extra_count = 1;
        h.objects[3].selected = 1;
        three_mm_scene_select_text(h.objects + 4, &s);
        CHECK(h.selection.text == h.objects + 4 && h.selection.actor == h.objects);
        if (view == 1)
        {
            trace(&h, text_trace, 9);
            CHECK(h.extra_count == 0 && !h.objects[3].selected && h.objects[4].selected);
            three_mm_scene_select_text(h.objects + 4, &s);
            trace(&h, early_trace, 4);
            h.objects[4].selected = 0;
            three_mm_scene_select_text(h.objects + 4, &s);
            CHECK(h.objects[4].selected && h.trace[h.trace_count - 1] == 52);
            h.trace_count = 0;
            h.selection.text = NULL;
            three_mm_scene_select_text(NULL, &s);
            trace(&h, early_trace, 4);
        }
        else
            trace(&h, text_quiet, 3);
    }
}
static void test_toggle(void)
{
    Host h;
    ThreeMMSceneSelectionServices s = init(&h, 0);
    const int first[] = {63, 101, 50, 40, 51};
    const int second[] = {60, 61, 102, 50, 51};
    const int promote[] = {201, 62, 40, 50, 51};
    const int remove[] = {203, 62, 50, 51};
    CHECK(!three_mm_scene_selection_contains(NULL, &s));
    CHECK(!three_mm_scene_selection_at(0, &s));
    CHECK(three_mm_scene_selection_toggle(h.objects, &s));
    trace(&h, first, 5);
    CHECK(three_mm_scene_selection_toggle(h.objects + 1, &s));
    trace(&h, second, 5);
    CHECK(three_mm_scene_selection_toggle(h.objects + 2, &s));
    h.trace_count = 0;
    CHECK(three_mm_scene_selection_count(&s) == 3);
    CHECK(three_mm_scene_selection_at(0, &s) == h.objects && three_mm_scene_selection_at(2, &s) == h.objects + 2);
    CHECK(!three_mm_scene_selection_at(-1, &s) && !three_mm_scene_selection_at(3, &s));
    CHECK(three_mm_scene_selection_toggle(h.objects, &s));
    trace(&h, promote, 5);
    CHECK(h.selection.actor == h.objects + 1 && h.extra_count == 1);
    CHECK(three_mm_scene_selection_toggle(h.objects + 2, &s));
    trace(&h, remove, 4);
    CHECK(three_mm_scene_selection_toggle(h.objects + 1, &s));
    CHECK(!h.selection.actor && three_mm_scene_selection_count(&s) == 0);
    s = init(&h, -1);
    h.selection.actor = h.objects;
    h.fail_allocate = 1;
    CHECK(!three_mm_scene_selection_toggle(h.objects + 1, &s));
    CHECK(h.selection.actor == h.objects && !h.allocated && h.trace_count == 1);
    h.trace_count = 0;
    h.fail_allocate = 0;
    h.fail_append = 1;
    CHECK(!three_mm_scene_selection_toggle(h.objects + 1, &s));
    CHECK(h.allocated && !h.extra_count && h.trace_count == 2);
    h.trace_count = 0;
    h.fail_append = 0;
    CHECK(three_mm_scene_selection_toggle(h.objects + 1, &s));
    CHECK(!h.objects[1].selected);
    three_mm_scene_select_actor(NULL, &s);
    CHECK(!three_mm_scene_selection_count(&s) && three_mm_scene_selection_contains(h.objects + 1, &s));
    CHECK(three_mm_scene_selection_toggle(h.objects + 1, &s));
    CHECK(!h.selection.actor && !h.extra_count);
}
static void test_bulk(void)
{
    Host h;
    ThreeMMSceneSelectionServices s;
    int i;
    s = init(&h, 0);
    CHECK(three_mm_scene_selection_all(&s) && !h.selection.actor);
    h.trace_count = 0;
    for (i = 0; i < 3; ++i)
        h.actors[i] = h.objects + i;
    h.actor_count = 3;
    h.fail_append = 2;
    CHECK(!three_mm_scene_selection_all(&s));
    CHECK(h.selection.actor == h.objects && h.extra_count == 1 && h.extras[0] == h.objects + 1);
    h.trace_count = 0;
    h.fail_append = 0;
    CHECK(three_mm_scene_selection_all(&s) && three_mm_scene_selection_count(&s) == 3);
    h.trace_count = 0;
    named(h.objects, "Alpha #TEAM! #two");
    named(h.objects + 1, "Beta #teamwork #Two");
    named(h.objects + 2, "Gamma #team");
    CHECK(three_mm_scene_selection_tag("TeAm", 4, 0, &s));
    CHECK(h.selection.actor == h.objects && h.extra_count == 1 && h.extras[0] == h.objects + 2);
    h.trace_count = 0;
    CHECK(three_mm_scene_selection_shared(h.objects, &s) && h.extra_count == 2);
    h.trace_count = 0;
    h.actors[1] = NULL;
    CHECK(three_mm_scene_selection_tag("two", 3, 0, &s) && h.selection.actor == h.objects && h.extra_count == 0);
    h.trace_count = 0;
    CHECK(three_mm_scene_selection_tag("", 0, 0, &s) && !h.selection.actor);
    h.trace_count = 0;
    CHECK(three_mm_scene_selection_tag("teamwork-longer-than-any-name", 28, 0, &s) && !h.selection.actor);
    h.trace_count = 0;
    named(h.objects, "no tags ## !");
    CHECK(three_mm_scene_selection_shared(h.objects, &s) && h.selection.actor == h.objects && !h.extra_count);
    h.trace_count = 0;
    h.actors[1] = h.objects + 1;
    named(h.objects, "#ab");
    named(h.objects + 1, "#AB_more");
    named(h.objects + 2, "#ab#two");
    CHECK(three_mm_scene_selection_shared(h.objects, &s) && h.extra_count == 1 && h.extras[0] == h.objects + 2);
    h.trace_count = 0;
    {
        const unsigned short tag[] = {'T', 'W', 'O'};
        CHECK(three_mm_scene_selection_tag(tag, 3, 1, &s) && h.selection.actor == h.objects + 2);
    }
    h.trace_count = 0;
    /* Native sharing truncates a long seed token before looking for a whole token. */
    for (i = 0; i < 65; ++i)
        h.objects[0].name[i] = i ? 'x' : '#';
    h.objects[0].length = 65;
    memcpy(h.objects[1].name, h.objects[0].name, 64 * sizeof(unsigned short));
    h.objects[1].length = 64;
    CHECK(three_mm_scene_selection_shared(h.objects, &s) && h.extra_count == 1 && h.extras[0] == h.objects + 1);
    h.trace_count = 0;
    named(h.objects, "#x");
    named(h.objects + 1, "#x");
    named(h.objects + 2, "#x");
    h.append_calls = 0;
    h.fail_append = 2;
    CHECK(!three_mm_scene_selection_shared(h.objects, &s) && h.extra_count == 1);
    h.trace_count = 0;
    h.append_calls = 0;
    CHECK(!three_mm_scene_selection_tag("x", 1, 0, &s) && h.extra_count == 1);
}
static void test_centroid(void)
{
    Host h;
    ThreeMMSceneSelectionServices s = init(&h, -1);
    ThreeMMActorPoint out = {11, 23, 47};
    CHECK(!three_mm_scene_selection_centroid(&out, &s) && out.dxr == 11 && out.dyr == 23 && out.dzr == 47);
    h.selection.actor = h.objects;
    h.extras[0] = h.objects + 1;
    h.extras[1] = NULL;
    h.extras[2] = h.objects + 2;
    h.extra_count = 3;
    CHECK(!three_mm_scene_selection_centroid(&out, &s) && out.dxr == 11);
    h.objects[0].visible = h.objects[2].visible = 1;
    h.objects[0].position.dxr = 65537;
    h.objects[2].position.dxr = 131072;
    h.objects[0].position.dyr = -65537;
    h.objects[2].position.dyr = -131072;
    CHECK(three_mm_scene_selection_centroid(&out, &s) && out.dxr == 98304 && out.dyr == -98304 && out.dzr == 0);
    h.objects[0].position.dxr = h.objects[2].position.dxr = 2147483647;
    CHECK(three_mm_scene_selection_centroid(&out, &s) && out.dxr == -1);
}
static void test_tag_units(void)
{
    Host h;
    ThreeMMSceneSelectionServices s = init(&h, 0);
    const unsigned char byte_tag[] = {0xe9};
    const unsigned short wide_tag[] = {0x1234};
    h.actors[0] = h.objects;
    h.actor_count = 1;
    h.objects[0].name[0] = '#';
    h.objects[0].name[1] = 0xe9;
    h.objects[0].length = 2;
    CHECK(three_mm_scene_selection_tag(byte_tag, 1, 0, &s) && h.selection.actor == h.objects);
    h.trace_count = 0;
    h.objects[0].name[1] = 0x1234;
    CHECK(three_mm_scene_selection_tag(wide_tag, 1, 1, &s) && h.selection.actor == h.objects);
    h.trace_count = 0;
    /* Query matching does not impose the shared-tag parser's ASCII grammar. */
    named(h.objects, "#a-b!");
    CHECK(three_mm_scene_selection_tag("a-b", 3, 0, &s) && h.selection.actor == h.objects);
}
int main(void)
{
    test_views();
    test_toggle();
    test_bulk();
    test_centroid();
    test_tag_units();
    puts("Scene selection policy passed");
    return 0;
}
