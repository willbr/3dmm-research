#include "mm/document_undo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "document undo failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check(!!(x), __LINE__)
typedef struct Token
{
    int id, refs;
} Token;
typedef struct Host
{
    MMDocumentUndo u;
    MMDocumentUndoServices s;
    Token tokens[32];
    int result, ordinal, seen, used, partial, status, value, replay_clear, clear_limit;
    int releases, retains, validations, allocations, frees;
    int released[128], release_done[128], release_count[128];
    char log[8192], fail;
    MMU32 maximum_bytes;
} Host;
static void event(Host *h, char c)
{
    CHECK(h->used < 8191);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static int effect(Host *h, char c)
{
    event(h, c);
    return h->fail == c && ++h->seen == h->ordinal ? h->result : 1;
}
static void *resize(void *v, void *p, MMU32 old_bytes, MMU32 bytes, int priority)
{
    Host *h = (Host *)v;
    int r;
    CHECK(old_bytes == h->u.storage.buffers[0].capacity);
    CHECK(priority == MM_STORAGE_SPEED || priority == MM_STORAGE_NORMAL);
    if (h->status < 0)
        return 0;
    r = effect(h, 'M');
    ++h->allocations;
    if (r < 0)
        h->status = r;
    if (r <= 0 || bytes > h->maximum_bytes)
        return 0;
    return realloc(p, (size_t)bytes);
}
static void free_storage(void *v, void *p, MMU32 bytes)
{
    Host *h = (Host *)v;
    CHECK(p && bytes == h->u.storage.buffers[0].capacity && !h->u.count);
    ++h->frees;
    event(h, 'F');
    free(p);
}
static int status(void *v)
{
    return ((Host *)v)->status;
}
static int replay(void *v, void *p, int redo)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    int r = effect(h, redo ? 'D' : 'U');
    CHECK(t && t->refs > 1);
    if (r > 0 || h->partial)
        h->value = redo ? t->id + 1 : -t->id;
    if (h->replay_clear)
        CHECK(mm_document_undo_clear(&h->u, &h->s) == 1);
    return r;
}
static int retain(void *v, void *p)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    int r = effect(h, '+');
    CHECK(t->refs > 0);
    ++h->retains;
    if (r > 0 || h->partial)
        ++t->refs;
    return r;
}
static int release(void *v, void *p)
{
    Host *h = (Host *)v;
    Token *t = (Token *)p;
    int r = effect(h, '-'), i = h->releases++;
    CHECK(i < 128 && t && t->refs > 1);
    h->released[i] = t->id;
    h->release_done[i] = (int)h->u.done;
    h->release_count[i] = (int)h->u.count;
    if (r > 0 || h->partial)
        --t->refs;
    return r;
}
static int clear_redo(void *v)
{
    Host *h = (Host *)v;
    int r = effect(h, 'C');
    if (r <= 0)
        return r;
    r = mm_document_undo_clear_redo(&h->u, &h->s);
    if (r > 0 && h->clear_limit >= 0)
        r = mm_document_undo_limit(&h->u, h->clear_limit, &h->s);
    return r;
}
static void validate(void *v)
{
    Host *h = (Host *)v;
    ++h->validations;
    event(h, 'V');
}
static void diagnostic(void *v)
{
    (void)v;
    CHECK(0);
}
static void init(Host *h)
{
    MMStorageAllocator a;
    int i;
    memset(h, 0, sizeof(*h));
    h->status = h->ordinal = 1;
    h->maximum_bytes = MM_STORAGE_LIMIT;
    h->clear_limit = -1;
    for (i = 0; i < 32; ++i)
    {
        h->tokens[i].id = i;
        h->tokens[i].refs = 1;
    }
    a.context = h;
    a.resize = resize;
    a.release = free_storage;
    CHECK(mm_document_undo_init(&h->u, &a, 1) == 1);
    h->s.context = h;
    h->s.replay = replay;
    h->s.retain = retain;
    h->s.release = release;
    h->s.clear_redo = clear_redo;
    h->s.status = status;
    h->s.validate = validate;
    h->s.diagnostic = diagnostic;
}
static void reset(Host *h, char tag, int result)
{
    h->used = h->seen = h->releases = h->retains = 0;
    h->log[0] = 0;
    h->fail = tag;
    h->result = result;
    h->ordinal = h->status = 1;
}
static void finish(Host *h)
{
    int i;
    reset(h, 0, 0);
    CHECK(mm_document_undo_dispose(&h->u, &h->s) == 1);
    CHECK(!h->u.ready && !h->u.count && !h->u.done && !h->u.storage.buffers[0].data);
    for (i = 0; i < 32; ++i)
        CHECK(h->tokens[i].refs == 1);
}
static void seed(Host *h, int count)
{
    int i;
    for (i = 0; i < count; ++i)
        CHECK(mm_document_undo_add(&h->u, &h->tokens[i], &h->s) == 1);
}
static void order(void)
{
    Host h;
    int i;
    init(&h);
    CHECK(h.u.maximum == 10 && !h.u.ready && !mm_document_undo_get(&h.u, 0));
    CHECK(!mm_document_undo_replay(&h.u, 0, &h.s) && !mm_document_undo_replay(&h.u, 1, &h.s));
    CHECK(mm_document_undo_limit(&h.u, 0, &h.s) == 1);
    CHECK(mm_document_undo_add(&h.u, &h.tokens[0], &h.s) == 1 && !h.u.ready && !h.used);
    CHECK(mm_document_undo_clear(&h.u, &h.s) == 1 && mm_document_undo_clear_redo(&h.u, &h.s) == 1);
    CHECK(!mm_document_undo_redo_count(&h.u));
    finish(&h);
    init(&h);
    seed(&h, 5);
    for (i = 0; i < 3; ++i)
        CHECK(mm_document_undo_replay(&h.u, 0, &h.s) == 1);
    reset(&h, 0, 0);
    CHECK(mm_document_undo_limit(&h.u, 1, &h.s) == 1);
    CHECK(!strcmp(h.log, "----V") && h.releases == 4 && h.u.count == 1 && h.u.done == 1);
    CHECK(h.released[0] == 4 && h.released[1] == 3 && h.released[2] == 2 && h.released[3] == 0);
    CHECK(h.release_done[0] == 2 && h.release_done[1] == 2 && h.release_done[2] == 2 && h.release_done[3] == 1);
    CHECK(h.release_count[0] == 4 && h.release_count[1] == 3 && h.release_count[2] == 2 && h.release_count[3] == 1);
    CHECK(mm_document_undo_get(&h.u, 0) == &h.tokens[1]);
    finish(&h);
    init(&h);
    seed(&h, 4);
    CHECK(mm_document_undo_replay(&h.u, 0, &h.s) == 1);
    reset(&h, 0, 0);
    CHECK(mm_document_undo_add(&h.u, &h.tokens[4], &h.s) == 1);
    CHECK(!strcmp(h.log, "C-+V") && h.tokens[3].refs == 1 && h.tokens[4].refs == 2);
    reset(&h, 0, 0);
    CHECK(mm_document_undo_clear(&h.u, &h.s) == 1 && h.u.ready && h.u.done == 0);
    CHECK(h.releases == 4 && h.released[0] == 4 && h.released[1] == 2 && h.released[3] == 0);
    CHECK(h.release_done[3] == 4 && h.release_count[3] == 0);
    finish(&h);
}
static void memory_failures(void)
{
    Host h;
    int result;
    for (result = 0; result >= -2; --result)
    {
        init(&h);
        reset(&h, 'M', result);
        h.ordinal = 1;
        /* A native captured exception suppresses exact-allocation retry. Ordinary
         * speculative failure can still succeed at exact size. */
        CHECK(mm_document_undo_prepare(&h.u, &h.s) == (result ? result : 1));
        CHECK(h.u.ready == !result && h.allocations == (result ? 1 : 2));
        finish(&h);
        init(&h);
        h.u.storage.min_grow[0] = 0;
        seed(&h, 3);
        reset(&h, 'M', result);
        CHECK(mm_document_undo_add(&h.u, &h.tokens[3], &h.s) == (result ? result : 1));
        CHECK(h.u.count == 3 && h.u.done == 3);
        CHECK(h.releases == !result && h.tokens[0].refs == (result ? 2 : 1));
        CHECK(h.tokens[3].refs == (result ? 1 : 2));
        finish(&h);
    }
    init(&h);
    h.maximum_bytes = 0;
    CHECK(!mm_document_undo_add(&h.u, &h.tokens[0], &h.s));
    CHECK(!h.u.ready && !h.u.count && !h.u.done && h.tokens[0].refs == 1 && !h.releases);
    finish(&h);
    init(&h);
    h.u.storage.min_grow[0] = 0;
    seed(&h, 3);
    h.maximum_bytes = 3 * (MMU32)sizeof(void *);
    reset(&h, 0, 0);
    CHECK(mm_document_undo_add(&h.u, &h.tokens[3], &h.s) == 1);
    CHECK(!strcmp(h.log, "CM-+V") && mm_document_undo_get(&h.u, 0) == &h.tokens[1]);
    finish(&h);
    init(&h);
    /* An initialized, empty collection can also fail its first append. */
    h.u.ready = 1;
    h.maximum_bytes = 0;
    CHECK(!mm_document_undo_add(&h.u, &h.tokens[0], &h.s));
    CHECK(!h.u.count && !h.u.done && !h.retains);
    finish(&h);
}
static void effect_failures(void)
{
    Host h;
    int result, partial, redo, mode;
    for (redo = 0; redo <= 1; ++redo)
        for (result = 0; result >= -2; --result)
            for (partial = 0; partial <= 1; ++partial)
            {
                init(&h);
                seed(&h, 2);
                if (redo)
                    CHECK(mm_document_undo_replay(&h.u, 0, &h.s) == 1);
                h.value = 99;
                reset(&h, redo ? 'D' : 'U', result);
                h.partial = partial;
                CHECK(mm_document_undo_replay(&h.u, redo, &h.s) == result);
                CHECK(h.u.done == 2 - redo && h.value == (partial ? (redo ? 2 : -1) : 99));
                finish(&h);
            }
    for (result = -1; result >= -2; --result)
        for (partial = 0; partial <= 1; ++partial)
        {
            init(&h);
            seed(&h, 3);
            CHECK(mm_document_undo_replay(&h.u, 0, &h.s) == 1);
            reset(&h, '-', result);
            h.partial = partial;
            CHECK(mm_document_undo_add(&h.u, &h.tokens[3], &h.s) == result);
            CHECK(h.u.count == 2 && h.u.done == 2 && h.tokens[3].refs == 1);
            CHECK(h.tokens[2].refs == 2 - partial);
            h.tokens[2].refs = 1;
            finish(&h);
            init(&h);
            seed(&h, 2);
            reset(&h, '+', result);
            h.partial = partial;
            CHECK(mm_document_undo_add(&h.u, &h.tokens[2], &h.s) == result);
            CHECK(h.u.count == 3 && h.u.done == 2 && h.tokens[2].refs == 1 + partial);
            /* The native append already published the entry before retain. */
            h.tokens[2].refs = 2;
            finish(&h);
            for (mode = 0; mode < 3; ++mode)
            {
                init(&h);
                seed(&h, 3);
                reset(&h, '-', result);
                h.partial = partial;
                CHECK((mode == 0   ? mm_document_undo_clear(&h.u, &h.s)
                       : mode == 1 ? mm_document_undo_limit(&h.u, 1, &h.s)
                                   : mm_document_undo_dispose(&h.u, &h.s)) == result);
                CHECK(h.u.count == 2 && h.u.done == (mode == 1 ? 2 : 3) && h.u.ready);
                CHECK(h.tokens[mode == 1 ? 0 : 2].refs == 2 - partial);
                h.tokens[mode == 1 ? 0 : 2].refs = 1;
                finish(&h);
            }
        }
    init(&h);
    seed(&h, 2);
    reset(&h, 'C', -2);
    CHECK(mm_document_undo_add(&h.u, &h.tokens[2], &h.s) == -2 && h.u.count == 2 && h.u.done == 2);
    finish(&h);
}
static void reentrancy(void)
{
    Host h;
    init(&h);
    seed(&h, 3);
    h.replay_clear = 1;
    CHECK(mm_document_undo_replay(&h.u, 0, &h.s) == 1);
    CHECK(!h.u.count && h.u.done == -1);
    /* Replay advances the live cursor after its callback; no new clamp is added. */
    h.u.done = 0;
    finish(&h);
    init(&h);
    seed(&h, 4);
    h.clear_limit = 2;
    reset(&h, 0, 0);
    CHECK(mm_document_undo_add(&h.u, &h.tokens[4], &h.s) == 1);
    CHECK(h.u.maximum == 2 && h.u.count == 2 && h.u.done == 2 && h.releases == 3);
    CHECK(mm_document_undo_get(&h.u, 0) == &h.tokens[3] && mm_document_undo_get(&h.u, 1) == &h.tokens[4]);
    finish(&h);
}
static unsigned next(unsigned *state)
{
    *state = (*state * 1664525UL + 1013904223UL) & 0xffffffffUL;
    return *state;
}
static void model(void)
{
    Host h;
    int list[32], count = 0, done_count = 0, maximum_count = 10, ready = 0;
    int op, id, limit, cut, i, j, refs, expected, value = 0;
    unsigned rng = 7741, step;
    init(&h);
    for (step = 0; step < 3000; ++step)
    {
        reset(&h, 0, 0);
        op = (int)(next(&rng) % 7);
        id = (int)(next(&rng) % 32);
        if (op == 0)
        {
            CHECK(mm_document_undo_add(&h.u, &h.tokens[id], &h.s) == 1);
            if (maximum_count)
            {
                ready = 1;
                count = done_count;
                if (count >= maximum_count)
                {
                    for (i = 1; i < count; ++i)
                        list[i - 1] = list[i];
                    --count;
                }
                list[count++] = id;
                done_count = count;
            }
        }
        else if (op <= 2)
        {
            expected = op == 1 ? done_count > 0 : done_count < count;
            CHECK(mm_document_undo_replay(&h.u, op == 2, &h.s) == expected);
            if (expected)
            {
                if (op == 1)
                    value = -list[--done_count];
                else
                    value = list[done_count++] + 1;
            }
        }
        else if (op == 3)
        {
            CHECK(mm_document_undo_clear(&h.u, &h.s) == 1);
            count = done_count = 0;
        }
        else if (op == 4)
        {
            CHECK(mm_document_undo_clear_redo(&h.u, &h.s) == 1);
            count = done_count;
        }
        else if (op == 5)
        {
            limit = (int)(next(&rng) % 12);
            CHECK(mm_document_undo_limit(&h.u, limit, &h.s) == 1);
            maximum_count = limit;
            cut = done_count > limit ? done_count : limit;
            if (count > cut)
                count = cut;
            while (count > limit)
            {
                for (i = 1; i < count; ++i)
                    list[i - 1] = list[i];
                --count;
                --done_count;
            }
        }
        else
        {
            CHECK(mm_document_undo_prepare(&h.u, &h.s) == 1);
            ready = 1;
        }
        CHECK(h.u.count == (MMU32)count && h.u.done == done_count && h.u.maximum == maximum_count);
        CHECK(h.u.ready == ready && mm_document_undo_redo_count(&h.u) == count - done_count && h.value == value);
        for (i = 0; i < count; ++i)
            CHECK(mm_document_undo_get(&h.u, (MMU32)i) == &h.tokens[list[i]]);
        CHECK(!mm_document_undo_get(&h.u, (MMU32)count));
        for (i = 0; i < 32; ++i)
        {
            refs = 1;
            for (j = 0; j < count; ++j)
                refs += list[j] == i;
            CHECK(h.tokens[i].refs == refs);
        }
    }
    finish(&h);
}
int main(void)
{
    order();
    memory_failures();
    effect_failures();
    reentrancy();
    model();
    puts("document undo tests passed");
    return 0;
}
