/* Independent geometry/event oracle and interrupted drawing transactions. */
#include "mm/help_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "help display failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(c) check(!!(c), __LINE__)
typedef struct Host
{
    MMHelpDisplayServices s;
    MMHelpDisplay display;
    unsigned char header[8];
    MMI32 size, x;
    MMU32 type, number, back, fore, style;
    MMMaskedRect raw, filled, framed, drawn;
    int cache, bitmap, refs, partial, mutate, frame_pattern;
    int fills, draws, frames, pens, result, occurrence, seen;
    char log[256], fail;
    unsigned used;
} Host;
static int effect(Host *h, char tag)
{
    CHECK(h->used < 255);
    h->log[h->used++] = tag;
    h->log[h->used] = 0;
    return h->fail == tag && ++h->seen == h->occurrence ? h->result : 1;
}
static void wire(unsigned char *p, MMU32 n, int big)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[big ? 3 - i : i] = (unsigned char)(n >> (i * 8));
}
static int equal(const MMMaskedRect *r, MMI32 l, MMI32 t, MMI32 right, MMI32 b)
{
    return r->left == l && r->top == t && r->right == right && r->bottom == b;
}
static MMI32 bytes(void *v, MMI32 index)
{
    Host *h = (Host *)v;
    CHECK(index == 3);
    effect(h, 'q');
    return h->size;
}
static void header(void *v, MMI32 index, unsigned char *out)
{
    Host *h = (Host *)v;
    CHECK(index == 3 && h->size >= 8);
    effect(h, 'h');
    memcpy(out, h->header, 8);
}
static int find(void *v, const MMTextChunk *p, void **out)
{
    Host *h = (Host *)v;
    CHECK(p->type == MM_HELP_BUTTON && p->number == h->number);
    *out = &h->cache;
    return effect(h, 'F');
}
static int child(void *v, void *cache, const MMTextChunk *p, MMU32 id, MMTextChunk *out)
{
    Host *h = (Host *)v;
    CHECK(cache == &h->cache && p->type == MM_HELP_BUTTON && p->number == h->number && id == 0x10000UL);
    out->type = MM_HELP_BITMAP;
    out->number = 23;
    return effect(h, 'C');
}
static int fetch(void *v, void *cache, const MMTextChunk *p, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'A');
    CHECK(p->type == MM_HELP_BITMAP);
    CHECK(h->type == MM_HELP_BUTTON ? cache == &h->cache && p->number == 23 : !cache && p->number == h->number);
    CHECK(h->refs == 0);
    if (r > 0 || h->partial)
    {
        *out = &h->bitmap;
        ++h->refs;
    }
    return r;
}
static int rectangle(void *v, void *bitmap, MMMaskedRect *out)
{
    Host *h = (Host *)v;
    CHECK(bitmap == &h->bitmap && h->refs == 1);
    *out = h->raw;
    return effect(h, 'R');
}
static void release(void *v, void *bitmap)
{
    Host *h = (Host *)v;
    CHECK(bitmap == &h->bitmap && h->refs == 1);
    --h->refs;
    effect(h, 'r');
    if (h->mutate == 'r')
    {
        h->style = 8;
        h->fore = 0x01010203UL;
        h->x = 40;
    }
}
static int font(void *v)
{
    return effect((Host *)v, 'N');
}
static int measure(void *v, MMI32 baseline, MMMaskedRect *out)
{
    out->left = 123;
    out->right = 456;
    out->top = baseline - 8;
    out->bottom = baseline + 3;
    return effect((Host *)v, 'M');
}
static MMU32 color(void *v, int foreground)
{
    Host *h = (Host *)v;
    effect(h, foreground ? 'f' : 'b');
    return foreground ? h->fore : h->back;
}
static MMU32 style(void *v)
{
    effect((Host *)v, 'S');
    return ((Host *)v)->style;
}
static MMI32 x(void *v)
{
    effect((Host *)v, 'x');
    return ((Host *)v)->x;
}
static void publish_x(void *v, MMI32 n)
{
    Host *h = (Host *)v;
    effect(h, 'X');
    h->x = n;
}
static int fill(void *v, const MMMaskedRect *p, MMU32 c)
{
    Host *h = (Host *)v;
    CHECK(c == h->back);
    h->filled = *p;
    ++h->fills;
    return effect(h, 'B');
}
static int draw(void *v, void *bitmap, const MMMaskedRect *p)
{
    Host *h = (Host *)v;
    CHECK(bitmap == &h->bitmap && h->refs == 1);
    h->drawn = *p;
    ++h->draws;
    return effect(h, 'D');
}
static int pen(void *v)
{
    Host *h = (Host *)v;
    CHECK(!h->refs);
    ++h->pens;
    if (h->mutate == 'P')
        h->fore = 0x01010203UL;
    return effect(h, 'P');
}
static int frame(void *v, const MMMaskedRect *p, int patterned, MMU32 c)
{
    Host *h = (Host *)v;
    CHECK(!h->refs && (patterned ? c == h->fore : c == 0x01000000UL));
    if (h->type == MM_HELP_EDIT)
        CHECK(h->x == p->right);
    h->framed = *p;
    h->frame_pattern = patterned;
    ++h->frames;
    if (h->mutate == 'T')
    {
        h->x = 50;
        h->back = 0x01010203UL;
    }
    return effect(h, 'T');
}
static void init(Host *h, MMU32 type, MMU32 number, int big)
{
    memset(h, 0, sizeof(*h));
    h->type = type;
    h->number = number;
    wire(h->header, type, big);
    wire(h->header + 4, number, big);
    h->raw.left = -3;
    h->raw.top = 2;
    h->raw.right = 5;
    h->raw.bottom = 7;
    h->size = 12;
    h->x = 6;
    h->back = 0xffffffffUL;
    h->fore = 0x01ff0000UL;
    h->result = -1;
    h->occurrence = 1;
    mm_help_display_init(&h->display);
    h->s.context = h;
    h->s.bytes = bytes;
    h->s.header = header;
    h->s.find = find;
    h->s.child = child;
    h->s.fetch = fetch;
    h->s.rectangle = rectangle;
    h->s.release = release;
    h->s.font = font;
    h->s.measure = measure;
    h->s.color = color;
    h->s.style = style;
    h->s.x = x;
    h->s.publish_x = publish_x;
    h->s.fill = fill;
    h->s.draw = draw;
    h->s.pen = pen;
    h->s.frame = frame;
}
static int paint(Host *h, int big)
{
    return mm_help_object_draw(&h->display, 3, 192, 16, big, &h->s);
}
static void normal(int big)
{
    Host h;
    MMMaskedRect r;
    unsigned flags, button;
    for (button = 0; button <= 1; ++button)
        for (flags = 0; flags < 8; ++flags)
        {
            init(&h, button ? MM_HELP_BUTTON : MM_HELP_BITMAP, 42, big);
            CHECK(mm_help_object_bounds(3, 192, big, &r, &h.s));
            CHECK(equal(&r, 0, -5, 8, 0) && !h.refs);
            CHECK(!strcmp(h.log, button ? "qhFCARr" : "qhARr"));
            h.used = 0;
            h.style = flags & 1 ? 8 : 0;
            h.back = flags & 2 ? 0x01020304UL : 0xffffffffUL;
            mm_help_display_hide(&h.display, flags & 4);
            CHECK(h.display.hide_buttons == !!(flags & 4));
            CHECK(paint(&h, big) && !h.refs && h.x == 14);
            CHECK(h.draws == !(button && (flags & 4)) && h.fills == !!(flags & 2) && h.frames == !!(flags & 1));
            if (h.draws)
                CHECK(equal(&h.drawn, 6, 11, 14, 16));
            if (h.fills)
                CHECK(equal(&h.filled, 6, 11, 14, 16));
            if (h.frames)
                CHECK(h.frame_pattern && equal(&h.framed, 6, 11, 14, 16));
        }
    init(&h, MM_HELP_EDIT, 18, big);
    CHECK(mm_help_object_bounds(3, 192, big, &r, &h.s));
    CHECK(equal(&r, 0, -9, 18, 4) && !strcmp(h.log, "qhNM"));
    h.used = 0;
    CHECK(paint(&h, big) && h.x == 24 && h.fills == 1 && !h.draws && h.frames == 1 && !h.frame_pattern);
    CHECK(equal(&h.framed, 6, 7, 24, 20) && equal(&h.filled, 7, 8, 23, 19));
    CHECK(!strcmp(h.log, "qhNMxXPTbB"));
    init(&h, MM_HELP_EDIT, 0xfffffffcUL, big);
    CHECK(paint(&h, big) && h.x == 2 && equal(&h.framed, 6, 7, 2, 20));
    init(&h, MM_HELP_BUTTON, 42, big);
    h.mutate = 'r';
    CHECK(paint(&h, big) && h.frames == 1 && h.x == 48);
    init(&h, MM_HELP_BITMAP, 42, big);
    h.style = 8;
    h.mutate = 'P';
    CHECK(paint(&h, big));
    init(&h, MM_HELP_BITMAP, 42, big);
    h.style = 8;
    h.mutate = 'T';
    CHECK(paint(&h, big) && h.x == 58);
    init(&h, MM_HELP_EDIT, 18, big);
    h.mutate = 'T';
    CHECK(paint(&h, big) && h.x == 50 && h.filled.right == 23);
    init(&h, MM_HELP_BITMAP, 42, big);
    h.x = 2147483647;
    CHECK(paint(&h, big) && h.x == (-2147483647 - 1) + 7);
}
static void faults(int big)
{
    Host h;
    MMMaskedRect r;
    const char *stages = "FCARBDPT";
    unsigned i;
    int n, result;
    for (i = 0; stages[i]; ++i)
        for (result = -2; result <= 0; ++result)
        {
            init(&h, MM_HELP_BUTTON, 42, big);
            h.back = 0x01000000UL;
            h.style = 8;
            h.partial = 1;
            h.fail = stages[i];
            h.result = result;
            CHECK(paint(&h, big) == result && !h.refs && h.x == 6);
            CHECK(h.log[h.used - 1] == (strchr("ARBD", stages[i]) ? 'r' : stages[i]));
        }
    stages = "NMP TB";
    for (i = 0; stages[i]; ++i)
        if (stages[i] != ' ')
            for (result = -2; result <= 0; ++result)
            {
                init(&h, MM_HELP_EDIT, 18, big);
                h.fail = stages[i];
                h.result = result;
                CHECK(paint(&h, big) == result && !h.refs);
                CHECK(h.x == (strchr("PTB", stages[i]) ? 24 : 6));
                CHECK(h.log[h.used - 1] == stages[i]);
            }
    for (n = 0; n < 8; ++n)
    {
        init(&h, MM_HELP_BITMAP, 42, big);
        h.size = n;
        r.left = 71;
        r.top = 72;
        r.right = 73;
        r.bottom = 74;
        CHECK(!mm_help_object_bounds(3, 192, big, &r, &h.s) && equal(&r, 71, 72, 73, 74));
        CHECK(!paint(&h, big) && !h.refs && h.x == 6 && !strcmp(h.log, "qq"));
    }
    init(&h, MM_HELP_BITMAP, 42, big);
    CHECK(!mm_help_object_bounds(3, 194, big, &r, &h.s) && !h.used);
    CHECK(!mm_help_object_draw(&h.display, 3, 64, 16, big, &h.s) && !h.used);
    init(&h, 0x3f3f3f3fUL, 42, big);
    CHECK(!paint(&h, big) && !strcmp(h.log, "qh"));
    for (result = -2; result <= 0; ++result)
    {
        init(&h, MM_HELP_BITMAP, 42, big);
        h.partial = 1;
        h.fail = 'A';
        h.result = result;
        r.left = 71;
        r.top = 72;
        r.right = 73;
        r.bottom = 74;
        CHECK(mm_help_object_bounds(3, 192, big, &r, &h.s) == result && !h.refs && equal(&r, 71, 72, 73, 74));
        init(&h, MM_HELP_BITMAP, 42, big);
        h.fail = 'R';
        h.result = result;
        CHECK(mm_help_object_bounds(3, 192, big, &r, &h.s) == result && !h.refs && equal(&r, -3, 2, 5, 7));
    }
}
int main(void)
{
    normal(0);
    normal(1);
    faults(0);
    faults(1);
    return 0;
}
