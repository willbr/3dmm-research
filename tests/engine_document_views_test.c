#include "mm/document_views.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "document views failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check(!!(x), __LINE__)
typedef struct Window
{
    int id, title;
} Window;
typedef struct View
{
    int refs, active;
    Window *window;
} View;
typedef struct Doc
{
    MMDocumentNode node;
    MMDocumentViews views;
    int id, file, dead, reads;
} Doc;
typedef struct Host
{
    MMDocumentRegistry registry;
    MMDocumentViewServices s;
    Doc docs[4];
    View tokens[16];
    Window windows[4];
    int status, used, seen, ordinal, result, validations, missing, partial, clipboard;
    int kind, number, label, reading, mutation, mutated, destroy_on_activate;
    char log[4096], fail;
    MMU32 maximum_bytes;
} Host;
static int effect(Host *h, char tag)
{
    CHECK(h->used < 4095);
    h->log[h->used++] = tag;
    h->log[h->used] = 0;
    return h->fail == tag && ++h->seen == h->ordinal ? h->result : 1;
}
static void *resize(void *v, void *p, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Host *h = (Host *)v;
    int r;
    (void)old_bytes;
    CHECK(priority == MM_STORAGE_SPEED || priority == MM_STORAGE_NORMAL);
    if (h->status < 0)
        return 0;
    r = effect(h, 'M');
    if (r < 0)
        h->status = r;
    return r <= 0 || bytes > h->maximum_bytes ? 0 : realloc(p, (size_t)bytes);
}
static void free_storage(void *v, void *p, MMU32 bytes)
{
    (void)bytes;
    effect((Host *)v, 'F');
    free(p);
}
static int status(void *v)
{
    return ((Host *)v)->status;
}
static void validate(void *v)
{
    ++((Host *)v)->validations;
    effect((Host *)v, 'V');
}
static void missing(void *v)
{
    ++((Host *)v)->missing;
    effect((Host *)v, '!');
}
static void *get(void *v, MMDocumentNode *n, MMI32 index)
{
    Host *h = (Host *)v;
    Doc *d = (Doc *)n->owner;
    CHECK(!d->dead && index >= 0);
    effect(h, 'G');
    return mm_document_views_get(&d->views, (MMU32)index);
}
static int active(void *v, void *p)
{
    Host *h = (Host *)v;
    int r = effect(h, 'a');
    return r > 0 ? ((View *)p)->active : r;
}
static void *window(void *v, void *p)
{
    effect((Host *)v, 'w');
    return ((View *)p)->window;
}
static int activate_window(void *v, void *p)
{
    Host *h = (Host *)v;
    int r = effect(h, 'A');
    CHECK(p == &h->windows[0]);
    if (r > 0 && h->destroy_on_activate)
    {
        CHECK(mm_document_views_dispose(&h->docs[0].views, &h->s) == 1);
        h->docs[0].dead = 1;
    }
    return r;
}
static int clipboard(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, 'C');
    return r > 0 ? ((Doc *)n->owner)->id == h->clipboard : r;
}
static int current_name(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    int r = effect(h, 'f');
    return r > 0 ? ((Doc *)n->owner)->file : r;
}
static int emit_name(void *v, int kind, MMI32 number)
{
    Host *h = (Host *)v;
    int r = effect(h, 'E');
    if (r > 0 || h->partial)
    {
        h->kind = kind;
        h->number = (int)number;
        h->label = kind == MM_DOC_NAME_UNTITLED ? 100 + (int)number : kind + 10;
    }
    return r;
}
static int read_name(void *v, MMDocumentNode *n)
{
    Host *h = (Host *)v;
    Doc *d = (Doc *)n->owner;
    int r = effect(h, 'N');
    if (r <= 0)
        return r;
    h->reading = d->id;
    ++d->reads;
    if (h->mutation == 1 && !h->mutated)
    {
        h->mutated = 1;
        mm_document_views_remove(&d->views, &h->tokens[0], &h->s);
    }
    return mm_document_name(&h->registry, n, &h->s);
}
static int set_title(void *v, void *p)
{
    Host *h = (Host *)v;
    Window *w = (Window *)p;
    int r = effect(h, 'T');
    if (r > 0 || h->partial)
        w->title = h->label;
    if (r > 0 && h->mutation == 2 && !h->mutated)
    {
        h->mutated = 1;
        mm_document_views_remove(&h->docs[h->reading].views, &h->tokens[0], &h->s);
        CHECK(mm_document_views_add(&h->docs[h->reading].views, &h->tokens[3], &h->s) == 1);
    }
    return r;
}
static void reset(Host *h, char fail, int result)
{
    h->status = h->ordinal = 1;
    h->used = h->seen = 0;
    h->log[0] = 0;
    h->fail = fail;
    h->result = result;
}
static void init(Host *h)
{
    MMStorageAllocator a;
    int i;
    memset(h, 0, sizeof(*h));
    h->status = h->ordinal = 1;
    h->clipboard = -1;
    h->maximum_bytes = MM_STORAGE_LIMIT;
    a.context = h;
    a.resize = resize;
    a.release = free_storage;
    for (i = 0; i < 4; ++i)
    {
        Doc *d = &h->docs[i];
        d->id = i;
        mm_document_register(&h->registry, &d->node, d, i ? &h->docs[0].node : 0, 0);
        CHECK(mm_document_views_init(&d->views, &a, 1) == 1);
        h->windows[i].id = i;
    }
    for (i = 0; i < 16; ++i)
        h->tokens[i].refs = 1;
    h->s.context = h;
    h->s.status = status;
    h->s.validate = validate;
    h->s.missing = missing;
    h->s.get = get;
    h->s.active = active;
    h->s.window = window;
    h->s.activate_window = activate_window;
    h->s.read_name = read_name;
    h->s.set_title = set_title;
    h->s.clipboard = clipboard;
    h->s.current_name = current_name;
    h->s.emit_name = emit_name;
}
static void finish(Host *h)
{
    int i;
    reset(h, 0, 0);
    for (i = 0; i < 4; ++i)
        CHECK(mm_document_views_dispose(&h->docs[i].views, &h->s) == 1);
    for (i = 0; i < 16; ++i)
        CHECK(h->tokens[i].refs == 1);
}
static void seed(Host *h)
{
    int i;
    for (i = 0; i < 3; ++i)
    {
        h->tokens[i].window = &h->windows[i];
        CHECK(mm_document_views_add(&h->docs[0].views, &h->tokens[i], &h->s) == 1);
    }
    h->tokens[3].window = &h->windows[3];
}
static void storage(void)
{
    Host h;
    MMDocumentViews *v;
    MMI32 at;
    int r;
    init(&h);
    v = &h.docs[0].views;
    at = 77;
    CHECK(!mm_document_views_find(v, &h.tokens[0], &at) && at == 77 && !v->ready);
    CHECK(mm_document_views_add(v, &h.tokens[0], &h.s) == 1);
    CHECK(mm_document_views_add(v, &h.tokens[1], &h.s) == 1);
    CHECK(mm_document_views_add(v, &h.tokens[0], &h.s) == 1 && v->count == 3);
    CHECK(mm_document_views_find(v, &h.tokens[0], &at) == 1 && at == 0);
    mm_document_views_remove(v, &h.tokens[0], &h.s);
    CHECK(mm_document_views_get(v, 0) == &h.tokens[1] && mm_document_views_get(v, 1) == &h.tokens[0]);
    CHECK(mm_document_views_first(v, &h.tokens[0], &h.s) == 1 && mm_document_views_get(v, 0) == &h.tokens[0]);
    CHECK(!mm_document_views_first(v, &h.tokens[9], &h.s) && h.missing == 1);
    finish(&h);
    for (r = 0; r >= -2; --r)
    {
        init(&h);
        v = &h.docs[0].views;
        reset(&h, 'M', r);
        CHECK(mm_document_views_add(v, &h.tokens[0], &h.s) == (r ? r : 1));
        CHECK(v->ready == !r && v->count == (MMU32)!r && h.validations == !r);
        finish(&h);
        init(&h);
        v = &h.docs[0].views;
        v->storage.min_grow[0] = 0;
        CHECK(mm_document_views_add(v, &h.tokens[0], &h.s) == 1);
        reset(&h, 'M', r);
        CHECK(mm_document_views_add(v, &h.tokens[1], &h.s) == r);
        CHECK(v->ready && v->count == 1 && mm_document_views_get(v, 0) == &h.tokens[0]);
        CHECK(h.validations == (r ? 1 : 2));
        finish(&h);
    }
    init(&h);
    h.maximum_bytes = 0;
    CHECK(!mm_document_views_add(&h.docs[0].views, &h.tokens[0], &h.s) && !h.docs[0].views.ready && !h.validations);
    finish(&h);
}
static void presentation(void)
{
    Host h;
    void *out;
    int r, partial, mode;
    init(&h);
    seed(&h);
    h.tokens[1].active = 1;
    CHECK(!mm_document_view_active(&h.docs[0].node, &out, &h.s) && !out);
    CHECK(mm_document_views_first(&h.docs[0].views, &h.tokens[1], &h.s) == 1);
    CHECK(mm_document_view_active(&h.docs[0].node, &out, &h.s) == 1 && out == &h.tokens[1]);
    finish(&h);
    init(&h);
    seed(&h);
    reset(&h, 0, 0);
    CHECK(mm_document_view_activate(&h.docs[0].node, &h.s) == 1 && !strcmp(h.log, "GwA"));
    finish(&h);
    init(&h);
    seed(&h);
    h.destroy_on_activate = 1;
    CHECK(mm_document_view_activate(&h.docs[0].node, &h.s) == 1 && h.docs[0].dead);
    finish(&h);
    for (mode = 0; mode < 3; ++mode)
    {
        init(&h);
        seed(&h);
        h.mutation = mode;
        CHECK(mm_document_titles(&h.docs[0].node, &h.s) == 1);
        CHECK(h.docs[0].reads == 1 && h.docs[1].reads == 1 && h.docs[2].reads == 1 && h.docs[3].reads == 1);
        CHECK(h.windows[1].title == (mode == 2 ? 0 : 101));
        CHECK(h.windows[0].title == (mode == 1 ? 0 : 101));
        CHECK(h.windows[2].title == 101 && h.windows[3].title == (mode == 2 ? 101 : 0));
        finish(&h);
    }
    for (r = 0; r >= -2; --r)
        for (partial = 0; partial <= 1; ++partial)
        {
            init(&h);
            seed(&h);
            h.partial = partial;
            reset(&h, 'T', r);
            CHECK(mm_document_titles(&h.docs[0].node, &h.s) == r);
            CHECK(h.docs[0].node.untitled == 1 && h.registry.untitled == 1);
            CHECK(h.windows[0].title == (partial ? 101 : 0) && !h.windows[1].title && !h.docs[3].reads);
            finish(&h);
        }
    for (r = -1; r >= -2; --r)
    {
        init(&h);
        seed(&h);
        reset(&h, 'a', r);
        CHECK(mm_document_view_active(&h.docs[0].node, &out, &h.s) == r && !out);
        reset(&h, 'A', r);
        CHECK(mm_document_view_activate(&h.docs[0].node, &h.s) == r);
        reset(&h, 'N', r);
        CHECK(mm_document_titles(&h.docs[0].node, &h.s) == r && !h.docs[0].node.untitled);
        finish(&h);
    }
}
static void names(void)
{
    Host h;
    int r, kind;
    init(&h);
    CHECK(mm_document_name(&h.registry, &h.docs[0].node, &h.s) == 1);
    CHECK(h.kind == MM_DOC_NAME_UNTITLED && h.number == 1 && !strcmp(h.log, "CfE"));
    CHECK(mm_document_name(&h.registry, &h.docs[0].node, &h.s) == 1 && h.registry.untitled == 1);
    h.docs[0].file = 1;
    CHECK(mm_document_name(&h.registry, &h.docs[0].node, &h.s) == 1 && h.kind == MM_DOC_NAME_FILE &&
          h.registry.untitled == 1);
    h.clipboard = 0;
    reset(&h, 0, 0);
    CHECK(mm_document_name(&h.registry, &h.docs[0].node, &h.s) == 1 && h.kind == MM_DOC_NAME_CLIPBOARD &&
          !strcmp(h.log, "CE"));
    h.registry.untitled = 2147483647L;
    CHECK(mm_document_untitled(&h.registry, &h.docs[1].node) == (-2147483647L - 1));
    CHECK(mm_document_untitled(&h.registry, &h.docs[1].node) == (-2147483647L - 1));
    finish(&h);
    for (r = 0; r >= -2; --r)
    {
        init(&h);
        reset(&h, 'E', r);
        h.partial = 1;
        CHECK(mm_document_name(&h.registry, &h.docs[0].node, &h.s) == r);
        CHECK(h.docs[0].node.untitled == 1 && h.registry.untitled == 1 && h.kind == MM_DOC_NAME_UNTITLED);
        finish(&h);
        if (!r)
            continue;
        for (kind = 0; kind < 2; ++kind)
        {
            init(&h);
            reset(&h, kind ? 'f' : 'C', r);
            CHECK(mm_document_name(&h.registry, &h.docs[0].node, &h.s) == r && !h.registry.untitled &&
                  !strchr(h.log, 'E'));
            finish(&h);
        }
    }
}
static unsigned next(unsigned *r)
{
    *r = *r * 1664525UL + 1013904223UL;
    return *r;
}
static void model(void)
{
    Host h;
    int list[128], count = 0, ready = 0, op, id, i, at, expected;
    MMI32 found;
    unsigned rng = 4117, step;
    init(&h);
    for (step = 0; step < 4000; ++step)
    {
        reset(&h, 0, 0);
        op = (int)(next(&rng) % 5);
        id = (int)(next(&rng) % 16);
        if (count >= 120 && op == 0)
            op = 4;
        for (at = 0; at < count && list[at] != id; ++at)
            ;
        expected = at < count;
        if (op == 0)
        {
            CHECK(mm_document_views_add(&h.docs[0].views, &h.tokens[id], &h.s) == 1);
            list[count++] = id;
            ready = 1;
        }
        else if (op == 1)
        {
            mm_document_views_remove(&h.docs[0].views, &h.tokens[id], &h.s);
            if (expected)
            {
                for (i = at + 1; i < count; ++i)
                    list[i - 1] = list[i];
                --count;
            }
        }
        else if (op == 2)
        {
            CHECK(mm_document_views_first(&h.docs[0].views, &h.tokens[id], &h.s) == expected);
            if (expected)
            {
                for (i = at; i > 0; --i)
                    list[i] = list[i - 1];
                list[0] = id;
            }
        }
        else if (op == 3)
        {
            found = 77;
            CHECK(mm_document_views_find(&h.docs[0].views, &h.tokens[id], &found) == expected);
            CHECK(found == (expected ? at : 77));
        }
        else
        {
            CHECK(mm_document_views_dispose(&h.docs[0].views, &h.s) == 1);
            count = ready = 0;
        }
        CHECK(h.docs[0].views.count == (MMU32)count && h.docs[0].views.ready == ready);
        for (i = 0; i < count; ++i)
            CHECK(mm_document_views_get(&h.docs[0].views, (MMU32)i) == &h.tokens[list[i]]);
        CHECK(!mm_document_views_get(&h.docs[0].views, (MMU32)count));
        for (i = 0; i < 16; ++i)
            CHECK(h.tokens[i].refs == 1);
    }
    finish(&h);
}
typedef struct EditTest
{
    char calls[12];
    int count, empty, self, selection, paste_result, paste_command, refs;
} EditTest;

