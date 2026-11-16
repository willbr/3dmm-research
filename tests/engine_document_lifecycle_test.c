#include "mm/document_lifecycle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "document lifecycle failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check(!!(x), __LINE__)
typedef struct Owner Owner;
typedef struct View
{
    Owner *owner;
    void *window;
} View;
struct Owner
{
    MMDocumentNode node;
    int id, refs, dead, present, count, dirty, answer, saved, named, choose;
    View *views[4];
};
typedef struct Host
{
    MMDocumentRegistry registry;
    MMDocumentLifecycleServices s;
    Owner nodes[8];
    View views[4];
    int window, used, ordinal, seen, result, partial, target_name, selected, adopt, force;
    int diagnostics[7], errors;
    char log[8192], fail;
    MMDocumentNode *clipboard;
} Host;
static Owner *owner(MMDocumentNode *n)
{
    Owner *o = (Owner *)n->owner;
    CHECK(o && !o->dead);
    return o;
}
static int effect(Host *h, char tag, MMDocumentNode *n)
{
    CHECK(h->used < 8189);
    h->log[h->used++] = tag;
    h->log[h->used++] = n ? (char)('0' + owner(n)->id) : '.';
    h->log[h->used] = 0;
    return h->fail == tag && ++h->seen == h->ordinal ? h->result : 1;
}
static MMI32 drop(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    effect(h, '-', n);
    return --owner(n)->refs;
}
static int retain(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, '+', n);
    if (r > 0 || h->partial)
        ++owner(n)->refs;
    return r;
}
static int release(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, 'R', n);
    return r > 0 ? mm_document_release(n, &h->s) : r;
}
static int destroy(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    Owner *o = owner(n);
    int r = effect(h, 'X', n);
    CHECK(n->freeing && !n->child && !o->count);
    if (r > 0)
    {
        r = mm_document_finish(&h->registry, n, &h->s);
        if (r > 0)
            o->dead = 1;
    }
    return r;
}
static int undo(void *v, MMDocumentNode *n)
{
    return effect((Host *)v, 'U', n);
}
static int present(void *v, MMDocumentNode *n)
{
    (void)v;
    return owner(n)->present;
}
static MMI32 count(void *v, MMDocumentNode *n)
{
    (void)v;
    return owner(n)->count;
}
static void *view(void *v, MMDocumentNode *n, MMI32 index)
{
    Owner *o = owner(n);
    (void)v;
    CHECK(index >= 0 && index < o->count);
    return o->views[index];
}
static void *window(void *v, void *p)
{
    (void)v;
    return ((View *)p)->window;
}
static int release_view(void *v, void *p)
{
    Host *h = (Host *)v;
    Owner *o = p == &h->window ? h->views[0].owner : ((View *)p)->owner;
    int i, removed = 0, r = effect(h, p == &h->window ? 'W' : 'V', &o->node);
    if (r <= 0)
        return r;
    for (i = 0; i < o->count;)
    {
        if ((p == &h->window && o->views[i]->window == p) || o->views[i] == p)
        {
            --o->count;
            memmove(o->views + i, o->views + i + 1, (size_t)(o->count - i) * sizeof(o->views[0]));
            ++removed;
        }
        else
            ++i;
    }
    CHECK(removed);
    while (removed--)
    {
        r = mm_document_release(&o->node, &h->s);
        if (r <= 0)
            return r;
    }
    return 1;
}
static int close_views(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, 'L', n);
    return r > 0 ? mm_document_close_views(n, &h->s) : r;
}
static int dispose_views(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, 'F', n);
    if (r > 0)
        owner(n)->present = 0;
    return r;
}
static int clipboard(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, 'C', n);
    return r > 0 ? h->clipboard == n : r;
}
static int clear_clipboard(void *v)
{
    Host *h = (Host *)v;
    MMDocumentNode *n = h->clipboard;
    int r = effect(h, 'B', n);
    if (r <= 0)
        return r;
    h->clipboard = 0;
    return mm_document_release(n, &h->s);
}
static int dirty(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, 'D', n);
    if (r < 0 || !owner(n)->dirty)
        return r < 0 ? r : 0;
    r = mm_document_internal(n, &h->s);
    return r < 0 ? r : !r;
}
static int query_save(void *v, MMDocumentNode *n, int force)
{
    Host *h = (Host *)v;
    int r = effect(h, 'Q', n);
    h->force = force;
    return r > 0 ? owner(n)->answer : r;
}
static int save(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, 'S', n);
    return r > 0 ? owner(n)->saved : r;
}
static int query_close(void *v, MMDocumentNode *n, MMU32 flags)
{
    Host *h = (Host *)v;
    int r = effect(h, 'q', n);
    return r > 0 ? mm_document_query_close(n, flags, &h->s) : r;
}
static int match(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, 'M', n);
    return r > 0 ? owner(n)->named && owner(n)->id == h->target_name : r;
}
static int current_name(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, 'N', n);
    if (r > 0 && owner(n)->named)
        h->selected = owner(n)->id;
    return r > 0 ? owner(n)->named : r;
}
static int choose_name(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, 'H', n);
    if (r > 0 && owner(n)->choose)
        h->selected = owner(n)->id;
    return r > 0 ? owner(n)->choose : r;
}
static int write_name(void *v, MMDocumentNode *n, int adopt)
{
    Host *h = (Host *)v;
    int r = effect(h, 'T', n);
    CHECK(h->selected == owner(n)->id);
    h->adopt = adopt;
    return r > 0 ? owner(n)->saved : r;
}
static int update_name(void *v, MMDocumentNode *n)
{
    return effect((Host *)v, 'A', n);
}
static void error(void *v)
{
    ++((Host *)v)->errors;
    effect((Host *)v, 'E', 0);
}
static void diagnostic(void *v, int kind)
{
    Host *h = (Host *)v;
    CHECK(kind >= 0 && kind < 7);
    ++h->diagnostics[kind];
    effect(h, '!', 0);
}
static void init(Host *h, int count_nodes)
{
    int i;
    memset(h, 0, sizeof(*h));
    h->ordinal = 1;
    h->selected = -1;
    for (i = 0; i < count_nodes; ++i)
    {
        Owner *o = &h->nodes[i];
        o->id = i;
        o->refs = o->dirty = o->answer = o->saved = o->choose = 1;
        mm_document_register(&h->registry, &o->node, o, i ? &h->nodes[0].node : 0, 0);
    }
    h->s.context = h;
    h->s.drop_reference = drop;
    h->s.retain = retain;
    h->s.release = release;
    h->s.destroy = destroy;
    h->s.dispose_undo = undo;
    h->s.views_present = present;
    h->s.view_count = count;
    h->s.view = view;
    h->s.window = window;
    h->s.release_view = release_view;
    h->s.close_views = close_views;
    h->s.dispose_views = dispose_views;
    h->s.clipboard = clipboard;
    h->s.clear_clipboard = clear_clipboard;
    h->s.dirty = dirty;
    h->s.query_save = query_save;
    h->s.save = save;
    h->s.query_close = query_close;
    h->s.match_name = match;
    h->s.current_name = current_name;
    h->s.choose_name = choose_name;
    h->s.write_name = write_name;
    h->s.update_name = update_name;
    h->s.save_error = error;
    h->s.diagnostic = diagnostic;
}
static void reset(Host *h, char fail, int result)
{
    h->used = h->seen = 0;
    h->log[0] = 0;
    h->fail = fail;
    h->result = result;
    h->ordinal = 1;
}
static void set_views_for(Host *h, int id)
{
    Owner *o = &h->nodes[id];
    int i;
    o->present = 1;
    o->count = 3;
    o->refs += 3;
    for (i = 0; i < 3; ++i)
    {
        h->views[i].owner = o;
        h->views[i].window = i < 2 ? &h->window : 0;
        o->views[i] = &h->views[i];
    }
}
static void set_views(Host *h)
{
    set_views_for(h, 0);
}
static void ownership(void)
{
    Host h;
    int result;
    init(&h, 3);
    h.nodes[2].refs = 2;
    CHECK(mm_document_release(&h.nodes[0].node, &h.s) == 1);
    CHECK(h.nodes[0].dead && h.nodes[1].dead && h.nodes[2].dead && !h.registry.first);
    CHECK(h.diagnostics[MM_DOC_CHILD_RETAINED] == 3);
    CHECK(strstr(h.log, "U0L2") && strstr(h.log, "X2C2F2L1") && strstr(h.log, "X1C1F1X0C0F0"));
    init(&h, 2);
    set_views_for(&h, 1);
    --h.nodes[1].refs;
    CHECK(mm_document_release(&h.nodes[0].node, &h.s) == 1 && h.nodes[0].dead && h.nodes[1].dead);
    CHECK(!h.diagnostics[MM_DOC_CHILD_RETAINED] && strstr(h.log, "L1+1W1"));
    init(&h, 1);
    h.nodes[0].refs = 2;
    CHECK(mm_document_release(&h.nodes[0].node, &h.s) == 1 && h.nodes[0].refs == 1 && !h.nodes[0].node.freeing);
    CHECK(!strcmp(h.log, "-0"));
    init(&h, 1);
    h.clipboard = &h.nodes[0].node;
    CHECK(mm_document_release(h.clipboard, &h.s) == 1 && h.nodes[0].dead && !h.clipboard);
    CHECK(h.nodes[0].refs == 1 && strstr(h.log, "C0!.+0+0B0-0F0"));
    CHECK(h.diagnostics[MM_DOC_CLIPBOARD_RELEASE] == 1);
    init(&h, 1);
    set_views(&h);
    --h.nodes[0].refs;
    CHECK(mm_document_close_views(&h.nodes[0].node, &h.s) == 1 && h.nodes[0].dead);
    CHECK(strstr(h.log, "+0W0-0-0V0-0R0-0U0X0C0F0"));
    init(&h, 1);
    CHECK(mm_document_close_views(&h.nodes[0].node, &h.s) == 1 && !h.used);
    h.nodes[0].present = 1;
    CHECK(mm_document_close_views(&h.nodes[0].node, &h.s) == 1 && !strcmp(h.log, "+0R0-0"));
    for (result = -1; result >= -2; --result)
    {
        init(&h, 2);
        reset(&h, 'U', result);
        CHECK(mm_document_release(&h.nodes[0].node, &h.s) == result);
        CHECK(!h.nodes[0].refs && h.nodes[0].node.freeing && !h.nodes[0].dead && !h.nodes[1].dead);
        init(&h, 2);
        reset(&h, 'L', result);
        CHECK(mm_document_release(&h.nodes[0].node, &h.s) == result && h.nodes[1].refs == 1);
        init(&h, 2);
        reset(&h, 'R', result);
        CHECK(mm_document_release(&h.nodes[0].node, &h.s) == result && h.nodes[1].refs == 1);
        init(&h, 1);
        set_views(&h);
        reset(&h, 'W', result);
        CHECK(mm_document_close_views(&h.nodes[0].node, &h.s) == result && h.nodes[0].refs == 5);
        CHECK(h.nodes[0].count == 3 && !strchr(h.log, 'R'));
        init(&h, 1);
        h.clipboard = &h.nodes[0].node;
        reset(&h, 'B', result);
        CHECK(mm_document_release(h.clipboard, &h.s) == result && h.nodes[0].refs == 2);
        CHECK(!h.nodes[0].dead && h.registry.first == h.clipboard);
    }
}
static void queries(void)
{
    Host h;
    MMDocumentNode *found;
    int result, kind;
    init(&h, 3);
    CHECK(mm_document_query_close(&h.nodes[0].node, 0, &h.s) == 1);
    CHECK(!strcmp(h.log, "D2C2Q2S2D1C1Q1S1D0C0Q0S0"));
    h.nodes[2].answer = 2;
    reset(&h, 0, 0);
    CHECK(!mm_document_query_close(&h.nodes[0].node, MM_DOC_FORCE, &h.s) && h.force == 1);
    CHECK(!strchr(h.log, 'S'));
    CHECK(mm_document_query_close(&h.nodes[0].node, MM_DOC_FORCE | MM_DOC_ASSUME_YES, &h.s) == 1);
    h.nodes[2].answer = 0;
    h.nodes[1].saved = 0;
    CHECK(!mm_document_query_close(&h.nodes[0].node, 0, &h.s));
    CHECK(mm_document_query_close(&h.nodes[0].node, MM_DOC_FORCE, &h.s) == 1);
    h.nodes[1].node.internal = 1;
    reset(&h, 0, 0);
    CHECK(mm_document_query_close(&h.nodes[0].node, 0, &h.s) == 1 && !strstr(h.log, "C1"));
    h.clipboard = &h.nodes[0].node;
    CHECK(mm_document_internal(h.clipboard, &h.s) == 1);
    init(&h, 1);
    set_views(&h);
    CHECK(mm_document_query_window(&h.nodes[0].node, &h.window, &h.s) == 1 && strstr(h.log, "q0"));
    reset(&h, 0, 0);
    CHECK(mm_document_query_window(&h.nodes[0].node, &h, &h.s) == 1 && !h.used);
    h.nodes[0].count = 0;
    CHECK(mm_document_query_window(&h.nodes[0].node, &h.window, &h.s) == 1 && h.diagnostics[MM_DOC_NO_VIEWS] == 1);
    for (kind = MM_DOC_SAVE; kind <= MM_DOC_SAVE_COPY; ++kind)
    {
        init(&h, 1);
        CHECK(mm_document_save(&h.nodes[0].node, kind, &h.s) == 1 && h.adopt == (kind != MM_DOC_SAVE_COPY));
        CHECK((strchr(h.log, 'A') != 0) == (kind != MM_DOC_SAVE_COPY));
    }
    init(&h, 1);
    h.nodes[0].named = 1;
    CHECK(mm_document_save(&h.nodes[0].node, MM_DOC_SAVE, &h.s) == 1 && !strcmp(h.log, "N0T0A0"));
    for (result = 0; result >= -2; --result)
    {
        init(&h, 1);
        reset(&h, 'T', result);
        CHECK(mm_document_save(&h.nodes[0].node, MM_DOC_SAVE, &h.s) == result && h.errors == !result);
        CHECK(!strchr(h.log, 'A'));
        init(&h, 1);
        reset(&h, 'H', result);
        CHECK(mm_document_save(&h.nodes[0].node, MM_DOC_SAVE_AS, &h.s) == result && !h.errors && !strchr(h.log, 'T'));
        init(&h, 1);
        reset(&h, 'q', result);
        CHECK(mm_document_query_all(&h.registry, 0, &h.s) == result);
        init(&h, 1);
        reset(&h, 'M', result);
        found = &h.nodes[0].node;
        CHECK(mm_document_find(&h.registry, &found, &h.s) == result && !found);
    }
    init(&h, 1);
    CHECK(!mm_document_save(&h.nodes[0].node, 3, &h.s) && h.diagnostics[MM_DOC_BAD_SAVE] == 1);
    h.target_name = 0;
    h.nodes[0].named = 1;
    CHECK(mm_document_find(&h.registry, &found, &h.s) == 1 && found == &h.nodes[0].node);
    init(&h, 3);
    CHECK(mm_document_unlink(&h.registry, &h.nodes[2].node));
    mm_document_register(&h.registry, &h.nodes[2].node, &h.nodes[2], 0, 0);
    CHECK(mm_document_unlink(&h.registry, &h.nodes[1].node));
    mm_document_register(&h.registry, &h.nodes[1].node, &h.nodes[1], 0, 0);
    CHECK(mm_document_query_all(&h.registry, 0, &h.s) == 1);
    CHECK(strstr(h.log, "q1") && strstr(h.log, "q2") && strstr(h.log, "q0"));
    CHECK(strstr(h.log, "q1") < strstr(h.log, "q2") && strstr(h.log, "q2") < strstr(h.log, "q0"));
    reset(&h, 0, 0);
    h.nodes[2].answer = 2;
    CHECK(!mm_document_query_all(&h.registry, 0, &h.s) && !strstr(h.log, "q0"));
    h.nodes[2].named = 1;
    h.target_name = 2;
    reset(&h, 0, 0);
    CHECK(mm_document_find(&h.registry, &found, &h.s) == 1 && found == &h.nodes[2].node);
    CHECK(!strcmp(h.log, "M1M2"));
    for (result = -1; result >= -2; --result)
    {
        const char *tags = "DQS";
        int i;
        for (i = 0; tags[i]; ++i)
        {
            init(&h, 3);
            reset(&h, tags[i], result);
            CHECK(mm_document_query_close(&h.nodes[0].node, MM_DOC_FORCE, &h.s) == result);
            CHECK(!strstr(h.log, "D1"));
        }
    }
}
typedef struct Model
{
    int children[25][24], count[25], parent[24], alive[24];
    MMDocumentNode nodes[24];
    MMDocumentRegistry registry;
    int ids[64], flags[64], used;
} Model;
static unsigned random_value(unsigned *r)
{
    *r = *r * 1664525UL + 1013904223UL;
    return *r;
}
static void expected(Model *m, int id, int root, int skip)
{
    int i, at = m->used++;
    m->ids[at] = id;
    m->flags[at] = MM_DOC_TREE_PRE | (id == root ? MM_DOC_TREE_ROOT : 0) | (!m->count[id] ? MM_DOC_TREE_POST : 0);
    if (m->count[id] && id != skip)
    {
        for (i = 0; i < m->count[id]; ++i)
            expected(m, m->children[id][i], root, skip);
        at = m->used++;
        m->ids[at] = id;
        m->flags[at] = MM_DOC_TREE_POST | (id == root ? MM_DOC_TREE_ROOT : 0);
    }
}
static void compare_tree(Model *m, int root, int skip)
{
    MMDocumentTree t;
    MMDocumentNode *out = 0, *sentinel;
    MMU32 flags, input = 0;
    int i = 0;
    m->used = 0;
    expected(m, root, root, skip);
    mm_document_tree_init(&t, &m->nodes[root]);
    while (mm_document_tree_next(&t, &out, &flags, input))
    {
        CHECK(i < m->used && out == &m->nodes[m->ids[i]] && flags == (MMU32)m->flags[i]);
        input = out == &m->nodes[skip < 0 ? 0 : skip] && skip >= 0 ? MM_DOC_TREE_SKIP : 0;
        ++i;
    }
    CHECK(i == m->used && flags == 0);
    sentinel = out;
    CHECK(!mm_document_tree_next(&t, &out, &flags, 0) && out == sentinel && !flags);
}
static void graphs(void)
{
    Model m;
    MMDocumentNode *anchor, *n, *next;
    MMDocumentTree t;
    MMU32 flags;
    unsigned rng = 941, round;
    int i, j, parent, sibling, at, root, slot;
    for (round = 0; round < 80; ++round)
    {
        memset(&m, 0, sizeof(m));
        for (i = 0; i < 24; ++i)
        {
            j = i ? (int)(random_value(&rng) % (unsigned)i) : 0;
            sibling = (int)(random_value(&rng) & 1);
            anchor = i && i % 5 ? &m.nodes[j] : 0;
            parent = !anchor ? 24 : sibling ? m.parent[j] : j;
            m.parent[i] = parent;
            at = 0;
            if (anchor && sibling)
            {
                while (m.children[parent][at] != j)
                    ++at;
                ++at;
            }
            for (slot = m.count[parent]; slot > at; --slot)
                m.children[parent][slot] = m.children[parent][slot - 1];
            m.children[parent][at] = i;
            ++m.count[parent];
            m.alive[i] = 1;
            mm_document_register(&m.registry, &m.nodes[i], &m.nodes[i], anchor, sibling);
        }
        for (parent = 0; parent <= 24; ++parent)
        {
            n = parent == 24 ? m.registry.first : m.nodes[parent].child;
            for (i = 0; i < m.count[parent]; ++i)
            {
                CHECK(n == &m.nodes[m.children[parent][i]]);
                CHECK(n->parent == (parent == 24 ? 0 : &m.nodes[parent]));
                n = n->sibling;
            }
            CHECK(!n);
        }
        for (root = 0; root < 24; ++root)
        {
            compare_tree(&m, root, -1);
            compare_tree(&m, root, root);
            compare_tree(&m, root, (int)(random_value(&rng) % 24));
        }
        for (i = 23; i >= 0; --i)
        {
            n = &m.nodes[i];
            next = n->sibling;
            CHECK(mm_document_unlink(&m.registry, n) == 1 && n->sibling == next);
            CHECK(!mm_document_unlink(&m.registry, n));
            parent = m.parent[i];
            for (at = 0; m.children[parent][at] != i; ++at)
                ;
            for (slot = at + 1; slot < m.count[parent]; ++slot)
                m.children[parent][slot - 1] = m.children[parent][slot];
            --m.count[parent];
            m.alive[i] = 0;
            CHECK(m.registry.first == (m.count[24] ? &m.nodes[m.children[24][0]] : 0));
            for (j = 0; j < i; ++j)
                CHECK(m.nodes[j].child == (m.count[j] ? &m.nodes[m.children[j][0]] : 0));
        }
        CHECK(!m.registry.first);
    }
    mm_document_tree_init(&t, 0);
    n = &m.nodes[0];
    CHECK(!mm_document_tree_next(&t, &n, &flags, 0) && n == &m.nodes[0] && !flags);
}
int main(void)
{
    graphs();
    ownership();
    queries();
    puts("document lifecycle tests passed");
    return 0;
}
