/* Independent object/header, geometry, handler and effect-order probes. */
#include "mm/help_topic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "help topic failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(c) check(!!(c), __LINE__)
typedef struct Entry
{
    MMI32 cp, bytes;
    int missing;
    unsigned char data[16];
} Entry;
typedef struct Host
{
    MMHelpInteraction state;
    MMHelpTopicServices s;
    Entry entries[8];
    MMI32 length, unique_next, current_cp, occupied[8];
    unsigned occupied_count, index, edits, buttons, completed, frees, unique_calls, scans;
    int big, live, partial, empty, ungrouped, group_zero, all_collide, source, cached, scan_source, mutation;
    void *owned;
    unsigned control_group;
    MMU32 control_target;
    char log[1024], fail;
    unsigned used;
    int result, occurrence, seen;
} Host;
static int effect(Host *h, char tag)
{
    CHECK(h->used < 1023);
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
static int base(void *v)
{
    Host *h = (Host *)v;
    CHECK(h->state.base == 77);
    h->source = 2;
    return effect(h, 'B');
}
static MMI32 characters(void *v)
{
    Host *h = (Host *)v;
    CHECK(h->scan_source == 1);
    return h->empty ? 0 : h->length;
}
static int scan(void *v, MMI32 cp, MMI32 *end, unsigned *g)
{
    Host *h = (Host *)v;
    int r = effect(h, 'S');
    CHECK(h->state.base == 77 && h->scan_source == 1);
    ++h->scans;
    CHECK(cp == 0 || cp == 2 || cp == 7);
    *end = cp == 0 ? 2 : cp == 2 ? 7 : 10;
    *g = h->ungrouped || !r ? 0 : cp == 0 ? 3 : cp == 2 ? 9 : 0;
    return r;
}
static int unique(void *v, MMI32 count, MMI32 *out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'U');
    ++h->unique_calls;
    CHECK(count == 1 || count == 9);
    if (count == 9)
    {
        CHECK(!h->cached && h->state.base == 0);
        *out = 100;
    }
    else
        *out = h->unique_next++;
    return r;
}
static int assign(void *v, MMI32 n)
{
    Host *h = (Host *)v;
    CHECK(n == 99 && h->state.base == 99 && !h->cached);
    return effect(h, 'A');
}
static void cache(void *v)
{
    Host *h = (Host *)v;
    CHECK(h->state.base == (h->empty || h->ungrouped ? 0 : 99));
    h->cached = h->source;
    effect(h, 'c');
}
static int fetch(void *v, MMI32 from, MMI32 *at, void **out, MMI32 *bytes)
{
    Host *h = (Host *)v;
    Entry *e;
    int r = effect(h, 'F');
    CHECK(!h->live && h->cached == 2 && h->source >= 2);
    if (h->index >= 8)
    {
        *at = 10;
        return r < 0 ? r : 0;
    }
    e = &h->entries[h->index++];
    CHECK(e->cp >= from);
    *at = e->cp;
    *bytes = e->bytes;
    if ((r > 0 && !e->missing) || (r <= 0 && h->partial))
    {
        h->owned = malloc((size_t)e->bytes);
        CHECK(h->owned);
        memcpy(h->owned, e->data, (size_t)e->bytes);
        *out = h->owned;
        h->live = 1;
    }
    h->source = 3;
    return r;
}
static void free_payload(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(h->live && p == h->owned);
    h->live = 0;
    ++h->frees;
    free(p);
    h->owned = 0;
    effect(h, 'f');
}
static int position(void *v, MMI32 cp, MMI32 *x, MMI32 *y)
{
    Host *h = (Host *)v;
    CHECK(!h->live && h->source == 3);
    h->current_cp = cp;
    *x = cp * 10;
    *y = 20;
    return effect(h, 'P');
}
static int character(void *v, MMI32 cp, MMTextCharacter *out)
{
    Host *h = (Host *)v;
    CHECK(cp == h->current_cp && !h->live);
    out->style = 8;
    out->font = 17;
    out->height = 12;
    out->offset = -2;
    out->foreground = 0x01010203UL;
    out->background = cp == 1 ? 0xffffffffUL : 0x010000ffUL;
    return effect(h, 'H');
}
static int font(void *v, const MMTextCharacter *p)
{
    Host *h = (Host *)v;
    CHECK(p->style == 8 && p->font == 17 && p->height == 12 && !h->live);
    return effect(h, 'N');
}
static int measure(void *v, MMMaskedRect *out)
{
    out->left = -1;
    out->right = 2;
    out->top = -8;
    out->bottom = 3;
    return effect((Host *)v, 'M');
}
static int group(void *v, MMI32 cp, unsigned *g, MMU32 *target)
{
    Host *h = (Host *)v;
    int r = effect(h, 'G');
    CHECK(cp == h->current_cp && !h->live && h->source == 3);
    *g = h->group_zero || !r ? 0 : (cp == 1 || cp == 3) ? 3 : 0;
    CHECK((cp == 3) == (target != 0));
    if (target)
        *target = !r ? MM_TEXT_NIL : 99;
    h->control_group = *g;
    h->control_target = target ? *target : 77;
    return r;
}
static int exists(void *v, MMI32 id)
{
    Host *h = (Host *)v;
    unsigned i;
    int r = effect(h, 'E');
    CHECK(id == h->state.base + 3);
    if (r <= 0)
        return r;
    if (h->all_collide)
        return 1;
    for (i = 0; i < h->occupied_count; ++i)
        if (h->occupied[i] == id)
            return 1;
    return 0;
}
static void reserve_id(Host *h, MMI32 id)
{
    unsigned i;
    for (i = 0; i < h->occupied_count; ++i)
        CHECK(h->occupied[i] != id);
    h->occupied[h->occupied_count++] = id;
    ++h->completed;
}
static int edit(void *v, MMI32 id, const MMMaskedRect *r, const MMTextCharacter *p)
{
    Host *h = (Host *)v;
    MMI32 cp = h->current_cp;
    int result = effect(h, 'D');
    CHECK(!h->live && (cp == 1 || cp == 8));
    CHECK(r->left == cp * 10 + 1 && r->right == cp * 10 + (cp == 1 ? 18 : -4) - 1 && r->top == 10 && r->bottom == 21);
    CHECK(p->foreground == 0x01010203UL && p->background == (cp == 1 ? 0x01ffffffUL : 0x010000ffUL) && p->offset == -2);
    ++h->edits;
    if (result > 0)
        reserve_id(h, id);
    return result;
}
static int button(void *v, MMI32 id, MMU32 resource, unsigned g, MMU32 target, MMI32 x, MMI32 y)
{
    Host *h = (Host *)v;
    MMI32 cp = h->current_cp;
    int result = effect(h, 'T');
    CHECK(!h->live && (cp == 3 || cp == 4) && h->cached == 2);
    CHECK(resource == (cp == 3 ? 31UL : 32UL) && target == h->control_target);
    CHECK(g == h->control_group);
    CHECK(x == cp * 10 && y == 18);
    ++h->buttons;
    if (result > 0)
        reserve_id(h, id);
    return result;
}
static void validate(void *v)
{
    Host *h = (Host *)v;
    CHECK(!h->live);
    effect(h, 'V');
}
static void entry(Host *h, unsigned i, MMI32 cp, MMU32 type, MMU32 word1, MMU32 word2, MMI32 bytes)
{
    Entry *e = &h->entries[i];
    e->cp = cp;
    e->bytes = bytes;
    memset(e->data, 0x55, sizeof(e->data));
    wire(e->data, type, h->big);
    wire(e->data + 4, word1, h->big);
    wire(e->data + 8, word2, h->big);
}
static void init(Host *h, int big)
{
    memset(h, 0, sizeof(*h));
    h->state.base = 77;
    h->big = big;
    h->length = 10;
    h->unique_next = 1000;
    h->source = h->scan_source = 1;
    h->result = -1;
    h->occurrence = 1;
    h->s.context = h;
    h->s.base = base;
    h->s.characters = characters;
    h->s.scan = scan;
    h->s.unique = unique;
    h->s.assign_base = assign;
    h->s.cache = cache;
    h->s.fetch = fetch;
    h->s.free = free_payload;
    h->s.position = position;
    h->s.character = character;
    h->s.font = font;
    h->s.measure = measure;
    h->s.group = group;
    h->s.exists = exists;
    h->s.edit = edit;
    h->s.button = button;
    h->s.validate = validate;
    entry(h, 0, 0, MM_HELP_BITMAP, 32, 0, 8);
    entry(h, 1, 1, MM_HELP_EDIT, 18, 0, 11);
    entry(h, 2, 2, MM_HELP_BUTTON, 31, 77, 12);
    h->entries[2].missing = 1;
    entry(h, 3, 3, MM_HELP_BUTTON, 31, MM_TEXT_NIL, 15);
    entry(h, 4, 4, MM_HELP_BUTTON, 32, 77, 12);
    entry(h, 5, 5, MM_HELP_BUTTON, 31, 99, 3);
    entry(h, 6, 6, 0x3f3f3f3fUL, 0, 0, 8);
    entry(h, 7, 8, MM_HELP_EDIT, 0xfffffffcUL, 0, 8);
}
static int build(Host *h)
{
    return mm_help_topic_build(&h->state, h->big, &h->s);
}
static void normal(int big)
{
    Host h;
    init(&h, big);
    CHECK(build(&h));
    CHECK(h.state.base == 99 && h.scans == 3 && h.completed == 4 && h.edits == 2 && h.buttons == 2 && h.frees == 7 &&
          !h.live);
    CHECK(h.occupied[0] == 102 && h.occupied[1] == 1000 && h.occupied[2] == 1001 && h.occupied[3] == 1002);
    CHECK(strstr(h.log, "fPHNMGED") && strstr(h.log, "fPHGEUT") && h.log[h.used - 1] == 'V');
    init(&h, big);
    h.all_collide = 1;
    CHECK(build(&h) && h.occupied[0] == 1000 && h.occupied[1] == 1001);
    init(&h, big);
    h.ungrouped = 1;
    h.group_zero = 1;
    CHECK(build(&h) && h.state.base == 0 && !strchr(h.log, 'A') && !strchr(h.log, 'E'));
    init(&h, big);
    h.empty = 1;
    h.index = 8;
    CHECK(build(&h) && h.state.base == 0 && !h.scans && !h.unique_calls && !strcmp(h.log, "BcFV"));
}
static void failures(int big)
{
    Host h;
    const char *stages = "BSUAFPHNMGEDT";
    unsigned i;
    int r, partial;
    for (i = 0; stages[i]; ++i)
        for (r = -2; r <= 0; ++r)
            for (partial = 0; partial < 2; ++partial)
            {
                init(&h, big);
                h.fail = stages[i];
                h.result = r;
                h.partial = partial;
                CHECK(build(&h) == (!r && strchr("SFGE", stages[i]) ? 1 : r));
                CHECK(!h.live);
                if (r || !strchr("SFGE", stages[i]))
                    CHECK(h.log[h.used - 1] == (stages[i] == 'F' && partial ? 'f' : stages[i]));
                if (stages[i] == 'B' || stages[i] == 'S')
                    CHECK(h.state.base == 77 || r == 0);
                else if (stages[i] == 'U')
                    CHECK(h.state.base == 0);
                else
                    CHECK(h.state.base == 99);
            }
    for (r = -2; r <= 0; ++r)
    {
        init(&h, big);
        h.fail = 'U';
        h.result = r;
        h.occurrence = 2;
        CHECK(build(&h) == r && h.completed == 1 && h.state.base == 99 && !h.live);
        init(&h, big);
        h.fail = 'T';
        h.result = r;
        h.occurrence = 2;
        CHECK(build(&h) == r && h.completed == 2 && !h.live);
    }
}
int main(void)
{
    normal(0);
    normal(1);
    failures(0);
    failures(1);
    return 0;
}