static void edit_event(EditTest *t, char event)
{
    t->calls[t->count++] = event;
    t->calls[t->count] = 0;
}

static int edit_copy(void *context, void **owned)
{
    EditTest *t = context;
    edit_event(t, owned ? 'C' : '?');
    if (owned && t->selection)
    {
        *owned = t;
        ++t->refs;
    }
    return t->selection;
}

static int edit_empty(void *context)
{
    EditTest *t = context;
    edit_event(t, 'E');
    return t->empty;
}

static int edit_self(void *context)
{
    EditTest *t = context;
    edit_event(t, 'S');
    return t->self;
}

static int edit_publish(void *context, void *document)
{
    EditTest *t = context;
    CHECK(document == t && t->refs == 1);
    edit_event(t, 'P');
    ++t->refs;
    return 1;
}

static int edit_release(void *context, void *document)
{
    EditTest *t = context;
    CHECK(document == t && t->refs == 2);
    edit_event(t, 'R');
    --t->refs;
    return 1;
}

static int edit_clear(void *context)
{
    edit_event(context, 'X');
    return 1;
}

static int edit_paste(void *context, int apply, int command)
{
    EditTest *t = context;
    edit_event(t, apply ? 'A' : 'Q');
    t->paste_command = command;
    return t->paste_result;
}

