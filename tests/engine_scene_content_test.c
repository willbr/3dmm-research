#include "three_mm/scene_content.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "Scene content line %d\n", __LINE__);                                                      \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
typedef struct Obj
{
    int refs, id, text, ready, bound, roll, hidden, selected, first, last, life;
} Obj;
typedef struct Entry
{
    int kind;
    Obj *object;
} Entry;
typedef struct Group
{
    int count;
    Entry v[16];
} Group;
typedef struct Undo
{
    ThreeMMSceneActorUndo actor;
    ThreeMMSceneTextUndo text;
    int refs, istext;
} Undo;
typedef struct Test
{
    ThreeMMSceneContentServices s;
    ThreeMMSceneContentUndoServices us;
    ThreeMMSceneOwner owner;
    ThreeMMSceneState data;
    ThreeMMSceneRuntime runtime;
    Group starts, actors, texts, extras;
    Obj *selected[2];
    Undo *active;
    int live, undo_live, next_id, dirty, invalid, indicator, updated, placement, bind_fail, seek_actor_fail,
        seek_text_fail, append_fail, insert_fail, roll_fail, roll_calls;
    int onstage_fail, paste_fail, new_fail, commit_fail, clears, not_undoable, dup_fail, dups, switched, switch_fail,
        seek_calls, seek_fail, flushes, published, hide_text, issues[7];
    int log[512], used;
    ThreeMMSceneActorUndo cleared_actor;
    ThreeMMSceneTextUndo cleared_text;
    void **release_observer;
} Test;
static void note(Test *t, int n)
{
    CHECK(t->used < 512);
    t->log[t->used++] = n;
}
static Obj *new_obj(Test *t, int text)
{
    Obj *o = (Obj *)calloc(1, sizeof(*o));
    CHECK(o);
    o->refs = 1;
    o->id = -1;
    o->text = text;
    o->ready = 1;
    o->first = 3;
    o->last = 7;
    o->life = 1;
    ++t->live;
    return o;
}
static void retain(void *p, void *object)
{
    Obj *o = (Obj *)object;
    (void)p;
    CHECK(o && o->refs > 0);
    ++o->refs;
}
static void release(void *p, void *object)
{
    Test *t = (Test *)p;
    Obj *o = (Obj *)object;
    CHECK(o && o->refs > 0);
    if (t->release_observer)
        CHECK(*t->release_observer == object);
    note(t, 1);
    if (!--o->refs)
    {
        --t->live;
        free(o);
    }
}
static ThreeMMI32 count(void *p, void *g)
{
    (void)p;
    return ((Group *)g)->count;
}
static void *at(void *p, void *g, ThreeMMI32 i)
{
    (void)p;
    CHECK(i >= 0 && i < ((Group *)g)->count);
    return ((Group *)g)->v[i].object;
}
static void event(void *p, void *g, ThreeMMI32 i, ThreeMMI32 *frame, int *kind)
{
    (void)p;
    *frame = 0;
    *kind = ((Group *)g)->v[i].kind;
}
static void *read_object(void *p, void *value)
{
    (void)p;
    return *(void **)value;
}
static int insert(void *p, ThreeMMI32 i, int kind, void *object)
{
    Test *t = (Test *)p;
    CHECK(i >= 0 && i <= t->starts.count);
    if (t->insert_fail)
        return 0;
    memmove(t->starts.v + i + 1, t->starts.v + i, (size_t)(t->starts.count - i) * sizeof(Entry));
    t->starts.v[i].kind = kind;
    t->starts.v[i].object = (Obj *)object;
    ++t->starts.count;
    note(t, 2);
    return 1;
}
static void erase(void *p, void *value, ThreeMMI32 i)
{
    Test *t = (Test *)p;
    Group *g = (Group *)value;
    CHECK(i >= 0 && i < g->count);
    memmove(g->v + i, g->v + i + 1, (size_t)(g->count - i - 1) * sizeof(Entry));
    --g->count;
    note(t, 3);
}
static void put_start(void *p, ThreeMMI32 i, void *o)
{
    Test *t = (Test *)p;
    t->starts.v[i].object = (Obj *)o;
    note(t, 4);
}
static void put_list(void *p, void *g, ThreeMMI32 i, void *o)
{
    ((Group *)g)->v[i].object = (Obj *)o;
    note((Test *)p, 5);
}
static int append(void *p, int text, void *o)
{
    Test *t = (Test *)p;
    Group *g = text ? &t->texts : &t->actors;
    if (t->append_fail)
        return 0;
    g->v[g->count++].object = (Obj *)o;
    return 1;
}
static void remove_last(void *p, int text)
{
    Test *t = (Test *)p;
    Group *g = text ? &t->texts : &t->actors;
    CHECK(g->count > 0);
    --g->count;
}
static void bind(void *p, void *object)
{
    Test *t = (Test *)p;
    Obj *o = (Obj *)object;
    o->bound = 1;
    o->ready = !t->bind_fail;
    note(t, 6);
}
static void bind_text(void *p, void *o)
{
    ((Obj *)o)->bound = 1;
    note((Test *)p, 7);
}
static int seek_object(void *p, int text, void *o, ThreeMMI32 frame)
{
    Test *t = (Test *)p;
    (void)o;
    (void)frame;
    note(t, text ? 8 : 9);
    return !(text ? t->seek_text_fail : t->seek_actor_fail);
}
static int ready(void *p, void *o)
{
    (void)p;
    return ((Obj *)o)->ready;
}
static ThreeMMI32 actor_id(void *p, void *o)
{
    (void)p;
    return ((Obj *)o)->id;
}
static void set_id(void *p, void *o, ThreeMMI32 id)
{
    (void)p;
    ((Obj *)o)->id = id;
}
static int add_roll(void *p, void *o, int tmpl)
{
    Test *t = (Test *)p;
    Obj *a = (Obj *)o;
    (void)tmpl;
    if (a->id == -1)
        a->id = t->next_id++;
    ++t->roll_calls;
    if (t->roll_calls == t->roll_fail)
        return 0;
    ++a->roll;
    note(t, 10);
    return 1;
}
static void remove_roll(void *p, void *o, int only)
{
    Test *t = (Test *)p;
    Obj *a = (Obj *)o;
    (void)only;
    CHECK(a->roll > 0);
    --a->roll;
    note(t, 11);
}
static void *selected(void *p, int text)
{
    return ((Test *)p)->selected[text];
}
static void set_selected(void *p, int text, void *o)
{
    ((Test *)p)->selected[text] = (Obj *)o;
    note((Test *)p, 12);
}
static void select(void *p, int text, void *o)
{
    ((Test *)p)->selected[text] = (Obj *)o;
    note((Test *)p, 13);
}
static void end_place(void *p)
{
    ++((Test *)p)->placement;
}
static void hide_actor(void *p, void *o)
{
    ((Obj *)o)->hidden = 1;
    note((Test *)p, 14);
}
static int onstage(void *p, void *o)
{
    (void)o;
    return !((Test *)p)->onstage_fail;
}
static int paste(void *p, void *o, ThreeMMI32 frame)
{
    (void)o;
    CHECK(frame == 1);
    return !((Test *)p)->paste_fail;
}
static void dirty(void *p)
{
    ++((Test *)p)->dirty;
    note((Test *)p, 15);
}
static void invalidate(void *p)
{
    ++((Test *)p)->invalid;
}
static void indicator(void *p)
{
    ++((Test *)p)->indicator;
}
static void update(void *p)
{
    ++((Test *)p)->updated;
}
static void text_select(void *p, void *o, int value)
{
    ((Obj *)o)->selected = value;
    note((Test *)p, 16);
}
static int lifetime(void *p, void *o, ThreeMMI32 *first, ThreeMMI32 *last)
{
    Obj *a = (Obj *)o;
    (void)p;
    if (!a->life)
        return 0;
    *first = a->first;
    *last = a->last;
    return 1;
}
static void hide_text(void *p, void *o)
{
    (void)o;
    ++((Test *)p)->hide_text;
}
static void issue(void *p, int n)
{
    Test *t = (Test *)p;
    CHECK(n >= 0 && n < 7);
    ++t->issues[n];
}
static void *new_undo(void *p, int text)
{
    Test *t = (Test *)p;
    Undo *u;
    if (t->new_fail)
        return NULL;
    u = (Undo *)calloc(1, sizeof(*u));
    CHECK(u);
    u->refs = 1;
    u->istext = text;
    ++t->undo_live;
    return u;
}
static ThreeMMSceneActorUndo *actor_state(void *p, void *u)
{
    (void)p;
    return &((Undo *)u)->actor;
}
static ThreeMMSceneTextUndo *text_state(void *p, void *u)
{
    (void)p;
    return &((Undo *)u)->text;
}
static void release_undo(void *p, void *value)
{
    Test *t = (Test *)p;
    Undo *u = (Undo *)value;
    CHECK(u->refs > 0);
    if (!--u->refs)
    {
        if (u->istext)
            three_mm_scene_text_undo_dispose(&u->text, &t->s);
        else
            three_mm_scene_actor_undo_dispose(&u->actor, &t->s);
        --t->undo_live;
        free(u);
    }
}
static int commit(void *p, void *value)
{
    Test *t = (Test *)p;
    Undo *u = (Undo *)value;
    if (t->commit_fail)
        return 0;
    CHECK(!t->active);
    t->active = u;
    ++u->refs;
    return 1;
}
static void clear(void *p)
{
    Test *t = (Test *)p;
    Undo *u = t->active;
    ++t->clears;
    t->active = NULL;
    note(t, 20);
    if (u)
    {
        t->cleared_actor = u->actor;
        t->cleared_text = u->text;
        release_undo(t, u);
    }
}
static void not_undoable(void *p)
{
    ++((Test *)p)->not_undoable;
}
static int switch_scene(void *p)
{
    Test *t = (Test *)p;
    ++t->switched;
    return !t->switch_fail;
}
static int seek(void *p, ThreeMMI32 frame)
{
    Test *t = (Test *)p;
    ++t->seek_calls;
    note(t, 21);
    t->data.currentFrame = frame;
    return t->seek_calls != t->seek_fail;
}
static ThreeMMSceneContentServices scene(void *p)
{
    return ((Test *)p)->s;
}
static void *duplicate(void *p, void *object)
{
    Test *t = (Test *)p;
    Obj *o = (Obj *)object, *copy;
    if (++t->dups == t->dup_fail)
        return NULL;
    copy = new_obj(t, 0);
    *copy = *o;
    copy->refs = 1;
    copy->roll = 0;
    copy->bound = 0;
    copy->ready = 0;
    return copy;
}
static void flush(void *p)
{
    Test *t = (Test *)p;
    ++t->flushes;
    note(t, 22);
}
static void publish(void *p, void *value, const ThreeMMSceneOwner *owner)
{
    Test *t = (Test *)p;
    CHECK(value == t && owner == &t->owner);
    ++t->published;
}
static void init(Test *t)
{
    ThreeMMSceneContentServices *s;
    ThreeMMSceneEventServices *v;
    ThreeMMSceneOwnerServices *o;
    memset(t, 0, sizeof(*t));
    t->next_id = 100;
    t->data.currentFrame = t->data.firstFrame = 1;
    t->data.lastFrame = 9;
    t->owner.owned[1] = &t->starts;
    t->owner.owned[2] = &t->actors;
    t->owner.owned[3] = &t->texts;
    t->owner.owned[4] = &t->extras;
    s = &t->s;
    s->context = t;
    v = &s->events;
    v->context = v->scene = t;
    v->owner = &t->owner;
    v->data = &t->data;
    v->runtime = &t->runtime;
    v->event = event;
    v->read_object = read_object;
    v->append = append;
    v->remove_last = remove_last;
    v->bind_actor = bind;
    v->seek = seek_object;
    o = &v->owners;
    o->context = t;
    o->count = count;
    o->list_at = at;
    o->object = at;
    o->release = release;
    o->publish = publish;
    s->retain = retain;
    s->actor_id = actor_id;
    s->set_actor_id = set_id;
    s->actor_ready = ready;
    s->actor_onstage = onstage;
    s->actor_paste = paste;
    s->hide_actor = hide_actor;
    s->insert_start = insert;
    s->put_start = put_start;
    s->put_list = put_list;
    s->erase = erase;
    s->selected = selected;
    s->set_selected = set_selected;
    s->select = select;
    s->end_placement = end_place;
    s->add_roll = add_roll;
    s->remove_roll = remove_roll;
    s->dirty = dirty;
    s->invalidate = invalidate;
    s->indicator = indicator;
    s->update_roll = update;
    s->bind_text = bind_text;
    s->text_select = text_select;
    s->text_lifetime = lifetime;
    s->hide_text = hide_text;
    s->new_undo = new_undo;
    s->actor_state = actor_state;
    s->text_state = text_state;
    s->commit_undo = commit;
    s->release_undo = release_undo;
    s->clear_undo = clear;
    s->not_undoable = not_undoable;
    s->diagnostic = issue;
    t->us.context = t;
    t->us.switch_scene = switch_scene;
    t->us.seek = seek;
    t->us.frame = 1;
    t->us.scene = scene;
    t->us.duplicate = duplicate;
    t->us.flush = flush;
    t->us.clear = clear;
    t->us.update_roll = update;
}
static void clean(Test *t)
{
    clear(t);
    while (t->starts.count)
    {
        Obj *o = t->starts.v[--t->starts.count].object;
        release(t, o);
    }
    CHECK(!t->live && !t->undo_live);
}
static void test_actor(void)
{
    Test t;
    Obj *a, *b, *extra;
    int failure;
    init(&t);
    a = new_obj(&t, 0);
    CHECK(three_mm_scene_actor_add(a, &t.s) && a->refs == 2 && a->id == 100 && t.actors.count == 1 &&
          t.starts.count == 1 && t.indicator == 1);
    b = new_obj(&t, 0);
    b->id = a->id;
    t.extras.v[0].object = a;
    t.extras.count = 1;
    CHECK(three_mm_scene_actor_add(b, &t.s) && a->refs == 1 && a->hidden && b->refs == 2 && t.selected[0] == b &&
          t.actors.v[0].object == b);
    CHECK(!t.extras.count);
    extra = new_obj(&t, 0);
    extra->id = b->id;
    t.extras.v[0].object = b;
    t.extras.v[1].object = NULL;
    t.extras.v[2].object = extra;
    t.extras.count = 3;
    b->roll = 1;
    three_mm_scene_actor_remove(b->id, &t.s);
    CHECK(!t.starts.count && !t.actors.count && t.extras.count == 1 && t.placement == 1 && b->refs == 1);
    release(&t, a);
    release(&t, b);
    release(&t, extra);
    clean(&t);
    for (failure = 0; failure < 5; ++failure)
    {
        init(&t);
        a = new_obj(&t, 0);
        t.insert_fail = failure == 0;
        t.roll_fail = failure == 1 ? 1 : 0;
        t.append_fail = failure == 2;
        t.seek_actor_fail = failure == 3;
        t.bind_fail = failure == 4;
        CHECK(!three_mm_scene_actor_add(a, &t.s) && a->refs == 1 && !t.starts.count && !t.actors.count && !a->roll &&
              !t.dirty);
        release(&t, a);
        clean(&t);
    }
    init(&t);
    a = new_obj(&t, 0);
    CHECK(three_mm_scene_actor_add(a, &t.s));
    b = new_obj(&t, 0);
    b->id = a->id;
    t.seek_actor_fail = 1;
    CHECK(!three_mm_scene_actor_add(b, &t.s) && b->refs == 1 && t.actors.v[0].object == a);
    release(&t, b);
    release(&t, a);
    clean(&t);
    init(&t);
    a = new_obj(&t, 0);
    t.onstage_fail = 1;
    CHECK(!three_mm_scene_actor_add_onstage(a, &t.s) && a->refs == 1 && !t.actors.count);
    release(&t, a);
    clean(&t);
    init(&t);
    a = new_obj(&t, 0);
    CHECK(three_mm_scene_actor_paste(a, &t.s) && t.updated == 1 && t.invalid == 2 && t.indicator == 2);
    release(&t, a);
    clean(&t);
}
static void test_text(void)
{
    Test t;
    Obj *a;
    int fail;
    for (fail = 0; fail < 3; ++fail)
    {
        init(&t);
        a = new_obj(&t, 1);
        t.insert_fail = fail == 0;
        t.append_fail = fail == 1;
        t.seek_text_fail = fail == 2;
        CHECK(!three_mm_scene_text_add(a, &t.s) && a->refs == 1 && !t.starts.count && !t.texts.count && a->bound);
        release(&t, a);
        clean(&t);
    }
    init(&t);
    a = new_obj(&t, 1);
    CHECK(three_mm_scene_text_add(a, &t.s) && a->refs == 2);
    t.selected[1] = a;
    a->selected = 1;
    CHECK(three_mm_scene_text_remove(a, &t.s) && a->refs == 1 && !t.texts.count && !t.selected[1] && !a->selected);
    release(&t, a);
    clean(&t);
    init(&t);
    a = new_obj(&t, 1);
    a->last = 9;
    CHECK(three_mm_scene_text_add_undo(a, &t.s) && t.active->text.last == 2147483647L && t.active->text.index == 0 &&
          a->refs == 3);
    clear(&t);
    t.commit_fail = 1;
    CHECK(!three_mm_scene_text_remove_undo(a, &t.s) && a->refs == 2 && t.texts.count == 1 && !t.undo_live);
    release(&t, a);
    clean(&t);
    init(&t);
    a = new_obj(&t, 1);
    CHECK(three_mm_scene_text_add(a, &t.s));
    t.texts.count = 0;
    CHECK(!three_mm_scene_text_remove(a, &t.s) && a->refs == 1 && !t.starts.count && t.issues[3] == 1);
    release(&t, a);
    clean(&t);
    init(&t);
    a = new_obj(&t, 1);
    t.commit_fail = 1;
    CHECK(!three_mm_scene_text_add_undo(a, &t.s) && t.texts.count == 1 && a->refs == 2 && t.not_undoable == 1 &&
          !t.undo_live);
    release(&t, a);
    clean(&t);
    init(&t);
    a = new_obj(&t, 1);
    a->life = 0;
    CHECK(!three_mm_scene_text_add_undo(a, &t.s) && a->refs == 2 && t.texts.count == 1 && !t.undo_live);
    release(&t, a);
    clean(&t);
    init(&t);
    CHECK(!three_mm_scene_text_at(0, &t.s) && !three_mm_scene_text_at(-1, &t.s));
    clean(&t);
}
static void test_public(void)
{
    Test t;
    Obj *a, *b;
    init(&t);
    a = new_obj(&t, 0);
    CHECK(three_mm_scene_actor_add(a, &t.s));
    t.commit_fail = 1;
    CHECK(!three_mm_scene_actor_remove_undo(a->id, &t.s) && a->refs == 2 && !t.undo_live && t.actors.count == 1);
    release(&t, a);
    clean(&t);
    init(&t);
    CHECK(!three_mm_scene_actor_remove_undo(17, &t.s) && t.issues[0] == 1);
    clean(&t);
    init(&t);
    a = new_obj(&t, 0);
    b = new_obj(&t, 0);
    CHECK(three_mm_scene_actor_add(a, &t.s) && three_mm_scene_actor_add(b, &t.s));
    three_mm_scene_roll_remove(0, &t.s);
    CHECK(!a->roll && !b->roll);
    t.roll_calls = 0;
    t.roll_fail = 2;
    CHECK(!three_mm_scene_roll_add(&t.s) && !a->roll && !b->roll);
    release(&t, a);
    release(&t, b);
    clean(&t);
    init(&t);
    three_mm_scene_content_bind_movie(&t, &t.s);
    CHECK(t.owner.movie == &t && t.published == 1);
    clean(&t);
}
static void test_actor_undo(void)
{
    Test t;
    Obj *a, *saved;
    Undo *u;
    int fail;
    init(&t);
    a = new_obj(&t, 0);
    u = (Undo *)new_undo(&t, 0);
    u->actor.actor = a;
    u->actor.operation = THREE_MM_SCENE_ACTOR_ADD;
    t.active = u;
    CHECK(three_mm_scene_actor_undo(&u->actor, 1, &t.us) && ((Obj *)u->actor.actor)->id == 100 &&
          t.actors.v[0].object->id == 100);
    CHECK(three_mm_scene_actor_undo(&u->actor, 0, &t.us) && !t.actors.count && t.flushes == 2 && t.updated == 2);
    clean(&t);
    for (fail = 0; fail < 4; ++fail)
    {
        init(&t);
        a = new_obj(&t, 0);
        u = (Undo *)new_undo(&t, 0);
        u->actor.actor = a;
        u->actor.operation = 1;
        t.active = u;
        t.switch_fail = fail == 0;
        t.seek_fail = fail == 1 ? 1 : 0;
        t.insert_fail = fail == 2;
        t.dup_fail = fail == 3 ? 1 : 0;
        CHECK(!three_mm_scene_actor_undo(&u->actor, 1, &t.us) && !t.active && !t.undo_live && t.flushes == 1 &&
              t.log[t.used - 3] == 22 && t.log[t.used - 2] == 20);
        clean(&t);
    }
    init(&t);
    a = new_obj(&t, 0);
    CHECK(three_mm_scene_actor_add(a, &t.s));
    saved = new_obj(&t, 0);
    saved->id = a->id;
    u = (Undo *)new_undo(&t, 0);
    u->actor.actor = saved;
    u->actor.operation = 3;
    t.active = u;
    t.seek_actor_fail = 1;
    CHECK(!three_mm_scene_actor_undo(&u->actor, 1, &t.us) && !t.undo_live && t.live == 1 && a->refs == 2);
    release(&t, a);
    clean(&t);
    init(&t);
    a = new_obj(&t, 0);
    a->id = 7;
    u = (Undo *)new_undo(&t, 0);
    u->actor.actor = a;
    u->actor.operation = 2;
    t.active = u;
    CHECK(three_mm_scene_actor_undo(&u->actor, 0, &t.us) && t.actors.count == 1 && u->actor.actor == a);
    clean(&t);
}
static void test_text_undo(void)
{
    Test t;
    Obj *a, *b;
    Undo *u;
    int redo, fail;
    for (redo = 0; redo < 2; ++redo)
    {
        init(&t);
        a = new_obj(&t, 1);
        u = (Undo *)new_undo(&t, 1);
        u->text.text = a;
        u->text.add = redo;
        u->text.first = 3;
        u->text.last = 7;
        u->text.index = 5;
        t.active = u;
        CHECK(three_mm_scene_text_undo(&u->text, redo, &t.us) && u->text.index == 0 && t.selected[1] == a &&
              t.hide_text == 1 && t.seek_calls == 2);
        clean(&t);
    }
    for (fail = 1; fail <= 2; ++fail)
    {
        init(&t);
        a = new_obj(&t, 1);
        u = (Undo *)new_undo(&t, 1);
        u->text.text = a;
        u->text.add = 1;
        u->text.first = 3;
        u->text.last = 7;
        u->text.index = 5;
        t.active = u;
        t.seek_fail = fail;
        if (fail == 1)
            CHECK(!three_mm_scene_text_undo(&u->text, 1, &t.us) && !t.active && !t.live);
        else
            CHECK(three_mm_scene_text_undo(&u->text, 1, &t.us) && u->text.index == 5 && t.texts.count == 1 &&
                  !t.clears);
        clean(&t);
    }
    for (redo = 0; redo < 2; ++redo)
    {
        init(&t);
        a = new_obj(&t, 1);
        u = (Undo *)new_undo(&t, 1);
        u->text.text = a;
        u->text.add = redo;
        u->text.first = 3;
        u->text.last = 7;
        t.active = u;
        t.insert_fail = 1;
        CHECK(!three_mm_scene_text_undo(&u->text, redo, &t.us) && t.seek_calls == (redo ? 2 : 1) && !t.active &&
              !t.live);
        clean(&t);
    }
    init(&t);
    a = new_obj(&t, 1);
    b = new_obj(&t, 1);
    CHECK(three_mm_scene_text_add(b, &t.s));
    u = (Undo *)new_undo(&t, 1);
    u->text.text = a;
    u->text.add = 1;
    u->text.index = 0;
    t.active = u;
    CHECK(three_mm_scene_text_undo(&u->text, 0, &t.us) && b->refs == 1 && a->refs == 1 && !t.texts.count);
    release(&t, b);
    clean(&t);
    init(&t);
    a = new_obj(&t, 1);
    u = (Undo *)new_undo(&t, 1);
    u->text.text = a;
    u->text.add = 1;
    u->text.index = 7;
    t.active = u;
    CHECK(!three_mm_scene_text_undo(&u->text, 0, &t.us) && !t.active && !t.live);
    clean(&t);
}
static void test_dispose(void)
{
    Test t;
    ThreeMMSceneActorUndo a;
    ThreeMMSceneTextUndo b;
    init(&t);
    a.actor = new_obj(&t, 0);
    t.release_observer = &a.actor;
    three_mm_scene_actor_undo_dispose(&a, &t.s);
    CHECK(!a.actor);
    b.text = new_obj(&t, 1);
    t.release_observer = &b.text;
    three_mm_scene_text_undo_dispose(&b, &t.s);
    CHECK(!b.text);
    t.release_observer = NULL;
    clean(&t);
}
int main(void)
{
    test_actor();
    test_text();
    test_public();
    test_actor_undo();
    test_text_undo();
    test_dispose();
    puts("C89 Scene actor/caption checks passed");
    return 0;
}
