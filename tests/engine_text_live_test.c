#include "mm/text_live.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "live text failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Host
{
    MMRichTextLiveServices services;
    MMRichTextState state;
    MMTextLiveOutput output;
    int calls, fail_at, failure, partial, root, removed, children, active, refs, errors, assert_move, assert_location,
        args;
    char log[128];
    unsigned used;
    MMI32 bytes;
    unsigned char marker[2];
} Host;
static void event(Host *h, char c)
{
    CHECK(h->used < 127);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static int step(Host *h, char c)
{
    event(h, c);
    return ++h->calls == h->fail_at ? h->failure : 1;
}
static MMU32 font_size(void *v, const MMMovieTitle *p)
{
    (void)v;
    CHECK(p->unit == 1 || p->unit == 2);
    return 8;
}
static int root(void *v, MMU32 bytes, MMU32 *number)
{
    Host *h = (Host *)v;
    int r = step(h, 'n');
    CHECK(bytes == 24 && h->output.chunk.type == MM_TEXT_ROOT);
    *number = 77;
    if (r > 0)
        h->root = 1;
    return r;
}
static int write(void *v, MMU32 offset, const void *p, MMU32 bytes)
{
    Host *h = (Host *)v;
    int r = step(h, h->active ? 'm' : 'h');
    CHECK(offset == 0 && bytes == (h->active ? 2U : 16U));
    if (r > 0 && h->active)
        memcpy(h->marker, p, 2);
    return r;
}
static int font(void *v, const MMMovieTitle *p)
{
    (void)p;
    return step((Host *)v, 'f');
}
static void remove_root(void *v, MMU32 number)
{
    Host *h = (Host *)v;
    CHECK(number == 77 && h->root);
    event(h, '!');
    h->root = h->children = 0;
    ++h->removed;
}
static int child(void *v, MMU32 number, MMU32 kind, MMU32 bytes)
{
    Host *h = (Host *)v;
    int r = step(h, kind == MM_TEXT_BYTES ? 't' : kind == MM_TEXT_PROPERTIES ? 'p' : 'a');
    CHECK(number == 77 && h->root && bytes == (kind == MM_TEXT_BYTES ? (MMU32)h->bytes + 2 : 20));
    if (r > 0)
    {
        ++h->children;
        h->active = kind == MM_TEXT_BYTES ? 1 : 2;
    }
    return r;
}
static MMU32 group_size(void *v, MMU32 kind, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == (kind == MM_TEXT_PROPERTIES ? (void *)&h->state.properties : (void *)&h->args));
    return 20;
}
static MMI32 count(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == &h->args);
    return h->args;
}
static int group(void *v, MMU32 kind, void *p, int order, int system)
{
    Host *h = (Host *)v;
    (void)p;
    CHECK(order == 1 && system == 0x1234);
    return step(h, kind == MM_TEXT_PROPERTIES ? 'g' : 'b');
}
static int move(void *v, MMU32 n)
{
    CHECK(n == 2);
    return step((Host *)v, 'v');
}
static int text(void *v)
{
    return step((Host *)v, 'w');
}
static int destination(void *v, MMTextLocation *out)
{
    Host *h = (Host *)v;
    int r = step(h, 'd');
    CHECK(h->root && h->children >= 2);
    if (r > 0 || h->partial)
    {
        out->file = &h->refs;
        out->offset = 42;
        out->bytes = h->bytes;
        ++h->refs;
    }
    return r;
}
static int redirect(void *v, const MMTextLocation *p, MMI32 bytes)
{
    Host *h = (Host *)v;
    CHECK(p->file == &h->refs && p->offset == 42 && p->bytes == h->bytes && bytes == h->bytes);
    return step(h, 'r');
}
static void release(void *v, MMU32 kind, void *p)
{
    Host *h = (Host *)v;
    CHECK(kind == MM_TEXT_BYTES && p == &h->refs && h->refs == 1);
    --h->refs;
    event(h, 'z');
}
static void assertion(void *v, int stage)
{
    Host *h = (Host *)v;
    if (stage == MM_TEXT_ASSERT_MOVE)
    {
        ++h->assert_move;
        event(h, 'V');
    }
    else
    {
        CHECK(stage == MM_TEXT_ASSERT_LOCATION);
        ++h->assert_location;
        event(h, 'D');
    }
}
static void error(void *v)
{
    Host *h = (Host *)v;
    CHECK(h->output.chunk.type == MM_TEXT_ROOT);
    ++h->errors;
    event(h, 'E');
}
static void setup(Host *h, unsigned unit)
{
    MMRichTextLiveServices *s = &h->services;
    memset(h, 0, sizeof(*h));
    CHECK(mm_rich_text_init(&h->state, unit, 0x303));
    h->state.text_system = 0x1234;
    h->bytes = h->state.text.bytes = 4;
    h->args = 1;
    h->state.property_order = h->state.argument_order = 256;
    h->state.property_system = h->state.argument_system = 0x202;
    h->state.properties = &h->state.properties;
    h->state.arguments = &h->args;
    h->output.chunk.type = 22;
    h->output.chunk.number = 33;
    s->write.context = h;
    s->write.objects.context = h;
    s->write.objects.release = release;
    s->write.font_bytes = font_size;
    s->write.add_root = root;
    s->write.write = write;
    s->write.font = font;
    s->write.remove = remove_root;
    s->write.add_child = child;
    s->write.group_bytes = group_size;
    s->write.count = count;
    s->write.group = group;
    s->context = h;
    s->move_text = move;
    s->text = text;
    s->destination = destination;
    s->redirect = redirect;
    s->assertion = assertion;
    s->error = error;
}
static void saves(void)
{
    Host h;
    char effects[32];
    int n, i, f, r, big, partial, redirecting;
    unsigned unit;
    for (unit = 1; unit <= 2; ++unit)
        for (big = 0; big < 2; ++big)
            for (redirecting = 0; redirecting < 2; ++redirecting)
            {
                setup(&h, unit);
                CHECK(mm_rich_text_save(&h.state, &h.output, big, redirecting, &h.services) == 1 && h.root &&
                      h.children == 3 && !h.refs && !h.errors && h.output.number_changed &&
                      h.output.chunk.number == 77);
                CHECK(h.marker[big ? 1 : 0] == 0x34 && h.marker[big ? 0 : 1] == 0x12);
                CHECK(!strcmp(h.log, redirecting ? "nhftmvwpgabdrz" : "nhftmvwpgab"));
                n = h.calls;
                memcpy(effects, h.log, (size_t)n);
                effects[n] = 0;
                for (partial = 0; partial < 2; ++partial)
                    for (f = -2; f <= 0; ++f)
                        for (i = 1; i <= n; ++i)
                        {
                            setup(&h, unit);
                            h.fail_at = i;
                            h.failure = f;
                            h.partial = partial;
                            r = mm_rich_text_save(&h.state, &h.output, big, redirecting, &h.services);
                            CHECK(!h.refs && h.output.number_changed && h.output.chunk.type == MM_TEXT_ROOT);
                            if (!f && (effects[i - 1] == 'v' || effects[i - 1] == 'r'))
                            {
                                CHECK(r == 1 && h.root && !h.errors && !h.removed);
                                CHECK(h.assert_move == (effects[i - 1] == 'v'));
                            }
                            else if (effects[i - 1] == 'd' || effects[i - 1] == 'r')
                            {
                                CHECK(r == f && h.root && h.children == 3 && !h.removed && !h.errors);
                                CHECK(h.assert_location == (!f));
                            }
                            else
                            {
                                CHECK(r == f && !h.root && !h.children && h.errors == (!f) && h.removed == (i != 1));
                                CHECK(h.output.chunk.number == (i <= 3 ? MM_TEXT_NIL : 77U));
                                if (h.removed && !f)
                                    CHECK(strchr(h.log, '!') < strchr(h.log, 'E'));
                            }
                        }
            }
    setup(&h, 1);
    h.state.defaults.height = 3;
    CHECK(!mm_rich_text_save(&h.state, &h.output, 0, 0, &h.services) && !h.calls && h.errors == 1 &&
          h.output.chunk.type == MM_TEXT_ROOT && h.output.chunk.number == 33 && !h.output.number_changed);
    setup(&h, 1);
    h.bytes = h.state.text.bytes = 0;
    h.args = 0;
    CHECK(mm_rich_text_save(&h.state, &h.output, 0, 1, &h.services) == 1 && h.children == 2 &&
          !strcmp(h.log, "nhftmvwpgdrz"));
    setup(&h, 1);
    h.state.arguments = 0;
    CHECK(mm_rich_text_save(&h.state, &h.output, 0, 0, &h.services) == 1 && h.children == 2);
}
typedef struct Box
{
    MMTextBoxStoreServices services;
    char log[32];
    unsigned used;
    int calls, fail_at, failure, root, linked, header, removed, child, asserted, other;
} Box;
static void box_event(Box *h, char c)
{
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static int box_step(Box *h, char c)
{
    box_event(h, c);
    return ++h->calls == h->fail_at ? h->failure : 1;
}
static void reset(void *v)
{
    Box *h = (Box *)v;
    box_event(h, 'R');
    h->child = 0;
}
static int save(void *v, MMTextChunk *out)
{
    Box *h = (Box *)v;
    int r = box_step(h, 'S');
    CHECK(h->child == 0);
    h->root = 1;
    out->type = h->other ? 22 : MM_TEXT_ROOT;
    out->number = 99;
    return r;
}
static void assert_type(void *v)
{
    Box *h = (Box *)v;
    ++h->asserted;
    box_event(h, 'T');
}
static int adopt(void *v, MMU32 box, const MMTextChunk *p)
{
    Box *h = (Box *)v;
    int r = box_step(h, 'A');
    CHECK(box == 7 && p->number == 99 && h->root);
    if (r > 0)
        h->linked = 1;
    return r;
}
static int metadata(void *v, MMU32 box)
{
    Box *h = (Box *)v;
    CHECK(box == 7 && h->linked);
    h->header = 1;
    return box_step(h, 'M');
}
static void remove_box(void *v, MMU32 box, const MMTextChunk *p, int linked)
{
    Box *h = (Box *)v;
    CHECK(box == 7 && p->number == 99 && h->linked == linked);
    ++h->removed;
    h->root = h->linked = 0;
    box_event(h, linked ? 'L' : 'D');
}
static void box_setup(Box *h)
{
    memset(h, 0, sizeof(*h));
    h->child = 7;
    h->services.context = h;
    h->services.reset_child = reset;
    h->services.save = save;
    h->services.assert_type = assert_type;
    h->services.adopt = adopt;
    h->services.metadata = metadata;
    h->services.remove = remove_box;
}
static void boxes(void)
{
    Box h;
    int f, i;
    box_setup(&h);
    CHECK(mm_text_box_store(7, &h.services) == 1 && !strcmp(h.log, "RSAM") && !h.child && h.root && h.linked &&
          h.header);
    for (f = -2; f <= 0; ++f)
        for (i = 1; i <= 3; ++i)
        {
            box_setup(&h);
            h.fail_at = i;
            h.failure = f;
            CHECK(mm_text_box_store(7, &h.services) == f && !h.child);
            CHECK(!strcmp(h.log, i == 1 ? "RS" : i == 2 ? "RSAD" : "RSAML"));
            CHECK(h.root == (i == 1) && h.removed == (i != 1) && h.header == (i == 3));
        }
    box_setup(&h);
    h.other = 1;
    CHECK(mm_text_box_store(7, &h.services) == 1 && h.asserted == 1 && !strcmp(h.log, "RSTAM"));
}
int main(void)
{
    saves();
    boxes();
    puts(
        "Live text: save output prefixes, native groups, empty text, redirect ownership and text-box rollback passed.");
    return 0;
}
