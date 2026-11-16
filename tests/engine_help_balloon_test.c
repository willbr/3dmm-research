#include "mm/help_balloon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "help balloon failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(c) check(!!(c), __LINE__)
typedef struct Node
{
    int id, refs;
    MMHelpButton button;
} Node;
typedef struct Host
{
    MMHelpBalloonFactoryServices factory;
    MMHelpBalloonReadServices read;
    MMHelpBalloonTopicServices topic;
    MMHelpBalloonStages stages;
    MMHelpBalloonLayoutServices layout;
    MMHelpButtonFactoryServices button_factory;
    MMHelpButtonServices button;
    MMHelpBalloon state;
    MMHelpTopic info;
    Node nodes[5];
    MMHelpButton *heap;
    MMMaskedRect own, parent, content, positioned, child;
    MMI32 width, height, x, y;
    int exists, balloon, same, partial, hidden, dead, kid, parent_valid, base_hit, found, mutation;
    int diagnostic, which, positions, child_positions, result, occurrence, seen;
    unsigned group;
    MMU32 target;
    char log[512], fail;
    unsigned used;
} Host;
static int effect(Host *h, char tag)
{
    CHECK(h->used < 511);
    h->log[h->used++] = tag;
    h->log[h->used] = 0;
    return h->fail == tag && ++h->seen == h->occurrence ? h->result : 1;
}
static int eq(const MMMaskedRect *r, int l, int t, int right, int b)
{
    return r->left == l && r->top == t && r->right == right && r->bottom == b;
}
static int source(void *v, MMHelpTopic *out)
{
    Host *h = (Host *)v;
    *out = h->info;
    return effect(h, 'G');
}
static int unique(void *v, MMI32 *out)
{
    *out = 100;
    return effect((Host *)v, 'U');
}
static int find_node(void *v, MMI32 id, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'F');
    CHECK(id == 100);
    *out = h->exists ? &h->nodes[0] : 0;
    return r <= 0 ? r : h->exists;
}
static int is_balloon(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[0]);
    return h->balloon;
}
static int same_source(void *v, void *p, MMU32 n)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[0] && n == 30);
    effect(h, 'Q');
    return h->same;
}
static int replace(void *v, void *p, const MMHelpTopic *t)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[0] && t->balloon == 30);
    return effect(h, 'R');
}
static int create_node(void *v, MMI32 id, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'C');
    CHECK(id == 100 && !h->nodes[1].refs);
    if (r > 0 || h->partial)
    {
        h->nodes[1].refs = 1;
        *out = &h->nodes[1];
    }
    return r;
}
static void snapshot(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[1]);
    effect(h, 's');
}
static int initialize(void *v, void *p, const MMHelpTopic *t)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[1] && t->hid == 100 && t->balloon == 30);
    return effect(h, 'I');
}
static int enter(void *v, void *p)
{
    Host *h = (Host *)v;
    int r = effect(h, 'E');
    CHECK(p == &h->nodes[1] && h->nodes[1].refs);
    if (!r)
    {
        h->nodes[1].refs = 0;
        h->dead = 1;
    }
    return r;
}
static int hit_node(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[1] && h->nodes[1].refs);
    if (h->fail == 'A' && h->result == 0)
    {
        h->nodes[1].refs = 0;
        h->dead = 1;
    }
    return effect(h, 'H');
}
static int alive(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[1]);
    return effect(h, 'A');
}
static void release(void *v, void *p)
{
    Host *h = (Host *)v;
    Node *n = (Node *)p;
    CHECK(n >= h->nodes && n < h->nodes + 5 && n->refs == 1);
    --n->refs;
    effect(h, "onvwd"[n->id]);
}
static void diagnostic(void *v, int kind)
{
    Host *h = (Host *)v;
    h->diagnostic = kind + 1;
    effect(h, '!');
}
static void validate(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[1] && h->nodes[1].refs && !h->dead);
    effect(h, 'V');
}
static int read_find(void *v, MMU32 n, void **out)
{
    Host *h = (Host *)v;
    CHECK(n == 17);
    *out = &h->nodes[0];
    return effect(h, 'F');
}
static int read_doc(void *v, void *p, MMU32 n, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'R');
    CHECK(p == &h->nodes[0] && n == 17);
    if (r > 0 || h->partial)
    {
        *out = &h->nodes[4];
        h->nodes[4].refs = 1;
    }
    return r;
}
static int hide(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[4] && h->nodes[4].refs == 1);
    h->hidden = 1;
    return effect(h, 'H');
}
static int build(void *v, void *p, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'B');
    CHECK(p == &h->nodes[4] && h->hidden);
    if (r > 0)
        *out = &h->nodes[1];
    return r;
}
static int topic_create(void *v, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'C');
    CHECK(h->state.topic == &h->nodes[2] && h->nodes[2].refs == 1);
    if (r >= 0 || h->partial)
    {
        *out = r > 0 || h->partial ? &h->nodes[3] : 0;
        if (*out)
            h->nodes[3].refs = 1;
    }
    return r;
}
static void info(void *v, MMHelpTopic *out)
{
    *out = ((Host *)v)->info;
}
static int assign(void *v, void *p, int number, MMU32 value)
{
    Host *h = (Host *)v;
    CHECK(p == h->state.topic && p == &h->nodes[3]);
    CHECK(value == (number ? h->info.sound_number : h->info.sound_type));
    if (h->mutation == 'S' && !number)
        h->info.sound_number = 55;
    return effect(h, number ? 'U' : 'S');
}
static int natural(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[3] && !h->nodes[2].refs);
    if (h->mutation == 'N')
        h->info.target = 0;
    return effect(h, 'N');
}
static int target_find(void *v, MMI32 id, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'F');
    CHECK(id == 5);
    *out = &h->nodes[0];
    return r;
}
static void *parent_node(void *v)
{
    return &((Host *)v)->nodes[0];
}
static int kid(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[0]);
    effect(h, 'K');
    return h->kid;
}
static int registration(void *v, void *p, MMHelpPoint *out)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[0]);
    out->x = 7;
    out->y = 9;
    return effect(h, 'J');
}
static int target_bounds(void *v, void *p, MMMaskedRect *out)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[0]);
    *out = h->parent;
    return effect(h, 'B');
}
static int map_target(void *v, void *p, MMHelpPoint *pt)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[0]);
    pt->x += 10;
    pt->y += 20;
    return effect(h, 'M');
}
static int map_parent(void *v, MMHelpPoint *pt)
{
    pt->x -= 4;
    pt->y -= 6;
    return effect((Host *)v, 'P');
}
static int own_registration(void *v, MMHelpPoint *pt)
{
    Host *h = (Host *)v;
    pt->x = 2;
    pt->y = 3;
    if (h->mutation == 'O')
        h->info.width = 40;
    return effect(h, 'O');
}
static int representation(void *v, MMI32 x, MMI32 y)
{
    Host *h = (Host *)v;
    h->x = x;
    h->y = y;
    return effect(h, 'D');
}
static int stage_base(void *v)
{
    return effect((Host *)v, 'B');
}
static int stage_set(void *v)
{
    return effect((Host *)v, 'S');
}
static int stage_enter(void *v)
{
    return effect((Host *)v, 'E');
}
static int layout_base(void *v, MMI32 x, MMI32 y)
{
    CHECK(x == 2 && y == 3);
    return effect((Host *)v, 'D');
}
static int layout_bounds(void *v, int kind, MMMaskedRect *out)
{
    Host *h = (Host *)v;
    *out = kind == 0 ? h->own : kind == 1 ? h->parent : h->content;
    return effect(h, "abc"[kind]);
}
static int position(void *v, int child, const MMMaskedRect *r)
{
    Host *h = (Host *)v;
    if (child)
    {
        h->child = *r;
        ++h->child_positions;
    }
    else
    {
        h->positioned = *r;
        ++h->positions;
        if (h->mutation == 'p')
        {
            h->width = 13;
            h->height = 9;
        }
    }
    return effect(h, child ? 'q' : 'p');
}
static void preferred(void *v, MMI32 *w, MMI32 *hgt)
{
    Host *h = (Host *)v;
    *w = h->width;
    *hgt = h->height;
    effect(h, 'z');
}
static int button_find(void *v, MMI32 id)
{
    Host *h = (Host *)v;
    int r = effect(h, 'F');
    CHECK(id == 100);
    return r <= 0 ? r : h->exists;
}
static MMHelpButton *button_state(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[1]);
    return &h->nodes[1].button;
}
static int button_initialize(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[1] && h->nodes[1].button.group == 3 && h->nodes[1].button.topic == 99);
    return effect(h, 'I');
}
static int button_bounds(void *v, void *p, MMMaskedRect *out)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[1]);
    *out = h->own;
    return effect(h, 'B');
}
static int button_position(void *v, void *p, const MMMaskedRect *r)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[1]);
    h->positioned = *r;
    return effect(h, 'P');
}
static int base_point(void *v, MMI32 x, MMI32 y)
{
    Host *h = (Host *)v;
    int r = effect(h, 'B');
    CHECK(x == 2 && y == 3);
    return r <= 0 ? r : h->base_hit;
}
static int parent_topic(void *v, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'P');
    *out = &h->nodes[0];
    return r <= 0 ? r : h->parent_valid;
}
static int map_point(void *v, MMHelpPoint *p)
{
    CHECK(p->x == 2 && p->y == 3);
    p->x += 5;
    p->y += 6;
    return effect((Host *)v, 'M');
}
static int group(void *v, void *node, const MMHelpPoint *p, unsigned *g, MMU32 *t)
{
    Host *h = (Host *)v;
    int r = effect(h, 'G');
    CHECK(node == &h->nodes[0] && p->x == 7 && p->y == 9);
    *g = h->group;
    *t = h->target;
    if (h->mutation == 'G')
        ++h->nodes[1].button.group;
    return r <= 0 ? r : h->found;
}
static MMI32 id(void *v)
{
    effect((Host *)v, 'i');
    return 100;
}
static MMU32 flags(void *v)
{
    effect((Host *)v, 'f');
    return 7;
}
static int clicked(void *v, void *p, unsigned g, MMU32 t, MMU32 f, MMI32 id_value)
{
    Host *h = (Host *)v;
    CHECK(p == &h->nodes[0] && g == 3 && t == 99 && f == 7 && id_value == 100);
    if (h->heap)
    {
        free(h->heap);
        h->heap = 0;
    }
    return effect(h, 'H');
}
static void init(Host *h)
{
    int i;
    memset(h, 0, sizeof(*h));
    for (i = 0; i < 5; ++i)
        h->nodes[i].id = i;
    h->nodes[0].refs = h->nodes[2].refs = 1;
    h->balloon = h->same = h->parent_valid = h->found = 1;
    h->result = -1;
    h->occurrence = 1;
    h->group = 3;
    h->target = 99;
    h->state.topic = &h->nodes[2];
    h->nodes[1].button.group = 3;
    h->nodes[1].button.topic = 99;
    mm_help_topic_init(&h->info);
    h->info.balloon = 30;
    h->info.hid = 100;
    h->info.target = 5;
    h->info.width = 20;
    h->info.height = 13;
    h->info.sound_type = 0x534e4449UL;
    h->info.sound_number = 17;
    h->own.left = -3;
    h->own.top = 2;
    h->own.right = 5;
    h->own.bottom = 7;
    h->parent.left = -7;
    h->parent.top = -5;
    h->parent.right = 4;
    h->parent.bottom = 2;
    h->content.left = 3;
    h->content.top = 5;
    h->content.right = 45;
    h->content.bottom = 25;
    h->width = 13;
    h->height = 9;
    h->factory.context = h;
    h->factory.source = source;
    h->factory.unique = unique;
    h->factory.find = find_node;
    h->factory.is_balloon = is_balloon;
    h->factory.same_source = same_source;
    h->factory.replace = replace;
    h->factory.create = create_node;
    h->factory.snapshot = snapshot;
    h->factory.initialize = initialize;
    h->factory.enter = enter;
    h->factory.hit = hit_node;
    h->factory.alive = alive;
    h->factory.release = release;
    h->factory.diagnostic = diagnostic;
    h->factory.validate = validate;
    h->read.context = h;
    h->read.find = read_find;
    h->read.read = read_doc;
    h->read.hide = hide;
    h->read.build = build;
    h->read.release = release;
    h->topic.context = h;
    h->topic.create = topic_create;
    h->topic.assign = assign;
    h->topic.info = info;
    h->topic.release = release;
    h->topic.natural = natural;
    h->topic.find = target_find;
    h->topic.parent = parent_node;
    h->topic.kid = kid;
    h->topic.registration = registration;
    h->topic.bounds = target_bounds;
    h->topic.map_target = map_target;
    h->topic.map_parent = map_parent;
    h->topic.own_registration = own_registration;
    h->topic.representation = representation;
    h->stages.context = h;
    h->stages.base = stage_base;
    h->stages.set = stage_set;
    h->stages.enter = stage_enter;
    h->layout.context = h;
    h->layout.base = layout_base;
    h->layout.bounds = layout_bounds;
    h->layout.position = position;
    h->layout.preferred = preferred;
    h->button_factory.context = h;
    h->button_factory.find = button_find;
    h->button_factory.create = create_node;
    h->button_factory.state = button_state;
    h->button_factory.initialize = button_initialize;
    h->button_factory.enter = enter;
    h->button_factory.bounds = button_bounds;
    h->button_factory.position = button_position;
    h->button_factory.release = release;
    h->button_factory.diagnostic = diagnostic;
    h->button_factory.validate = validate;
    h->button.context = h;
    h->button.base_point = base_point;
    h->button.parent_topic = parent_topic;
    h->button.map = map_point;
    h->button.group = group;
    h->button.id = id;
    h->button.flags = flags;
    h->button.hit = clicked;
    h->button.diagnostic = diagnostic;
}
static void merges(void)
{
    Host h;
    MMHelpTopic base, p;
    unsigned bits;
    init(&h);
    base = h.info;
    for (bits = 0; bits < 256; ++bits)
    {
        p = base;
        mm_help_topic_init(&h.info);
        h.info.balloon = bits & 1 ? 31 : MM_TEXT_NIL;
        h.info.hid = bits & 2 ? 101 : 0;
        h.info.target = bits & 4 ? 6 : 0;
        h.info.script = bits & 8 ? 27 : MM_TEXT_NIL;
        h.info.width = -3;
        h.info.height = 4;
        h.info.sound_type = bits & 16 ? 7 : 0;
        h.info.sound_number = bits & 32 ? 9 : MM_TEXT_NIL;
        mm_help_balloon_merge(&p, &h.info);
        CHECK(p.balloon == ((bits & 1) ? 31 : 30) && p.hid == ((bits & 2) ? 101 : 100) &&
              p.target == ((bits & 4) ? 6 : 5) && p.script == ((bits & 8) ? 27 : MM_TEXT_NIL) && p.width == 17 &&
              p.height == 17);
        CHECK(p.sound_type == ((bits & 48) == 48 ? 7 : base.sound_type) &&
              p.sound_number == ((bits & 48) == 48 ? 9 : 17));
    }
    p = base;
    mm_help_balloon_merge(&p, 0);
    CHECK(!memcmp(&p, &base, sizeof(p)));
    p.width = 2147483647;
    h.info.width = 1;
    mm_help_balloon_merge(&p, &h.info);
    CHECK(p.width == (-2147483647 - 1));
}
static void factories(void)
{
    Host h;
    void *out;
    int r, partial;
    unsigned i;
    const char *stages = "GFCIEHA";
    init(&h);
    CHECK(mm_help_balloon_new(&out, 0, &h.factory) && out == &h.nodes[1] && !strcmp(h.log, "GFCsIEHAV"));
    init(&h);
    h.info.hid = 0;
    CHECK(mm_help_balloon_new(&out, 0, &h.factory) && !strcmp(h.log, "GUCsIEHAV"));
    init(&h);
    h.exists = 1;
    CHECK(mm_help_balloon_new(&out, 0, &h.factory) && !h.nodes[0].refs && !strcmp(h.log, "GFoCsIEHAV"));
    init(&h);
    h.exists = 1;
    h.balloon = 0;
    CHECK(!mm_help_balloon_new(&out, 0, &h.factory) && !out && h.nodes[0].refs &&
          h.diagnostic == MM_HELP_DUPLICATE + 1);
    for (r = -2; r <= 1; ++r)
    {
        init(&h);
        h.exists = 1;
        h.factory.review_reuse = 1;
        h.fail = 'R';
        h.result = r;
        CHECK(mm_help_balloon_new(&out, 0, &h.factory) == r && h.nodes[0].refs && !h.nodes[1].refs);
        CHECK(out == (r > 0 ? &h.nodes[0] : 0));
    }
    for (i = 0; stages[i]; ++i)
        for (partial = 0; partial < 2; ++partial)
            for (r = -2; r <= 0; ++r)
            {
                if (stages[i] == 'F' && !r)
                    continue;
                init(&h);
                h.fail = stages[i];
                h.result = r;
                h.partial = partial;
                CHECK(mm_help_balloon_new(&out, 0, &h.factory) == r && !out);
                CHECK(strchr("EHA", stages[i]) ? !strchr(h.log, 'n') : !h.nodes[1].refs);
                if (!r && stages[i] == 'E')
                    CHECK(h.diagnostic == MM_HELP_ENTER_DEAD + 1 && h.dead);
                if (!r && stages[i] == 'A')
                    CHECK(h.diagnostic == MM_HELP_HIT_DEAD + 1 && h.dead);
            }
    stages = "FRHB";
    for (i = 0; stages[i]; ++i)
        for (r = -2; r <= 0; ++r)
        {
            init(&h);
            h.partial = 1;
            h.fail = stages[i];
            h.result = r;
            CHECK(mm_help_balloon_read(&out, 17, &h.read) == r && !h.nodes[4].refs && !out);
        }
    init(&h);
    CHECK(mm_help_balloon_read(&out, 17, &h.read) && out == &h.nodes[1] && !h.nodes[4].refs && !strcmp(h.log, "FRHBd"));
}
static void topics(void)
{
    Host h;
    int r;
    unsigned i;
    const char *stages = "CSUNBMPOD";
    init(&h);
    CHECK(mm_help_balloon_topic(&h.state, &h.topic) && h.state.topic == &h.nodes[3] && !h.nodes[2].refs && h.x == 23 &&
          h.y == 23 && !strcmp(h.log, "CSUvNFKBMPOD"));
    init(&h);
    h.kid = 1;
    CHECK(mm_help_balloon_topic(&h.state, &h.topic) && h.x == 31 && h.y == 33);
    init(&h);
    h.mutation = 'S';
    CHECK(mm_help_balloon_topic(&h.state, &h.topic) && h.info.sound_number == 55);
    init(&h);
    h.mutation = 'N';
    CHECK(mm_help_balloon_topic(&h.state, &h.topic) && !strchr(h.log, 'F'));
    init(&h);
    h.mutation = 'O';
    CHECK(mm_help_balloon_topic(&h.state, &h.topic) && h.x == 43);
    for (i = 0; stages[i]; ++i)
        for (r = -2; r <= 0; ++r)
        {
            init(&h);
            h.fail = stages[i];
            h.result = r;
            CHECK(mm_help_balloon_topic(&h.state, &h.topic) == r);
            if (!r && strchr("CSU", stages[i]))
                CHECK(h.state.topic == &h.nodes[2] && h.nodes[2].refs && !h.nodes[3].refs);
            else if (strchr("CSU", stages[i]))
                CHECK(h.nodes[2].refs);
            else
                CHECK(!h.nodes[2].refs && h.state.topic == &h.nodes[3]);
        }
    init(&h);
    CHECK(mm_help_balloon_apply(1, &h.stages) && !strcmp(h.log, "BS"));
    init(&h);
    CHECK(mm_help_balloon_apply(0, &h.stages) && !strcmp(h.log, "SE"));
    for (r = -2; r <= 0; ++r)
    {
        init(&h);
        h.fail = 'S';
        h.result = r;
        CHECK(mm_help_balloon_apply(0, &h.stages) == r && !strchr(h.log, 'E'));
    }
}
static void layouts(void)
{
    Host h;
    int x, y, w, height, expected_x, expected_y;
    for (x = -30; x <= 60; x += 9)
        for (y = -20; y <= 50; y += 11)
            for (w = 5; w <= 65; w += 20)
                for (height = 3; height <= 48; height += 15)
                {
                    init(&h);
                    h.parent.left = h.parent.top = 0;
                    h.parent.right = 50;
                    h.parent.bottom = 40;
                    h.own.left = x;
                    h.own.right = x + w;
                    h.own.top = y;
                    h.own.bottom = y + height;
                    CHECK(mm_help_balloon_layout(2, 3, &h.layout));
                    expected_x = x < 0 ? 0 : x + w > 50 ? (w > 50 ? 0 : 50 - w) : x;
                    expected_y = y < 0 ? 0 : y + height > 40 ? (height > 40 ? 0 : 40 - height) : y;
                    if (h.positions)
                        CHECK(eq(&h.positioned, expected_x, expected_y, expected_x + w, expected_y + height));
                    else
                        CHECK(expected_x == x && expected_y == y);
                    CHECK(eq(&h.child, 17, 10, 30, 19) && h.child_positions == 1);
                }
    init(&h);
    h.mutation = 'p';
    h.width = 3;
    h.height = 3;
    CHECK(mm_help_balloon_layout(2, 3, &h.layout) && eq(&h.child, 17, 10, 30, 19));
    init(&h);
    h.own.left = h.own.top = h.own.right = h.own.bottom = 1;
    CHECK(mm_help_balloon_layout(2, 3, &h.layout) && h.positions == 1);
}
static void buttons(void)
{
    Host h;
    void *out;
    int r;
    unsigned i;
    const char *stages = "FCIEBP";
    init(&h);
    CHECK(mm_help_button_new(&out, 100, 3, 99, 12, 24, &h.button_factory) && out == &h.nodes[1] &&
          eq(&h.positioned, 12, 19, 20, 24) && !strcmp(h.log, "FCIEBPV"));
    init(&h);
    h.exists = 1;
    CHECK(!mm_help_button_new(&out, 100, 3, 99, 12, 24, &h.button_factory) && !out &&
          h.diagnostic == MM_HELP_DUPLICATE + 1);
    for (i = 0; stages[i]; ++i)
        for (r = -2; r <= 0; ++r)
        {
            if (stages[i] == 'F' && !r)
                continue;
            init(&h);
            h.fail = stages[i];
            h.result = r;
            h.partial = 1;
            CHECK(mm_help_button_new(&out, 100, 3, 99, 12, 24, &h.button_factory) == r && !out);
            CHECK(strchr("EBP", stages[i]) ? !strchr(h.log, 'n') : !h.nodes[1].refs);
        }
    init(&h);
    CHECK(mm_help_button_point(&h.nodes[1].button, 2, 3, &h.button) && !strcmp(h.log, "BPMG"));
    init(&h);
    h.base_hit = 1;
    CHECK(mm_help_button_point(&h.nodes[1].button, 2, 3, &h.button) && !strcmp(h.log, "B"));
    init(&h);
    h.nodes[1].button.group = 0;
    CHECK(!mm_help_button_point(&h.nodes[1].button, 2, 3, &h.button) && !strcmp(h.log, "B"));
    init(&h);
    h.mutation = 'G';
    CHECK(!mm_help_button_point(&h.nodes[1].button, 2, 3, &h.button));
    init(&h);
    h.parent_valid = 0;
    CHECK(mm_help_button_click(&h.nodes[1].button, &h.button) && h.diagnostic == MM_HELP_BAD_PARENT + 1 &&
          !strcmp(h.log, "iP!"));
    init(&h);
    h.heap = (MMHelpButton *)malloc(sizeof(*h.heap));
    CHECK(h.heap);
    *h.heap = h.nodes[1].button;
    CHECK(mm_help_button_click(h.heap, &h.button) && !h.heap && !strcmp(h.log, "iPfH"));
}
static void interrupted_callbacks(void)
{
    Host h;
    int r;
    unsigned i;
    const char *stages = "Dabpcq";
    for (i = 0; stages[i]; ++i)
        for (r = -2; r <= 0; ++r)
        {
            init(&h);
            h.fail = stages[i];
            h.result = r;
            CHECK(mm_help_balloon_layout(2, 3, &h.layout) == r);
            CHECK(h.log[h.used - 1] == stages[i]);
            if (stages[i] != 'q')
                CHECK(!h.child_positions);
        }
    init(&h);
    h.fail = 'F';
    h.result = 0;
    CHECK(mm_help_balloon_topic(&h.state, &h.topic) && h.x == 23 && h.y == 23);
    for (r = -2; r < 0; ++r)
    {
        init(&h);
        h.fail = 'F';
        h.result = r;
        CHECK(mm_help_balloon_topic(&h.state, &h.topic) == r && !h.nodes[2].refs && h.log[h.used - 1] == 'F');
        init(&h);
        h.kid = 1;
        h.fail = 'J';
        h.result = r;
        CHECK(mm_help_balloon_topic(&h.state, &h.topic) == r && h.log[h.used - 1] == 'J');
    }
    stages = "BPMG";
    for (i = 0; stages[i]; ++i)
        for (r = -2; r <= 0; ++r)
        {
            init(&h);
            h.fail = stages[i];
            h.result = r;
            CHECK(mm_help_button_point(&h.nodes[1].button, 2, 3, &h.button) == (!r && stages[i] == 'B' ? 1 : r));
        }
    for (r = -2; r <= 1; ++r)
    {
        init(&h);
        h.fail = 'H';
        h.result = r;
        h.heap = (MMHelpButton *)malloc(sizeof(*h.heap));
        CHECK(h.heap);
        *h.heap = h.nodes[1].button;
        CHECK(mm_help_button_click(h.heap, &h.button) == (r < 0 ? r : 1) && !h.heap);
    }
}
int main(void)
{
    merges();
    factories();
    topics();
    layouts();
    buttons();
    interrupted_callbacks();
    return 0;
}
