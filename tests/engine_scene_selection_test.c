#include "mm/scene_selection.h"
#include "scene_member_fixture.h"
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
    MMI32 length;
    MMActorPoint position;
} Object;
typedef struct Host
{
    MMSceneOwner owner;
    SceneMemberMemory members;
    Object objects[5];
    MMSceneSelection selection;
    int mode;
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
static MMI32 name(void *p, void *o, unsigned short *out)
{
    Object *object = (Object *)o;
    (void)p;
    memcpy(out, object->name, object->length * sizeof(*out));
    return object->length;
}
static int position(void *p, void *o, MMActorPoint *out)
{
    Object *object = (Object *)o;
    (void)p;
    if (!object->visible)
        return 0;
    *out = object->position;
    return 1;
}
static MMSceneSelectionServices init(Host *h, int view)
{
    MMSceneSelectionServices s;
    int i;
    memset(h, 0, sizeof(*h));
    memset(&s, 0, sizeof(s));
    init_scene_members(&h->owner, &h->members);
    h->mode = view;
    for (i = 0; i < 5; ++i)
        h->objects[i].id = i + 1;
    s.context = h;
    s.state = &h->selection;
    s.owner = &h->owner;
    s.publish = set_selected;
    s.view_mode = mode;
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
    MMI32 i;
    o->length = (MMI32)strlen(text);
    CHECK(o->length <= 255);
    for (i = 0; i < o->length; ++i)
        o->name[i] = (unsigned char)text[i];
}
static void test_views(void)
{
    Host h;
    MMSceneSelectionServices s;
    int view;
    const int actor_trace[] = {201, 202, 103, 404, 50, 40, 51};
    const int quiet_trace[] = {50, 40, 51};
    const int text_trace[] = {50, 201, 51, 202, 404, 305, 41, 52};
    const int early_trace[] = {50, 201, 51};
    const int text_quiet[] = {50, 41, 52};
    for (view = -1; view <= 1; ++view)
    {
        s = init(&h, view);
        h.selection.actor = h.objects;
        h.selection.text = h.objects + 3;
        CHECK(mm_scene_member_append(&h.owner, MM_SCENE_SELECTION, h.objects + 1));
        CHECK(mm_scene_member_append(&h.owner, MM_SCENE_SELECTION, NULL));
        h.objects[0].selected = h.objects[1].selected = h.objects[3].selected = 1;
        mm_scene_select_actor(h.objects + 2, &s);
        CHECK(h.selection.actor == h.objects + 2 && h.selection.text == h.objects + 3);
        if (view == 0)
        {
            trace(&h, actor_trace, 7);
            CHECK(mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 0 && !h.objects[3].selected);
        }
        else
        {
            trace(&h, quiet_trace, 3);
            CHECK(mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 2 && h.objects[3].selected);
        }
        s = init(&h, view);
        h.selection.actor = h.objects;
        h.selection.text = h.objects + 3;
        CHECK(mm_scene_member_append(&h.owner, MM_SCENE_SELECTION, h.objects + 1));
        h.objects[3].selected = 1;
        mm_scene_select_text(h.objects + 4, &s);
        CHECK(h.selection.text == h.objects + 4 && h.selection.actor == h.objects);
        if (view == 1)
        {
            trace(&h, text_trace, 8);
            CHECK(mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 0 && !h.objects[3].selected &&
                  h.objects[4].selected);
            mm_scene_select_text(h.objects + 4, &s);
            trace(&h, early_trace, 3);
            h.objects[4].selected = 0;
            mm_scene_select_text(h.objects + 4, &s);
            CHECK(h.objects[4].selected && h.trace[h.trace_count - 1] == 52);
            h.trace_count = 0;
            h.selection.text = NULL;
            mm_scene_select_text(NULL, &s);
            trace(&h, early_trace, 3);
        }
        else
            trace(&h, text_quiet, 3);
    }
}
static void test_toggle(void)
{
    Host h;
    MMSceneSelectionServices s = init(&h, 0);
    const int first[] = {101, 50, 40, 51};
    const int second[] = {102, 50, 51};
    const int promote[] = {201, 40, 50, 51};
    const int remove[] = {203, 50, 51};
    CHECK(!mm_scene_selection_contains(NULL, &s));
    CHECK(!mm_scene_selection_at(0, &s));
    CHECK(mm_scene_selection_toggle(h.objects, &s));
    trace(&h, first, 4);
    CHECK(mm_scene_selection_toggle(h.objects + 1, &s));
    trace(&h, second, 3);
    CHECK(mm_scene_selection_toggle(h.objects + 2, &s));
    h.trace_count = 0;
    CHECK(mm_scene_selection_count(&s) == 3);
    CHECK(mm_scene_selection_at(0, &s) == h.objects && mm_scene_selection_at(2, &s) == h.objects + 2);
    CHECK(!mm_scene_selection_at(-1, &s) && !mm_scene_selection_at(3, &s));
    CHECK(mm_scene_selection_toggle(h.objects, &s));
    trace(&h, promote, 4);
    CHECK(h.selection.actor == h.objects + 1 && mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 1);
    CHECK(mm_scene_selection_toggle(h.objects + 2, &s));
    trace(&h, remove, 3);
    CHECK(mm_scene_selection_toggle(h.objects + 1, &s));
    CHECK(!h.selection.actor && mm_scene_selection_count(&s) == 0);
    s = init(&h, -1);
    h.selection.actor = h.objects;
    h.members.fail_at = 1;
    CHECK(!mm_scene_selection_toggle(h.objects + 1, &s));
    CHECK(h.selection.actor == h.objects && !mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) && !h.trace_count);
    h.members.fail_at = 0;
    CHECK(mm_scene_selection_toggle(h.objects + 1, &s));
    CHECK(!h.objects[1].selected);
    mm_scene_select_actor(NULL, &s);
    CHECK(!mm_scene_selection_count(&s) && mm_scene_selection_contains(h.objects + 1, &s));
    CHECK(mm_scene_selection_toggle(h.objects + 1, &s));
    CHECK(!h.selection.actor && !mm_scene_member_count(&h.owner, MM_SCENE_SELECTION));
}
static void test_bulk(void)
{
    Host h;
    MMSceneSelectionServices s;
    int i;
    s = init(&h, 0);
    CHECK(mm_scene_selection_all(&s) && !h.selection.actor);
    h.trace_count = 0;
    for (i = 0; i < 3; ++i)
        CHECK(mm_scene_member_append(&h.owner, MM_SCENE_ACTORS, h.objects + i));
    h.members.fail_at = h.members.attempts + 2;
    CHECK(!mm_scene_selection_all(&s));
    CHECK(h.selection.actor == h.objects && mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 1 &&
          mm_scene_member_at(&h.owner, MM_SCENE_SELECTION, 0) == h.objects + 1);
    h.trace_count = 0;
    h.members.fail_at = 0;
    CHECK(mm_scene_selection_all(&s) && mm_scene_selection_count(&s) == 3);
    h.trace_count = 0;
    named(h.objects, "Alpha #TEAM! #two");
    named(h.objects + 1, "Beta #teamwork #Two");
    named(h.objects + 2, "Gamma #team");
    CHECK(mm_scene_selection_tag("TeAm", 4, 0, &s));
    CHECK(h.selection.actor == h.objects && mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 1 &&
          mm_scene_member_at(&h.owner, MM_SCENE_SELECTION, 0) == h.objects + 2);
    h.trace_count = 0;
    CHECK(mm_scene_selection_shared(h.objects, &s) && mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 2);
    h.trace_count = 0;
    mm_scene_member_replace(&h.owner, MM_SCENE_ACTORS, 1, NULL);
    CHECK(mm_scene_selection_tag("two", 3, 0, &s) && h.selection.actor == h.objects &&
          mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 0);
    h.trace_count = 0;
    CHECK(mm_scene_selection_tag("", 0, 0, &s) && !h.selection.actor);
    h.trace_count = 0;
    CHECK(mm_scene_selection_tag("teamwork-longer-than-any-name", 28, 0, &s) && !h.selection.actor);
    h.trace_count = 0;
    named(h.objects, "no tags ## !");
    CHECK(mm_scene_selection_shared(h.objects, &s) && h.selection.actor == h.objects &&
          !mm_scene_member_count(&h.owner, MM_SCENE_SELECTION));
    h.trace_count = 0;
    mm_scene_member_replace(&h.owner, MM_SCENE_ACTORS, 1, h.objects + 1);
    named(h.objects, "#ab");
    named(h.objects + 1, "#AB_more");
    named(h.objects + 2, "#ab#two");
    CHECK(mm_scene_selection_shared(h.objects, &s) && mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 1 &&
          mm_scene_member_at(&h.owner, MM_SCENE_SELECTION, 0) == h.objects + 2);
    h.trace_count = 0;
    {
        const unsigned short tag[] = {'T', 'W', 'O'};
        CHECK(mm_scene_selection_tag(tag, 3, 1, &s) && h.selection.actor == h.objects + 2);
    }
    h.trace_count = 0;
    /* Native sharing truncates a long seed token before looking for a whole token. */
    for (i = 0; i < 65; ++i)
        h.objects[0].name[i] = i ? 'x' : '#';
    h.objects[0].length = 65;
    memcpy(h.objects[1].name, h.objects[0].name, 64 * sizeof(unsigned short));
    h.objects[1].length = 64;
    CHECK(mm_scene_selection_shared(h.objects, &s) && mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 1 &&
          mm_scene_member_at(&h.owner, MM_SCENE_SELECTION, 0) == h.objects + 1);
    h.trace_count = 0;
    named(h.objects, "#x");
    named(h.objects + 1, "#x");
    named(h.objects + 2, "#x");
    mm_scene_member_clear(&h.owner, MM_SCENE_SELECTION);
    CHECK(mm_storage_dispose(&h.owner.members[MM_SCENE_SELECTION].storage) == MM_STORAGE_OK);
    h.members.fail_at = h.members.attempts + 2;
    CHECK(!mm_scene_selection_shared(h.objects, &s) && mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 1);
    h.trace_count = 0;
    CHECK(!mm_scene_selection_tag("x", 1, 0, &s) && mm_scene_member_count(&h.owner, MM_SCENE_SELECTION) == 1);
}
static void test_centroid(void)
{
    Host h;
    MMSceneSelectionServices s = init(&h, -1);
    MMActorPoint out = {11, 23, 47};
    CHECK(!mm_scene_selection_centroid(&out, &s) && out.dxr == 11 && out.dyr == 23 && out.dzr == 47);
    h.selection.actor = h.objects;
    CHECK(mm_scene_member_append(&h.owner, MM_SCENE_SELECTION, h.objects + 1));
    CHECK(mm_scene_member_append(&h.owner, MM_SCENE_SELECTION, NULL));
    CHECK(mm_scene_member_append(&h.owner, MM_SCENE_SELECTION, h.objects + 2));
    CHECK(!mm_scene_selection_centroid(&out, &s) && out.dxr == 11);
    h.objects[0].visible = h.objects[2].visible = 1;
    h.objects[0].position.dxr = 65537;
    h.objects[2].position.dxr = 131072;
    h.objects[0].position.dyr = -65537;
    h.objects[2].position.dyr = -131072;
    CHECK(mm_scene_selection_centroid(&out, &s) && out.dxr == 98304 && out.dyr == -98304 && out.dzr == 0);
    h.objects[0].position.dxr = h.objects[2].position.dxr = 2147483647;
    CHECK(mm_scene_selection_centroid(&out, &s) && out.dxr == -1);
}
static void test_tag_units(void)
{
    Host h;
    MMSceneSelectionServices s = init(&h, 0);
    const unsigned char byte_tag[] = {0xe9};
    const unsigned short wide_tag[] = {0x1234};
    CHECK(mm_scene_member_append(&h.owner, MM_SCENE_ACTORS, h.objects));
    h.objects[0].name[0] = '#';
    h.objects[0].name[1] = 0xe9;
    h.objects[0].length = 2;
    CHECK(mm_scene_selection_tag(byte_tag, 1, 0, &s) && h.selection.actor == h.objects);
    h.trace_count = 0;
    h.objects[0].name[1] = 0x1234;
    CHECK(mm_scene_selection_tag(wide_tag, 1, 1, &s) && h.selection.actor == h.objects);
    h.trace_count = 0;
    /* Query matching does not impose the shared-tag parser's ASCII grammar. */
    named(h.objects, "#a-b!");
    CHECK(mm_scene_selection_tag("a-b", 3, 0, &s) && h.selection.actor == h.objects);
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
