#include "mm/text_undo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "text undo failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check(!!(x), __LINE__)
typedef struct Owner
{
    int kind, refs, internal;
    MMTextUndoControl control;
    MMTextUndoRecord record;
    char text[128];
    MMI32 length;
} Owner;
typedef struct Host
{
    Owner target, records[8], documents[16], previous;
    MMTextUndoRecordServices s;
    MMTextUndoTransactionServices t;
    int records_used, documents_used, partial, ordinal, seen, result, used;
    int maximum, done, accepted, diagnostics[2], copies, selections, added, clears;
    char log[2048], fail;
    MMI32 selection_first[8], selection_end[8];
    MMI32 copy_from[8], copy_count[8], copy_at[8], copy_deleted[8];
    Owner *stack;
} Host;
static void event(Host *h, char c)
{
    CHECK(h->used < 2047);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static int effect(Host *h, char c)
{
    event(h, c);
    return h->fail == c && ++h->seen == h->ordinal ? h->result : 1;
}
static void init_document(Owner *p, const char *text)
{
    memset(p, 0, sizeof(*p));
    p->refs = 1;
    p->length = (MMI32)strlen(text);
    CHECK(p->length < 128);
    memcpy(p->text, text, (size_t)p->length + 1);
}
static int allocate(void *v, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'A');
    if (r > 0 || h->partial)
    {
        Owner *p;
        CHECK(h->records_used < 8);
        p = &h->records[h->records_used++];
        memset(p, 0, sizeof(*p));
        p->kind = 1;
        p->refs = 1;
        *out = p;
    }
    return r;
}
static MMTextUndoRecord *record(void *v, void *p)
{
    (void)v;
    CHECK(((Owner *)p)->kind == 1 && ((Owner *)p)->refs);
    return &((Owner *)p)->record;
}
static int document(void *v, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'D');
    if (r > 0 || h->partial)
    {
        CHECK(h->documents_used < 16);
        *out = &h->documents[h->documents_used++];
        init_document((Owner *)*out, "\r");
    }
    return r;
}
static void release(void *v, void *owner)
{
    Host *h = (Host *)v;
    Owner *p = (Owner *)owner;
    CHECK(p->refs > 0);
    event(h, p->kind ? 'R' : 'd');
    if (--p->refs == 0 && p->kind)
        mm_text_undo_record_dispose(&p->record, &h->s);
}
static MMI32 characters(void *v, void *p)
{
    (void)v;
    CHECK(((Owner *)p)->refs > 0 && !((Owner *)p)->kind);
    return ((Owner *)p)->length;
}
static int accept(void *v, void *p)
{
    (void)p;
    return ((Host *)v)->accepted;
}
static int internal(void *v, void *p)
{
    Host *h = (Host *)v;
    int r = effect(h, 'I');
    if (r > 0 || h->partial)
        ((Owner *)p)->internal = 1;
    return r;
}
static int suspend(void *v, void *p)
{
    Host *h = (Host *)v;
    int r = effect(h, 'S');
    if (r > 0 || h->partial)
        mm_text_undo_suspend(&((Owner *)p)->control);
    return r;
}
static int resume(void *v, void *p)
{
    Host *h = (Host *)v;
    int r = effect(h, 'r');
    if (r > 0 || h->partial)
        mm_text_undo_resume(&((Owner *)p)->control);
    return r;
}
static void edit(Owner *p, const char *text, MMI32 count, MMI32 at, MMI32 deleted)
{
    CHECK(at >= 0 && deleted >= 0 && count >= 0 && at + deleted <= p->length);
    CHECK(p->length + count - deleted < 128);
    memmove(p->text + at + count, p->text + at + deleted, (size_t)(p->length - at - deleted + 1));
    if (count)
        memcpy(p->text + at, text, (size_t)count);
    p->length += count - deleted;
}
static int copy(void *v, void *dest, void *source, MMI32 from, MMI32 count, MMI32 at, MMI32 deleted)
{
    Host *h = (Host *)v;
    Owner *src = (Owner *)source;
    int r = effect(h, 'C'), i = h->copies++;
    CHECK(i < 8 && from >= 0 && from + count <= src->length);
    h->copy_from[i] = from;
    h->copy_count[i] = count;
    h->copy_at[i] = at;
    h->copy_deleted[i] = deleted;
    if (r > 0 || h->partial)
        edit((Owner *)dest, src->text + from, count, at, deleted);
    return r;
}
static int remove_text(void *v, void *p, MMI32 at, MMI32 count)
{
    Host *h = (Host *)v;
    int r = effect(h, 'X');
    if (r > 0 || h->partial)
        edit((Owner *)p, 0, 0, at, count);
    return r;
}
static int selection(void *v, void *p, int kind, MMI32 first, MMI32 end)
{
    Host *h = (Host *)v;
    int i;
    CHECK(p == &h->target && kind >= 0 && kind < 4);
    if (kind >= MM_TEXT_UNDO_SELECT_QUIET)
    {
        i = h->selections++;
        CHECK(i < 8);
        h->selection_first[i] = first;
        h->selection_end[i] = end;
    }
    return effect(h, "HVQE"[kind]);
}
static void check_saved(void *v, void *p)
{
    (void)v;
    CHECK(p && !((Owner *)p)->kind && ((Owner *)p)->refs > 0);
}
static void diagnostic(void *v, int kind)
{
    Host *h = (Host *)v;
    CHECK(kind >= 0 && kind < 2);
    ++h->diagnostics[kind];
    event(h, '!');
}
static void check_range(void *v, void *p, MMI32 at)
{
    CHECK(at >= 0 && at < ((Owner *)p)->length);
    event((Host *)v, 'b');
}
static MMI32 maximum(void *v)
{
    return ((Host *)v)->maximum;
}
static MMI32 done(void *v)
{
    return ((Host *)v)->done;
}
static int resume_target(void *v)
{
    return resume(v, &((Host *)v)->target);
}
static int create_record(void *v, MMI32 combine, MMI32 first, MMI32 end, MMI32 inserted, void **out)
{
    Host *h = (Host *)v;
    event(h, 'T');
    return mm_text_undo_record_create(out, combine, &h->target, first, end, inserted, &h->s);
}
static void *previous(void *v, MMI32 index)
{
    Host *h = (Host *)v;
    CHECK(index == h->done - 1);
    return &h->previous;
}
static int is_record(void *v, void *p)
{
    int r = effect((Host *)v, 'K');
    return r > 0 ? ((Owner *)p)->kind : r;
}
static int combine_records(void *v, void *a, void *b)
{
    Host *h = (Host *)v;
    int r = effect(h, 'B');
    return r > 0 ? mm_text_undo_record_combine(&((Owner *)a)->record, &((Owner *)b)->record, &h->s) : r;
}
static int clear_redo(void *v)
{
    Host *h = (Host *)v;
    ++h->clears;
    return effect(h, 'Z');
}
static int add(void *v, void *p)
{
    Host *h = (Host *)v;
    int r = effect(h, 'L');
    ++h->added;
    if (r > 0)
    {
        ++((Owner *)p)->refs;
        h->stack = (Owner *)p;
    }
    return r;
}
static void check_pending(void *v, void *p)
{
    (void)v;
    CHECK(((Owner *)p)->kind && ((Owner *)p)->refs > 0);
}
static void init(Host *h)
{
    memset(h, 0, sizeof(*h));
    init_document(&h->target, "abXYZfg\r");
    h->maximum = 10;
    h->accepted = 1;
    h->ordinal = 1;
    h->s.context = h;
    h->s.allocate = allocate;
    h->s.record = record;
    h->s.document = document;
    h->s.release = release;
    h->s.characters = characters;
    h->s.accept = accept;
    h->s.internal = internal;
    h->s.suspend = suspend;
    h->s.resume = resume;
    h->s.copy = copy;
    h->s.remove = remove_text;
    h->s.selection = selection;
    h->s.check_saved = check_saved;
    h->s.diagnostic = diagnostic;
    h->s.check_range = check_range;
    h->t.context = h;
    h->t.control = &h->target.control;
    h->t.maximum = maximum;
    h->t.done = done;
    h->t.resume = resume_target;
    h->t.create = create_record;
    h->t.release = release;
    h->t.previous = previous;
    h->t.is_record = is_record;
    h->t.combine = combine_records;
    h->t.clear_redo = clear_redo;
    h->t.add = add;
    h->t.check_pending = check_pending;
    h->t.diagnostic = diagnostic;
}
static void fault(Host *h, char tag, int result, int ordinal)
{
    h->used = h->seen = 0;
    h->log[0] = 0;
    h->fail = tag;
    h->result = result;
    h->ordinal = ordinal;
}
static Owner *saved(Host *h, const char *text)
{
    Owner *p;
    CHECK(h->documents_used < 16);
    p = &h->documents[h->documents_used++];
    init_document(p, text);
    p->control.suspended = p->internal = 1;
    return p;
}
static void no_private_owners(Host *h)
{
    int i;
    for (i = 0; i < h->records_used; ++i)
        CHECK(!h->records[i].refs);
    for (i = 0; i < h->documents_used; ++i)
        CHECK(!h->documents[i].refs);
}
static int same_record(const MMTextUndoRecord *a, const MMTextUndoRecord *b)
{
    return a->combine == b->combine && a->at == b->at && a->inserted == b->inserted && a->saved == b->saved;
}
static void counters(void)
{
    MMTextUndoControl c;
    memset(&c, 0, sizeof(c));
    mm_text_undo_suspend(&c);
    mm_text_undo_suspend(&c);
    mm_text_undo_bump(&c);
    CHECK(c.suspended == 2 && !c.combine);
    mm_text_undo_resume(&c);
    mm_text_undo_resume(&c);
    mm_text_undo_bump(&c);
    CHECK(!c.suspended && c.combine == 1);
    mm_text_undo_resume(&c);
    mm_text_undo_bump(&c);
    CHECK(c.suspended == -1 && c.combine == 2);
    c.combine = 2147483647L;
    mm_text_undo_bump(&c);
    CHECK(c.combine == (-2147483647L - 1));
}
static void construction(void)
{
    Host h;
    void *out;
    Owner *p, *d;
    const char *tags = "ADISC";
    int i, result, partial;
    init(&h);
    CHECK(mm_text_undo_record_create(&out, 7, &h.target, 5, 2, 1, &h.s) == 1);
    p = (Owner *)out;
    d = (Owner *)p->record.saved;
    CHECK(p->record.combine == 7 && p->record.at == 2 && p->record.inserted == 1);
    CHECK(d->internal && d->control.suspended == 1 && !strcmp(d->text, "XYZ\r"));
    CHECK(!strcmp(h.log, "bADISC") && h.copy_from[0] == 2 && h.copy_count[0] == 3);
    release(&h, p);
    no_private_owners(&h);
    init(&h);
    CHECK(mm_text_undo_record_create(&out, 7, &h.target, 2, 2, 3, &h.s) == 1);
    CHECK(!((Owner *)out)->record.saved && !strcmp(h.log, "bA"));
    release(&h, out);
    for (i = 0; tags[i]; ++i)
        for (result = 0; result >= -2; --result)
            for (partial = 0; partial <= 1; ++partial)
            {
                init(&h);
                fault(&h, tags[i], result, 1);
                h.partial = partial;
                out = &h;
                CHECK(mm_text_undo_record_create(&out, 7, &h.target, 5, 2, 1, &h.s) == result);
                CHECK(!out && !h.target.control.suspended && !strcmp(h.target.text, "abXYZfg\r"));
                no_private_owners(&h);
            }
}
static void replay(void)
{
    Host h;
    MMTextUndoRecord r, old;
    Owner *before, *after;
    const char *tags = "DISHrQVE";
    int result, i, stage, partial;
    init(&h);
    memset(&r, 0, sizeof(r));
    r.at = 2;
    r.inserted = 3;
    r.saved = before = saved(&h, "bc\r");
    CHECK(mm_text_undo_record_apply(&r, &h.target, &h.s) == 1);
    after = (Owner *)r.saved;
    CHECK(!strcmp(h.target.text, "abbcfg\r") && !strcmp(after->text, "XYZ\r"));
    CHECK(r.inserted == 2 && !before->refs && !strcmp(h.log, "DISCSHCrQVEd"));
    CHECK(h.selection_first[0] == 2 && h.selection_end[0] == 4);
    CHECK(h.selection_first[1] == 4 && h.selection_end[1] == 4);
    CHECK(mm_text_undo_record_apply(&r, &h.target, &h.s) == 1);
    CHECK(!strcmp(h.target.text, "abXYZfg\r") && r.inserted == 3 && !after->refs);
    mm_text_undo_record_dispose(&r, &h.s);
    no_private_owners(&h);
    init(&h);
    memset(&r, 0, sizeof(r));
    r.at = 2;
    r.inserted = 3;
    CHECK(mm_text_undo_record_apply(&r, &h.target, &h.s) == 1);
    CHECK(r.saved && !r.inserted && !strcmp(h.target.text, "abfg\r"));
    fault(&h, 0, 0, 1);
    CHECK(mm_text_undo_record_apply(&r, &h.target, &h.s) == 1);
    CHECK(!r.saved && r.inserted == 3 && !strcmp(h.target.text, "abXYZfg\r"));
    CHECK(!strcmp(h.log, "SHCrQVEd"));
    for (i = 0; i < 5; ++i)
    {
        init(&h);
        memset(&r, 0, sizeof(r));
        r.at = i == 0 ? -1 : i == 1 ? 8 : 2;
        r.inserted = i == 2 ? -1 : i == 3 ? 7 : 1;
        h.accepted = i != 4;
        CHECK(!mm_text_undo_record_apply(&r, &h.target, &h.s));
        CHECK(!strcmp(h.log, "!") && h.diagnostics[MM_TEXT_UNDO_WRONG_DOCUMENT] == 1);
    }
    for (i = 0; tags[i]; ++i)
        for (result = 0; result >= -2; --result)
        {
            /* Native void hooks cannot return ordinary false. */
            if (!result && tags[i] != 'D')
                continue;
            init(&h);
            memset(&r, 0, sizeof(r));
            r.at = 2;
            r.inserted = 3;
            r.saved = before = saved(&h, "bc\r");
            old = r;
            fault(&h, tags[i], result, 1);
            CHECK(mm_text_undo_record_apply(&r, &h.target, &h.s) == result);
            CHECK(same_record(&r, &old) && before->refs == 1);
            CHECK(h.documents_used == (tags[i] == 'D' ? 1 : 2));
            if (h.documents_used == 2)
                CHECK(!h.documents[1].refs);
            CHECK(h.target.control.suspended == (tags[i] == 'H' || tags[i] == 'r'));
        }
    for (stage = 1; stage <= 2; ++stage)
        for (result = 0; result >= -2; --result)
            for (partial = 0; partial <= 1; ++partial)
            {
                init(&h);
                memset(&r, 0, sizeof(r));
                r.at = 2;
                r.inserted = 3;
                r.saved = before = saved(&h, "bc\r");
                old = r;
                fault(&h, 'C', result, stage);
                h.partial = partial;
                CHECK(mm_text_undo_record_apply(&r, &h.target, &h.s) == result);
                CHECK(same_record(&r, &old) && before->refs == 1 && !h.documents[1].refs);
                CHECK(h.target.control.suspended == (result ? stage - 1 : stage - 2));
                if (stage == 2 && partial)
                    CHECK(!strcmp(h.target.text, "abbcfg\r"));
                else
                    CHECK(!strcmp(h.target.text, "abXYZfg\r"));
                if (!result)
                    CHECK(strstr(h.log, "dr"));
            }
    for (result = 0; result >= -2; --result)
    {
        init(&h);
        memset(&r, 0, sizeof(r));
        r.at = 2;
        r.inserted = 3;
        fault(&h, 'X', result, 1);
        CHECK(mm_text_undo_record_apply(&r, &h.target, &h.s) == result);
        CHECK(!r.saved && r.inserted == 3 && !h.documents[0].refs);
        CHECK(h.target.control.suspended == (result != 0));
    }
}
static void combination(void)
{
    Host h;
    MMTextUndoRecord a, b, original;
    Owner *first;
    int result, partial;
    init(&h);
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.combine = b.combine = 7;
    a.at = 2;
    a.inserted = 3;
    b.at = 5;
    b.inserted = 2;
    CHECK(mm_text_undo_record_combine(&a, &b, &h.s) == 1 && a.inserted == 5 && !h.used);
    b.combine = 8;
    CHECK(!mm_text_undo_record_combine(&a, &b, &h.s) && a.inserted == 5 && !h.used);
    b.combine = 7;
    CHECK(!mm_text_undo_record_combine(&a, &b, &h.s) && !h.used);
    a.inserted = b.inserted = 0;
    a.saved = first = saved(&h, "ab\r");
    b.saved = saved(&h, "cd\r");
    b.at = a.at;
    CHECK(mm_text_undo_record_combine(&a, &b, &h.s) == 1 && !strcmp(first->text, "abcd\r"));
    CHECK(h.copy_at[0] == 2 && h.copy_count[0] == 2 && a.at == 2);
    for (result = 0; result >= -2; --result)
        for (partial = 0; partial <= 1; ++partial)
        {
            init(&h);
            a.at = 4;
            b.at = 2;
            a.saved = first = saved(&h, "cd\r");
            b.saved = saved(&h, "ab\r");
            original = b;
            fault(&h, 'C', result, 1);
            h.partial = partial;
            CHECK(mm_text_undo_record_combine(&a, &b, &h.s) == result);
            CHECK(a.at == 4 && same_record(&b, &original));
            CHECK(!strcmp(first->text, partial ? "abcd\r" : "cd\r"));
        }
    init(&h);
    a.saved = first = saved(&h, "cd\r");
    b.saved = saved(&h, "ab\r");
    CHECK(mm_text_undo_record_combine(&a, &b, &h.s) == 1 && a.at == 2 && !strcmp(first->text, "abcd\r"));
    b.at = 7;
    CHECK(!mm_text_undo_record_combine(&a, &b, &h.s));
}
static void transactions(void)
{
    Host h;
    Owner *p;
    int result, mode;
    init(&h);
    h.target.control.combine = 7;
    CHECK(mm_text_undo_setup(2, 4, 1, &h.t) == 1);
    p = (Owner *)h.target.control.pending;
    CHECK(p && p->record.combine == 7 && h.target.control.suspended == 1);
    CHECK(mm_text_undo_setup(1, 3, 2, &h.t) == 1 && h.records_used == 1);
    CHECK(mm_text_undo_cancel(&h.t) == 1 && h.target.control.pending == p && h.target.control.suspended == 1);
    CHECK(mm_text_undo_cancel(&h.t) == 1 && !h.target.control.pending && !h.target.control.suspended);
    no_private_owners(&h);
    for (mode = 0; mode < 2; ++mode)
    {
        init(&h);
        h.maximum = mode ? 10 : 0;
        CHECK(mm_text_undo_setup(2, mode ? 2 : 4, 0, &h.t) == 1);
        CHECK(!h.target.control.pending && h.target.control.suspended == 1 && !h.records_used);
        CHECK(mm_text_undo_commit(&h.t) == 1 && !h.target.control.suspended);
    }
    for (result = 0; result >= -2; --result)
    {
        init(&h);
        fault(&h, 'C', result, 1);
        CHECK(mm_text_undo_setup(2, 4, 1, &h.t) == result);
        CHECK(!h.target.control.pending && h.target.control.suspended == (result != 0));
        no_private_owners(&h);
        init(&h);
        CHECK(mm_text_undo_setup(2, 4, 1, &h.t) == 1);
        p = (Owner *)h.target.control.pending;
        fault(&h, 'L', result, 1);
        CHECK(mm_text_undo_commit(&h.t) == (result ? result : 1));
        CHECK(!h.target.control.suspended && h.target.control.pending == (result ? p : 0));
        CHECK(p->refs == (result != 0));
    }
    for (mode = 0; mode < 3; ++mode)
    {
        init(&h);
        CHECK(mm_text_undo_setup(2, 2, 1, &h.t) == 1);
        p = (Owner *)h.target.control.pending;
        h.done = 1;
        h.previous.kind = mode != 0;
        h.previous.refs = 1;
        h.previous.record.at = mode == 2 ? 3 : 1;
        h.previous.record.inserted = 1;
        fault(&h, 0, 0, 1);
        CHECK(mm_text_undo_commit(&h.t) == 1 && !h.target.control.pending);
        CHECK(h.clears == (mode == 1) && h.added == (mode != 1));
        CHECK(p->refs == (mode != 1));
        if (mode == 1)
            CHECK(!strcmp(h.log, "rKBZR") && h.previous.record.inserted == 2);
    }
    for (mode = 0; mode < 4; ++mode)
        for (result = -1; result >= -2; --result)
        {
            init(&h);
            CHECK(mm_text_undo_setup(2, 2, 1, &h.t) == 1);
            p = (Owner *)h.target.control.pending;
            h.done = 1;
            h.previous.kind = h.previous.refs = 1;
            h.previous.record.at = h.previous.record.inserted = 1;
            fault(&h, "rKBZ"[mode], result, 1);
            CHECK(mm_text_undo_commit(&h.t) == result);
            CHECK(h.target.control.pending == p && p->refs == 1);
            CHECK(h.target.control.suspended == (mode == 0));
        }
    init(&h);
    CHECK(mm_text_undo_setup(2, 2, 1, &h.t) == 1);
    p = (Owner *)h.target.control.pending;
    fault(&h, 'r', -2, 1);
    CHECK(mm_text_undo_cancel(&h.t) == -2 && h.target.control.pending == p && p->refs == 1);
}
int main(void)
{
    counters();
    construction();
    replay();
    combination();
    transactions();
    puts("text undo tests passed");
    return 0;
}
