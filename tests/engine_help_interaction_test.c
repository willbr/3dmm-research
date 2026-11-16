#include "mm/help_interaction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "help interaction failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(c) check(!!(c), __LINE__)
typedef struct Host
{
    MMHelpInteraction state, *heap;
    MMHelpInteractionServices s;
    MMHelpScriptServices script;
    MMHelpMouse mouse;
    MMHelpKey key;
    unsigned group;
    MMU32 target, adjusted, cursor_flags, number;
    int found, key_mode, captured, mutation, calls, balloon_calls, destroyed;
    int run_result, run_value, partial, script_refs, interpreter_refs, script_token, interpreter_token;
    int alive, snapshot, has_return, return_value, reset_count, nodes[4], kid_at;
    int result, occurrence, seen;
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
static void mouse(void *v, MMHelpMouse *out)
{
    *out = ((Host *)v)->mouse;
}
static void key(void *v, MMHelpKey *out)
{
    *out = ((Host *)v)->key;
}
static void publish(void *v, MMU32 flags)
{
    Host *h = (Host *)v;
    effect(h, 'p');
    if (h->key_mode)
        h->key.flags = flags;
    else
        h->mouse.flags = flags;
}
static int adjust(void *v, MMU32 flags, MMU32 *out)
{
    Host *h = (Host *)v;
    *out = flags ^ 0x400;
    if (h->mutation == 'A')
    {
        h->key.id = 1002;
        h->mouse.x = 70;
        h->mouse.y = 80;
    }
    return effect(h, 'A');
}
static int hit(void *v, MMI32 x, MMI32 y, unsigned *g, MMU32 *t)
{
    Host *h = (Host *)v;
    int r = effect(h, 'H');
    CHECK(x == h->mouse.x && y == h->mouse.y);
    if (g)
        *g = h->group;
    if (t)
        *t = h->target;
    return r <= 0 ? r : h->found;
}
static void check_tracking(void *v, int down)
{
    Host *h = (Host *)v;
    CHECK(down ? !h->captured : h->captured);
    effect(h, 'c');
}
static int track(void *v, int start)
{
    Host *h = (Host *)v;
    h->captured = start;
    if (h->mutation == 'T')
    {
        h->mouse.flags ^= 0x800;
        h->mouse.x = 90;
    }
    return effect(h, start ? 'T' : 'E');
}
static int cursor(void *v, MMU32 flags)
{
    Host *h = (Host *)v;
    h->cursor_flags = flags;
    if (h->mutation == 'C')
        h->mouse.flags ^= 0x800;
    return effect(h, 'C');
}
static void destroy_state(Host *h)
{
    if (h->heap)
    {
        free(h->heap);
        h->heap = 0;
    }
    h->destroyed = 1;
}
static int run(void *v, unsigned g, MMU32 flags, MMI32 id, MMU32 ch, MMU32 topic, MMI32 *out)
{
    Host *h = (Host *)v;
    CHECK(!h->destroyed);
    ++h->calls;
    if (h->key_mode)
        CHECK(!out && g == (((MMU32)h->key.id - 1000) & 255) && flags == h->key.flags && id == h->key.id &&
              ch == h->key.character && topic == MM_TEXT_NIL);
    else
        CHECK(g == 7 && flags == 0x1234 && id == -17 && ch == 0 && topic == h->target && out && *out == 0);
    if (out)
        *out = h->run_value;
    if (h->mutation == 'R')
        destroy_state(h);
    effect(h, 'R');
    return h->fail == 'R' ? h->result : h->run_result;
}
static int do_hit(void *v, unsigned g, MMU32 topic, MMU32 flags, MMI32 id)
{
    Host *h = (Host *)v;
    CHECK(g == h->state.group && topic == h->state.topic && flags == h->state.flags && id == 0);
    ++h->calls;
    return effect(h, 'D');
}
static int balloon(void *v, MMU32 topic)
{
    Host *h = (Host *)v;
    CHECK(!h->destroyed && topic == h->target);
    ++h->balloon_calls;
    return effect(h, 'B');
}
static int base_point(void *v, MMI32 x, MMI32 y)
{
    Host *h = (Host *)v;
    CHECK(x == h->mouse.x && y == h->mouse.y);
    return effect(h, 'P');
}
static int locate(void *v, MMI32 x, MMI32 y, MMI32 *cp)
{
    Host *h = (Host *)v;
    CHECK(x == 12 && y == 13);
    *cp = 4;
    return effect(h, 'L');
}
static int group_at(void *v, MMI32 cp)
{
    CHECK(cp == 4);
    return effect((Host *)v, 'G');
}
static void *parent(void *v, void *node)
{
    Host *h = (Host *)v;
    int *p = (int *)node;
    CHECK(p >= h->nodes && p < h->nodes + 4);
    effect(h, 'n');
    return p == h->nodes + 3 ? 0 : p + 1;
}
static int kid(void *v, void *node)
{
    Host *h = (Host *)v;
    effect(h, 'k');
    return node == h->nodes + h->kid_at;
}
static int set_cursor(void *v, void *node, MMU32 flags)
{
    Host *h = (Host *)v;
    CHECK(h->kid_at ? node == h->nodes + h->kid_at && flags == (0x12 | 256) : !node && flags == 0x12);
    return effect(h, 'S');
}
static void reset_return(void *v)
{
    Host *h = (Host *)v;
    ++h->reset_count;
    effect(h, 'z');
    if (h->has_return)
        h->return_value = 0;
}
static int number(void *v, MMU32 *out)
{
    Host *h = (Host *)v;
    CHECK(!h->destroyed);
    *out = h->number;
    return effect(h, 'N');
}
static int fetch(void *v, MMU32 n, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'F');
    CHECK(n == 17 && !h->script_refs);
    if (r > 0 || h->partial)
    {
        *out = &h->script_token;
        ++h->script_refs;
    }
    return r;
}
static int create(void *v, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'I');
    CHECK(h->script_refs == 1 && !h->destroyed);
    if (r > 0 || h->partial)
    {
        *out = &h->interpreter_token;
        ++h->interpreter_refs;
    }
    return r;
}
static void snapshot(void *v)
{
    Host *h = (Host *)v;
    CHECK(!h->destroyed);
    h->snapshot = 1;
    effect(h, 's');
}
static int script_run(void *v, void *interpreter, void *script, const MMI32 *args)
{
    Host *h = (Host *)v;
    int r;
    CHECK(script == &h->script_token && interpreter == &h->interpreter_token && h->script_refs == 1 &&
          h->interpreter_refs == 1 && h->snapshot);
    CHECK(args[0] == 255 && (MMU32)args[1] == 0x87654321UL && args[2] == -17 && args[3] == 0x34 &&
          (MMU32)args[4] == 0xabcdef12UL);
    if (h->has_return)
        h->return_value = 55;
    destroy_state(h);
    r = effect(h, 'R');
    return r <= 0 ? r : h->run_result;
}
static void release(void *v, int interpreter, void *p)
{
    Host *h = (Host *)v;
    if (interpreter)
    {
        CHECK(p == &h->interpreter_token && h->interpreter_refs == 1);
        --h->interpreter_refs;
        effect(h, 'i');
    }
    else
    {
        CHECK(p == &h->script_token && h->script_refs == 1 && !h->interpreter_refs);
        --h->script_refs;
        effect(h, 'f');
    }
}
static int alive(void *v)
{
    Host *h = (Host *)v;
    CHECK(h->snapshot && h->destroyed && !h->interpreter_refs && h->script_refs == 1);
    effect(h, 'a');
    return h->alive;
}
static void init(Host *h)
{
    memset(h, 0, sizeof(*h));
    mm_help_interaction_init(&h->state);
    h->state.base = 1000;
    h->mouse.x = 12;
    h->mouse.y = 13;
    h->mouse.down = 1;
    h->mouse.flags = 10;
    h->key.id = 1001;
    h->key.flags = 9;
    h->key.character = 0x1234;
    h->group = 7;
    h->target = 99;
    h->found = h->run_result = h->alive = h->has_return = 1;
    h->number = 17;
    h->return_value = -77;
    h->occurrence = 1;
    h->result = -1;
    h->s.context = h;
    h->s.mouse = mouse;
    h->s.key = key;
    h->s.publish_flags = publish;
    h->s.adjust = adjust;
    h->s.hit = hit;
    h->s.check_tracking = check_tracking;
    h->s.track = track;
    h->s.cursor = cursor;
    h->s.run = run;
    h->s.do_hit = do_hit;
    h->s.balloon = balloon;
    h->s.base_point = base_point;
    h->s.locate = locate;
    h->s.group_at = group_at;
    h->s.parent = parent;
    h->s.kid = kid;
    h->s.set_cursor = set_cursor;
    h->script.context = h;
    h->script.reset_return = reset_return;
    h->script.number = number;
    h->script.fetch = fetch;
    h->script.create = create;
    h->script.snapshot = snapshot;
    h->script.run = script_run;
    h->script.release = release;
    h->script.alive = alive;
}
static void commands(void)
{
    Host h;
    int r, n;
    init(&h);
    CHECK(mm_help_track(&h.state, &h.s) && !strcmp(h.log, "ApcHTC") && h.state.group == 7 && h.state.topic == 99 &&
          h.state.flags == 0x40a);
    h.used = 0;
    h.mouse.down = 0;
    CHECK(mm_help_track(&h.state, &h.s) && !strcmp(h.log, "Apc") && !h.calls);
    h.used = 0;
    h.mouse.flags = 2;
    CHECK(mm_help_track(&h.state, &h.s) && !strcmp(h.log, "ApcEHD") && h.calls == 1 && !h.captured);
    init(&h);
    h.found = 0;
    CHECK(mm_help_track(&h.state, &h.s) && !h.captured && h.state.group == 7 && h.state.topic == 99);
    init(&h);
    h.mutation = 'C';
    CHECK(mm_help_track(&h.state, &h.s) && h.state.flags == 0xc0a);
    init(&h);
    h.mutation = 'T';
    CHECK(mm_help_track(&h.state, &h.s) && h.cursor_flags == 0xc0a && h.state.flags == 0xc0a);
    for (n = 0; n < 2; ++n)
    {
        init(&h);
        CHECK(mm_help_track(&h.state, &h.s));
        h.mouse.down = 0;
        h.mouse.flags = 0;
        if (n)
            ++h.target;
        else
            ++h.group;
        CHECK(mm_help_track(&h.state, &h.s) && !h.calls);
    }
    for (n = 999; n <= 1257; ++n)
    {
        init(&h);
        h.key_mode = 1;
        h.key.id = n;
        r = mm_help_bad_key(&h.state, &h.s);
        CHECK(r == (n > 1000 && n < 1257) && h.calls == r);
        if (!r)
            CHECK(!h.used && h.key.flags == 9);
    }
    init(&h);
    h.key_mode = 1;
    h.mutation = 'A';
    CHECK(mm_help_bad_key(&h.state, &h.s) && h.key.id == 1002);
    init(&h);
    h.key_mode = 1;
    h.run_result = 0;
    h.mutation = 'R';
    h.heap = (MMHelpInteraction *)malloc(sizeof(*h.heap));
    CHECK(h.heap);
    *h.heap = h.state;
    CHECK(mm_help_bad_key(h.heap, &h.s) && h.destroyed && !h.heap);
    init(&h);
    h.mutation = 'A';
    CHECK(mm_help_mouse_move(&h.s) && h.cursor_flags == (0x40a | 512) && h.mouse.flags == 10);
    for (r = -2; r <= 1; ++r)
    {
        init(&h);
        h.fail = 'R';
        h.result = r;
        h.mutation = r <= 0 ? 'R' : 0;
        CHECK(mm_help_hit(7, h.target, 0x1234, -17, &h.s) == r && h.balloon_calls == (r > 0));
    }
    init(&h);
    h.run_value = 1;
    CHECK(mm_help_hit(7, h.target, 0x1234, -17, &h.s) && !h.balloon_calls);
    init(&h);
    h.target = MM_TEXT_NIL;
    CHECK(mm_help_hit(7, h.target, 0x1234, -17, &h.s) && !h.balloon_calls);
}
static void command_faults(void)
{
    Host h;
    const char *stages = "AHTC";
    unsigned i;
    int r;
    for (i = 0; stages[i]; ++i)
        for (r = -2; r <= 0; ++r)
        {
            init(&h);
            h.fail = stages[i];
            h.result = r;
            CHECK(mm_help_track(&h.state, &h.s) == (!r && stages[i] == 'H' ? 1 : r));
            CHECK(h.log[h.used - 1] == stages[i] && !h.state.flags);
        }
    for (r = -2; r <= 0; ++r)
    {
        init(&h);
        h.fail = 'P';
        h.result = r;
        CHECK(mm_help_point(12, 13, &h.s) == r && !strcmp(h.log, "P"));
        init(&h);
        h.fail = 'L';
        h.result = r;
        CHECK(mm_help_group_point(12, 13, &h.s) == r && !strcmp(h.log, "L"));
    }
    init(&h);
    CHECK(mm_help_point(12, 13, &h.s) && !strcmp(h.log, "PH"));
    init(&h);
    CHECK(mm_help_group_point(12, 13, &h.s) && !strcmp(h.log, "LG"));
    init(&h);
    CHECK(mm_help_cursor(h.nodes, 0x12, &h.s) && !strcmp(h.log, "nknknknS"));
    init(&h);
    h.kid_at = 2;
    CHECK(mm_help_cursor(h.nodes, 0x12, &h.s) && !strcmp(h.log, "nknkS"));
}
static int execute(Host *h)
{
    return mm_help_script(255, 0x87654321UL, -17, 0x1234, 0xabcdef12UL, &h->script);
}
static void scripts(void)
{
    Host h;
    const char *stages = "NFIR";
    unsigned i;
    int r, has_return, partial;
    for (has_return = 0; has_return < 2; ++has_return)
        for (r = -2; r <= 1; ++r)
        {
            init(&h);
            h.has_return = has_return;
            h.alive = r;
            CHECK(execute(&h) == r && !h.script_refs && !h.interpreter_refs && !strcmp(h.log, "zNFIsRiaf"));
            CHECK(h.return_value == (has_return ? 55 : -77));
        }
    init(&h);
    h.number = MM_TEXT_NIL;
    CHECK(execute(&h) && h.return_value == 0 && !strcmp(h.log, "zN"));
    init(&h);
    h.run_result = 0;
    CHECK(execute(&h) && h.return_value == 0 && h.reset_count == 2 && !strcmp(h.log, "zNFIsRziaf"));
    for (i = 0; stages[i]; ++i)
        for (partial = 0; partial < 2; ++partial)
            for (r = -2; r <= 0; ++r)
            {
                init(&h);
                h.fail = stages[i];
                h.result = r;
                h.partial = partial;
                CHECK(execute(&h) == (r ? r : stages[i] == 'N' ? 0 : 1));
                CHECK(!h.script_refs && !h.interpreter_refs);
                if (stages[i] == 'R')
                    CHECK(h.return_value == (r ? 55 : 0));
                else
                    CHECK(h.return_value == 0 && !strchr(h.log, 'a'));
                if (r)
                    CHECK(!strchr(h.log, 'a') && h.reset_count == 1);
            }
}
int main(void)
{
    commands();
    command_faults();
    scripts();
    return 0;
}