static void edit_commands(void)
{
    EditTest t;
    MMDocumentEditServices s = {0};
    s.context = &t;
    s.copy = edit_copy;
    s.clipboard_empty = edit_empty;
    s.clipboard_is_self = edit_self;
    s.publish = edit_publish;
    s.release = edit_release;
    s.clear = edit_clear;
    s.paste = edit_paste;

    memset(&t, 0, sizeof(t));
    CHECK(!mm_document_edit_enabled(MM_DOC_COMMAND_CUT, &s) && !strcmp(t.calls, "?"));
    t.count = 0;
    CHECK(mm_document_edit_clipboard(MM_DOC_COMMAND_CUT, &s) == 1 && !strcmp(t.calls, "C"));
    t.selection = 1;
    t.count = 0;
    CHECK(mm_document_edit_clipboard(MM_DOC_COMMAND_CUT, &s) == 1 && !strcmp(t.calls, "CPRX") && t.refs == 1);
    t.count = 0;
    CHECK(mm_document_edit_clipboard(MM_DOC_COMMAND_CLEAR, &s) == 1 && !strcmp(t.calls, "X"));

    t.empty = 1;
    t.count = 0;
    CHECK(!mm_document_edit_enabled(MM_DOC_COMMAND_PASTE, &s) && !strcmp(t.calls, "E"));
    t.empty = 0;
    t.self = 1;
    t.count = 0;
    CHECK(mm_document_edit_clipboard(MM_DOC_COMMAND_PASTE, &s) == 1 && !strcmp(t.calls, "ES"));
    t.self = 0;
    t.paste_result = 1;
    t.count = 0;
    CHECK(mm_document_edit_enabled(MM_DOC_COMMAND_PASTE_SPECIAL, &s) == 1 && !strcmp(t.calls, "ESQ"));
    CHECK(t.paste_command == MM_DOC_COMMAND_PASTE_SPECIAL);
    t.paste_result = 0;
    t.count = 0;
    CHECK(mm_document_edit_clipboard(MM_DOC_COMMAND_PASTE, &s) == 1 && !strcmp(t.calls, "ESA"));
    t.paste_result = -1;
    t.count = 0;
    CHECK(mm_document_edit_clipboard(MM_DOC_COMMAND_PASTE, &s) == -1 && !strcmp(t.calls, "ESA"));
}

int main(void)
{
    storage();
    presentation();
    names();
    model();
    edit_commands();
    puts("document views and edit command tests passed");
    return 0;
}
