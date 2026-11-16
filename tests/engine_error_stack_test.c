#include "mm/error_stack.h"
#include <assert.h>
#include <string.h>
typedef struct Probe {
    MMErrorStackView view;
    unsigned pushes, overflows, writes, trashes;
    int depth;
    char events[4096]; unsigned n;
} Probe;
static void event(Probe *p, char c) { assert(p->n < sizeof(p->events) - 1); p->events[p->n++] = c; p->events[p->n] = 0; }
static void enter(void *context) { Probe *p = (Probe *)context; assert(!p->depth); ++p->depth; event(p, 'L'); }
static void leave(void *context) { Probe *p = (Probe *)context; assert(p->depth == 1); --p->depth; event(p, 'U'); }
static void report(void *context, MMI32 code, const char *source, MMI32 line)
{
    Probe *p = (Probe *)context;
    (void)code; (void)source; (void)line;
    assert(!p->depth); ++p->pushes; event(p, 'P');
}
static void overflow(void *context)
{
    Probe *p = (Probe *)context;
    assert(p->depth == 1 && mm_error_stack_count(&p->view) == 20);
    ++p->overflows; event(p, 'O');
}
static void write(void *context, void *output, MMI32 code)
{
    Probe *p = (Probe *)context;
    assert(p->depth == 1); ++p->writes; event(p, 'W'); *(MMI32 *)output = code;
}
static void trash(void *context, void *output)
{
    Probe *p = (Probe *)context;
    assert(p->depth == 1 && !mm_error_stack_count(&p->view)); ++p->trashes; event(p, 'T');
    memset(output, 0xa3, sizeof(MMI32));
}
static void cases(int debug)
{
    MMErrorStack plain; MMErrorStackDebug detailed;
    Probe p; MMErrorStackHost h;
    MMI32 i, output = 123;
    unsigned char garbage[sizeof(MMI32)];
    memset(&p, 0, sizeof(p));
    p.view = debug ? mm_error_stack_debug_view(&detailed) : mm_error_stack_view(&plain);
    h.context = &p; h.enter = enter; h.leave = leave; h.report_push = report; h.report_overflow = overflow;
    h.write = write; h.underflow = trash;
    mm_error_stack_init(&p.view);
    assert(!mm_error_stack_pop(&p.view, &h, &output));
    memset(garbage, 0xa3, sizeof(garbage));
    assert(!memcmp(&output, garbage, sizeof(output)) && !strcmp(p.events, "LTU"));
    p.n = 0; p.events[0] = 0;
    assert(!mm_error_stack_pop(&p.view, &h, NULL) && !strcmp(p.events, "LU") && p.trashes == 1);
    for (i = 0; i < 25; ++i) mm_error_stack_push(&p.view, &h, i, "first", i);
    assert(p.pushes == 25 && p.overflows == 5 && *p.view.count == 20);
    for (i = 0; i < 20; ++i) assert(mm_error_stack_get(&p.view, &h, i) == i + 5);
    mm_error_stack_push(&p.view, &h, 24, "second", 99);
    assert(p.pushes == 26 && p.overflows == 5 && *p.view.count == 20);
    if (debug) assert(!strcmp(detailed.entries[19].source, "first") && detailed.entries[19].line == 24);
    assert(!mm_error_stack_contains(&p.view, &h, 4) && mm_error_stack_contains(&p.view, &h, 5));
    assert(mm_error_stack_get(&p.view, &h, -1) == -1 && mm_error_stack_get(&p.view, &h, 20) == -1);
    mm_error_stack_flush(&p.view, &h, 7);
    assert(*p.view.count == 19 && mm_error_stack_get(&p.view, &h, 2) == 8);
    if (debug) assert(detailed.entries[2].line == 8 && !strcmp(detailed.entries[2].source, "first"));
    for (i = 24; i >= 5; --i) {
        if (i == 7) continue;
        assert(mm_error_stack_pop(&p.view, &h, &output) && output == i);
    }
    assert(!*p.view.count && p.writes == 19 && !p.depth);
    mm_error_stack_push(&p.view, &h, 3, NULL, 0); mm_error_stack_push(&p.view, &h, 4, NULL, 0);
    mm_error_stack_push(&p.view, &h, 3, NULL, 0); mm_error_stack_flush(&p.view, &h, 3);
    assert(*p.view.count == 1 && mm_error_stack_get(&p.view, &h, 0) == 4);
    mm_error_stack_clear(&p.view, &h); assert(!*p.view.count);
    mm_error_stack_push(&p.view, NULL, -2147483647L - 1, NULL, 0);
    assert(mm_error_stack_pop(&p.view, NULL, &output) && output == (-2147483647L - 1));
    output = 321; assert(!mm_error_stack_pop(&p.view, NULL, &output) && output == 321);
}
int main(void) { cases(0); cases(1); return 0; }
