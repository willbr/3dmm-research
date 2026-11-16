#include "mm/text_copy.h"
#include "mm/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "text copy failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check(!!(x), __LINE__)
typedef struct Trace
{
    char log[4096], fail;
    int used, ordinal, seen, result;
} Trace;
typedef struct Argument
{
    unsigned char bytes[522];
    MMI32 length;
    MMU32 fixed;
    int free;
} Argument;
typedef struct Doc
{
    Trace *trace;
    MMTextPropertyEntry entries[128];
    MMI32 count, characters, slots;
    Argument arguments[32];
    void *group;
    MMTextReplaceRichServices rich;
    MMTextFormatConfig config;
    MMTextCharacter input;
    MMI32 undo_at, undo_end, undo_inserted, notify_at, notify_inserted, notify_deleted;
    MMU32 flags;
    MMTextReplaceRequest replacement;
    int warnings[2], diagnostics[4], undos, commits, cancels, notifications, bumps, locks, tail;
    int alloc_partial, dispatched, draw, dispatch_index;
    MMI32 published_at, published_bytes;
    unsigned char allocation[522];
} Doc;
typedef struct Pair
{
    Doc source, target;
    Trace trace;
    MMTextCopyServices copy;
    MMTextObjectServices object;
} Pair;
static void event(Doc *d, char c)
{
    Trace *t = d->trace;
    CHECK(t->used < 4095);
    t->log[t->used++] = c;
    t->log[t->used] = 0;
}
static int effect(Doc *d, char c)
{
    Trace *t = d->trace;
    event(d, c);
    return t->fail == c && ++t->seen == t->ordinal ? t->result : 1;
}
static void *owner(void *v, int kind)
{
    CHECK(kind == MM_TEXT_OWNER_ARGUMENTS);
    return ((Doc *)v)->group;
}
static void publish(void *v, int kind, void *group)
{
    Doc *d = (Doc *)v;
    CHECK(kind == MM_TEXT_OWNER_ARGUMENTS && group == d && !d->group);
    d->group = group;
    event(d, 'p');
}
static int create(void *v, MMI32 bytes, void **out)
{
    Doc *d = (Doc *)v;
    int r = effect(d, 'G');
    CHECK(bytes > 0);
    if (r > 0)
        *out = d;
    return r;
}
static MMI32 argument_count(void *v, void *group)
{
    CHECK(v == group);
    return ((Doc *)v)->slots;
}
static int free_slot(void *v, void *group, MMI32 i)
{
    Doc *d = (Doc *)v;
    CHECK(group == v && i >= 0 && i < d->slots);
    return d->arguments[i].free;
}
static const void *data(void *v, void *group, MMI32 i, MMI32 *bytes)
{
    Doc *d = (Doc *)v;
    CHECK(!free_slot(v, group, i));
    *bytes = d->arguments[i].length;
    return d->arguments[i].bytes;
}
static MMU32 fixed(void *v, void *group, MMI32 i)
{
    CHECK(!free_slot(v, group, i));
    return ((Doc *)v)->arguments[i].fixed;
}
static void put_fixed(void *v, void *group, MMI32 i, MMU32 value)
{
    CHECK(!free_slot(v, group, i));
    ((Doc *)v)->arguments[i].fixed = value;
    event((Doc *)v, 'r');
}
static int add(void *v, void *group, const void *bytes, MMI32 length, MMU32 value, MMI32 *out)
{
    Doc *d = (Doc *)v;
    MMI32 i;
    int r = effect(d, 'A');
    CHECK(group == v && length > 0 && length <= 522);
    if (r <= 0)
        return r;
    for (i = 0; i < d->slots && !d->arguments[i].free; ++i)
        ;
    CHECK(i < 32);
    if (i == d->slots)
        ++d->slots;
    memcpy(d->arguments[i].bytes, bytes, (size_t)length);
    d->arguments[i].length = length;
    d->arguments[i].fixed = value;
    d->arguments[i].free = 0;
    *out = i;
    return 1;
}
static void remove_argument(void *v, void *group, MMI32 i)
{
    Doc *d = (Doc *)v;
    CHECK(!free_slot(v, group, i));
    event(d, (d->arguments[i].fixed >> 24) == 2 ? 'f' : 'o');
    d->arguments[i].free = 1;
}
static void diagnostic(void *v, int kind)
{
    Doc *d = (Doc *)v;
    CHECK(kind >= 0 && kind < 4);
    ++d->diagnostics[kind];
    event(d, '!');
}
static void validate_input(void *v, unsigned kind, const void *bytes, MMI32 n)
{
    CHECK(kind < 256 && bytes && n > 0);
    event((Doc *)v, 'i');
}
static MMI32 count(void *v)
{
    return ((Doc *)v)->count;
}
static void get(void *v, MMI32 i, MMTextPropertyEntry *out)
{
    Doc *d = (Doc *)v;
    CHECK(i >= 0 && i < d->count);
    *out = d->entries[i];
}
static void put(void *v, MMI32 i, const MMTextPropertyEntry *entry)
{
    Doc *d = (Doc *)v;
    CHECK(i >= 0 && i < d->count);
    d->entries[i] = *entry;
    event(d, 'w');
}
static int insert(void *v, MMI32 i, const MMTextPropertyEntry *entry)
{
    Doc *d = (Doc *)v;
    int r = effect(d, 'X');
    CHECK(i >= 0 && i <= d->count && d->count < 128);
    if (r > 0)
    {
        memmove(d->entries + i + 1, d->entries + i, (size_t)(d->count - i) * sizeof(*entry));
        d->entries[i] = *entry;
        ++d->count;
    }
    return r;
}
static void remove_entry(void *v, MMI32 i)
{
    Doc *d = (Doc *)v;
    CHECK(i >= 0 && i < d->count);
    --d->count;
    memmove(d->entries + i, d->entries + i + 1, (size_t)(d->count - i) * sizeof(d->entries[0]));
}
static void clear_cache(void *v)
{
    event((Doc *)v, 'k');
}
static int paragraph_end(void *v, MMI32 cp, MMI32 *end)
{
    CHECK(cp >= 0);
    *end = ((Doc *)v)->characters;
    return 1;
}
static int paragraph_query(void *v, int kind, MMI32 cp, MMI32 *out)
{
    (void)v;
    *out = kind == MM_TEXT_PARA_IS_START ? 1 : cp;
    return 1;
}
static int font_name(void *v, MMI32 font, MMMovieTitle *out)
{
    Doc *d = (Doc *)v;
    static const unsigned short wide[] = {'F', 'o', 'n', 't'};
    int r = effect(d, 'F');
    CHECK(font == 17);
    if (r > 0)
        CHECK(mm_movie_title_set(out, out->unit == 1 ? (const void *)"Font" : wide, 4));
    return r;
}
static MMI32 characters(void *v)
{
    return ((Doc *)v)->characters;
}
static int undo(void *v, MMI32 at, MMI32 end, MMI32 inserted)
{
    Doc *d = (Doc *)v;
    ++d->undos;
    d->undo_at = at;
    d->undo_end = end;
    d->undo_inserted = inserted;
    return effect(d, 'U');
}
static int reserve(void *v, MMI32 n)
{
    CHECK(n > 0 && n <= 9);
    return effect((Doc *)v, 'R');
}
static int commit(void *v)
{
    ++((Doc *)v)->commits;
    return effect((Doc *)v, 'M');
}
static int cancel(void *v)
{
    ++((Doc *)v)->cancels;
    return effect((Doc *)v, 'C');
}
static void bump(void *v)
{
    ++((Doc *)v)->bumps;
    event((Doc *)v, 'b');
}
static void validate(void *v)
{
    event((Doc *)v, 'v');
}
static void read_input(void *v, int paragraph, MMTextFormatConfig *config, void *value, void *old, int *has_old)
{
    Doc *d = (Doc *)v;
    (void)old;
    CHECK(!paragraph && d->undos);
    *config = d->config;
    *(MMTextCharacter *)value = d->input;
    *has_old = 0;
    event(d, 'T');
}
static int replace(void *v, const MMTextReplaceRequest *q)
{
    Doc *d = (Doc *)v;
    int r = effect(d, 'D');
    d->replacement = *q;
    if (q->kind == MM_TEXT_REPLACE_BYTES)
        CHECK(q->inserted == 1 && *(const unsigned char *)q->data == 1);
    if (r > 0)
    {
        d->characters += q->inserted - q->deleted;
        r = mm_text_properties_adjust(q->at, q->inserted, q->deleted, d->characters, &d->rich.edit.format.properties);
    }
    return r;
}
static int notify(void *v, MMI32 at, MMI32 ins, MMI32 del, MMU32 flags)
{
    Doc *d = (Doc *)v;
    ++d->notifications;
    d->notify_at = at;
    d->notify_inserted = ins;
    d->notify_deleted = del;
    d->flags = flags;
    return effect(d, 'N');
}
static void too_much(void *v)
{
    event((Doc *)v, 'Q');
}
static MMI32 source_characters(void *v)
{
    return ((Pair *)v)->source.characters;
}
static MMI32 source_bytes(void *v, MMI32 index)
{
    Doc *d = &((Pair *)v)->source;
    CHECK(index >= 0 && index < d->slots && !d->arguments[index].free);
    event(d, 's');
    return d->arguments[index].length;
}
static const void *lock_source(void *v, MMI32 index)
{
    Doc *d = &((Pair *)v)->source;
    CHECK(index >= 0 && index < d->slots && !d->locks);
    ++d->locks;
    event(d, 'l');
    return d->arguments[index].bytes;
}
static void unlock_source(void *v)
{
    Doc *d = &((Pair *)v)->source;
    CHECK(d->locks == 1);
    --d->locks;
    event(d, 'u');
}
static void *source_stream(void *v)
{
    return &((Pair *)v)->source;
}
static int check_tail(void *v, MMI32 cp, MMI32 inserted, MMI32 deleted)
{
    Pair *p = (Pair *)v;
    CHECK(cp == p->source.characters - 1 && inserted > 0 && deleted > 0);
    ++p->target.tail;
    return effect(&p->target, 't');
}
static void warning(void *v, int kind)
{
    Pair *p = (Pair *)v;
    CHECK(kind >= 0 && kind < 2);
    ++p->target.warnings[kind];
    event(&p->target, 'W');
}
static MMI32 object_bytes(void *v, MMI32 i)
{
    Doc *d = (Doc *)v;
    CHECK(i >= 0 && i < d->slots && !d->arguments[i].free);
    event(d, 's');
    return d->arguments[i].length;
}
static int allocate(void *v, MMI32 bytes, void **out)
{
    Doc *d = (Doc *)v;
    int r = effect(d, 'H');
    CHECK(bytes > 0 && bytes <= 522);
    *out = r > 0 || d->alloc_partial ? d->allocation : 0;
    return r;
}
static void publish_at(void *v, MMI32 cp)
{
    ((Doc *)v)->published_at = cp;
    event((Doc *)v, 'a');
}
static void publish_bytes(void *v, MMI32 bytes)
{
    ((Doc *)v)->published_bytes = bytes;
    event((Doc *)v, 'z');
}
static int dispatch(void *v, int draw, MMI32 index, unsigned kind)
{
    Doc *d = (Doc *)v;
    CHECK(kind == 192 && index >= 0);
    ++d->dispatched;
    d->draw = draw;
    d->dispatch_index = (int)index;
    return effect(d, 'J');
}
static void init_doc(Doc *d, Trace *trace, unsigned unit)
{
    MMTextPropertyServices *p;
    MMTextArgumentServices *a;
    d->trace = trace;
    d->characters = 9;
    d->config.text.unit = unit;
    d->config.text.system = unit == 1 ? MM_TEXT_WINDOWS : MM_TEXT_UTF16_LE;
    d->config.font = 11;
    d->config.height = 12;
    d->config.black = 0x01000000UL;
    d->config.clear = 0xffffffffUL;
    d->config.tab = 18;
    d->config.line = d->config.after = 256;
    d->input.font = 17;
    d->input.height = 19;
    d->input.foreground = d->config.black;
    d->input.background = d->config.clear;
    d->rich.context = d;
    d->rich.characters = characters;
    d->rich.replace_virtual = replace;
    d->rich.notify = notify;
    d->rich.too_much = too_much;
    d->rich.edit.context = d;
    d->rich.edit.undo = undo;
    d->rich.edit.reserve = reserve;
    d->rich.edit.commit = commit;
    d->rich.edit.cancel = cancel;
    d->rich.edit.bump = bump;
    d->rich.edit.validate = validate;
    d->rich.edit.read_input = read_input;
    d->rich.edit.paragraphs.context = d;
    d->rich.edit.paragraphs.query = paragraph_query;
    d->rich.edit.format.context = d;
    d->rich.edit.format.font_name = font_name;
    p = &d->rich.edit.format.properties;
    p->context = d;
    p->count = count;
    p->get = get;
    p->put = put;
    p->insert = insert;
    p->remove = remove_entry;
    p->clear_cache = clear_cache;
    p->paragraph_end = paragraph_end;
    p->diagnostic = diagnostic;
    a = &p->arguments;
    a->context = d;
    a->objects.context = d;
    a->objects.get = owner;
    a->objects.set = publish;
    a->create = create;
    a->count = argument_count;
    a->free_slot = free_slot;
    a->data = data;
    a->fixed = fixed;
    a->put_fixed = put_fixed;
    a->add = add;
    a->remove = remove_argument;
    a->diagnostic = diagnostic;
    a->validate_input = validate_input;
}
static void init(Pair *p, unsigned unit)
{
    memset(p, 0, sizeof(*p));
    p->trace.ordinal = 1;
    init_doc(&p->source, &p->trace, unit);
    init_doc(&p->target, &p->trace, unit);
    p->copy.context = p;
    p->copy.target = p->target.rich;
    p->copy.source = p->source.rich.edit.format.properties;
    p->copy.source_characters = source_characters;
    p->copy.source_bytes = source_bytes;
    p->copy.lock_source = lock_source;
    p->copy.unlock_source = unlock_source;
    p->copy.source_stream = source_stream;
    p->copy.check_tail = check_tail;
    p->copy.warning = warning;
    p->object.context = &p->target;
    p->object.unit = unit;
    p->object.target = p->target.rich;
    p->object.bytes = object_bytes;
    p->object.allocate = allocate;
    p->object.publish_at = publish_at;
    p->object.publish_bytes = publish_bytes;
    p->object.dispatch = dispatch;
    p->object.diagnostic = diagnostic;
}
static void reset(Pair *p, char fail, int result)
{
    memset(&p->trace, 0, sizeof(p->trace));
    p->trace.ordinal = 1;
    p->trace.fail = fail;
    p->trace.result = result;
}
static MMU32 key(unsigned kind, MMI32 cp)
{
    return ((MMU32)kind << 24) | (MMU32)cp;
}
static void seed(Doc *d, unsigned kind, MMI32 cp, MMI32 value)
{
    MMTextPropertyEntry e;
    MMI32 i;
    e.key = key(kind, cp);
    e.value = value;
    for (i = 0; i < d->count && d->entries[i].key < e.key; ++i)
        ;
    CHECK(insert(d, i, &e) == 1);
}
static MMI32 argument(Doc *d, unsigned kind, const char *value)
{
    MMI32 ref = 0;
    CHECK(mm_text_argument_ensure(kind, value, (MMI32)strlen(value) + 1, &ref,
                                  &d->rich.edit.format.properties.arguments) == 1);
    return ref;
}
static MMI32 value(Doc *d, unsigned kind, MMI32 cp)
{
    MMTextPropertyEntry e;
    return mm_text_property_find(key(kind, cp), d->characters, &e, 0, 0, &d->rich.edit.format.properties) ? e.value : 0;
}
static void properties(unsigned unit)
{
    Pair p;
    int result;
    MMI32 ref;
    init(&p, unit);
    seed(&p.source, 1, 0, 7);
    seed(&p.source, 1, 3, 19);
    seed(&p.source, 64, 1, argument(&p.source, 64, "client"));
    reset(&p, 0, 0);
    CHECK(mm_text_copy_properties(1, 2, 4, 6, 0, 128, &p.copy) == 1);
    CHECK(value(&p.target, 1, 1) == 0 && value(&p.target, 1, 2) == 7);
    CHECK(value(&p.target, 1, 4) == 19 && value(&p.target, 1, 7) == 19);
    CHECK(value(&p.target, 1, 8) == 0 && p.source.count == 3);
    ref = value(&p.target, 64, 6);
    CHECK(ref > 0 && !strcmp((const char *)p.target.arguments[ref - 1].bytes, "client"));
    CHECK(strstr(p.trace.log, "sliGpAu") && !p.source.locks);
    /* Clearing walks the mutating table: removing kind 1 shifts kind 3
     * past the cursor, so it remains. This is intentional legacy behavior. */
    init(&p, unit);
    seed(&p.target, 1, 0, 7);
    seed(&p.target, 3, 0, 8);
    seed(&p.target, 5, 0, 9);
    CHECK(mm_text_copy_properties(0, 0, 9, 9, 0, 128, &p.copy) == 1);
    CHECK(p.target.count == 1 && value(&p.target, 3, 0) == 8);
    for (result = 0; result >= -2; --result)
    {
        init(&p, unit);
        seed(&p.source, 64, 0, argument(&p.source, 64, "payload"));
        reset(&p, 'A', result);
        CHECK(mm_text_copy_properties(0, 0, 4, 6, 64, 128, &p.copy) == (result ? result : 1));
        CHECK(!p.source.locks && strchr(p.trace.log, 'u'));
        CHECK(p.target.count == 0 && p.target.warnings[MM_TEXT_COPY_ARGUMENT] == !result);
        init(&p, unit);
        seed(&p.source, 64, 0, argument(&p.source, 64, "payload"));
        reset(&p, 'R', result);
        CHECK(mm_text_copy_properties(0, 0, 4, 6, 64, 128, &p.copy) == (result ? result : 1));
        CHECK(!p.source.locks && p.target.slots == 1 && p.target.arguments[0].free == !result);
        CHECK(p.target.warnings[MM_TEXT_COPY_GROW] == !result);
    }
}
static void document(unsigned unit)
{
    Pair p;
    int result;
    unsigned kind;
    init(&p, unit);
    CHECK(mm_text_copy_document(0, 9, 0, 9, 7, &p.copy) == 1);
    CHECK(p.target.undo_inserted == 9 && p.target.tail == 1);
    CHECK(p.target.replacement.inserted == 8 && p.target.replacement.deleted == 8);
    CHECK(p.target.notify_inserted == 9 && p.target.notify_deleted == 9 && p.target.flags == 7);
    init(&p, unit);
    CHECK(mm_text_copy_document(8, 1, 8, 1, 0, &p.copy) == 1);
    CHECK(p.target.tail == 1 && !strchr(p.trace.log, 'D') && p.target.notifications == 1);
    init(&p, unit);
    CHECK(mm_text_copy_document(0, 0, 1, 0, 0, &p.copy) == 1);
    CHECK(p.target.undos == 1 && p.target.commits == 1 && p.target.notify_inserted == 0);
    init(&p, unit);
    p.target.characters = 0x007fffffL;
    CHECK(mm_text_copy_document(0, 1, 1, 0, 0, &p.copy) == 0);
    CHECK(!strcmp(p.trace.log, "Q") && !p.target.undos);
    for (result = 0; result >= -2; --result)
    {
        init(&p, unit);
        reset(&p, 'D', result);
        CHECK(mm_text_copy_document(1, 3, 2, 1, 0, &p.copy) == result);
        CHECK(p.target.cancels == !result && !p.target.commits && p.target.characters == 9);
        init(&p, unit);
        reset(&p, 'U', result);
        CHECK(mm_text_copy_document(1, 3, 2, 1, 0, &p.copy) == result);
        CHECK(!p.target.cancels && !strchr(p.trace.log, 'D'));
    }
    for (kind = 192; kind <= 195; ++kind)
    {
        init(&p, unit);
        seed(&p.source, kind, 3, argument(&p.source, kind, "object"));
        reset(&p, 0, 0);
        CHECK(mm_text_copy_document(2, 3, 1, 0, 5, &p.copy) == 1);
        CHECK(p.target.count == 1 && p.target.entries[0].key == key(kind, 2));
        CHECK(p.target.arguments[0].fixed == ((MMU32)kind << 24) + 1);
        CHECK(strstr(p.trace.log, "lsiGpAu") && !p.source.locks);
        for (result = 0; result >= -2; --result)
        {
            init(&p, unit);
            seed(&p.source, kind, 3, argument(&p.source, kind, "object"));
            reset(&p, 'X', result);
            CHECK(mm_text_copy_document(2, 3, 1, 0, 5, &p.copy) == (result ? result : 1));
            CHECK(p.target.arguments[0].free == !result && !p.target.cancels);
            CHECK(p.target.characters == 12 && p.target.commits == !result);
            init(&p, unit);
            seed(&p.source, kind, 3, argument(&p.source, kind, "object"));
            reset(&p, 'A', result);
            CHECK(mm_text_copy_document(2, 3, 1, 0, 5, &p.copy) == (result ? result : 1));
            CHECK(!p.source.locks && !p.target.warnings[MM_TEXT_COPY_ARGUMENT]);
        }
    }
}
static void queries(unsigned unit)
{
    Pair p;
    MMI32 at, bytes;
    void *out;
    int result;
    init(&p, unit);
    seed(&p.target, 192, 3, argument(&p.target, 192, "object"));
    seed(&p.target, 194, 6, argument(&p.target, 194, "custom"));
    reset(&p, 0, 0);
    bytes = 123;
    out = &p;
    CHECK(mm_text_object_fetch(4, &at, &out, &bytes, &p.object) == 0);
    CHECK(at == 9 && bytes == 123 && out == &p && !strcmp(p.trace.log, "a"));
    reset(&p, 0, 0);
    CHECK(mm_text_object_fetch(1, &at, &out, &bytes, &p.object) == 1);
    CHECK(at == 3 && bytes == 7 && out == p.target.allocation && !strcmp((const char *)out, "object"));
    CHECK(!strcmp(p.trace.log, "aszsH") && p.target.published_at == 3 && p.target.published_bytes == 7);
    CHECK(mm_text_object_fetch(3, &at, 0, 0, &p.object) == 1 && at == 3);
    CHECK(!mm_text_object_dispatch(2, 0, &p.object) && !mm_text_object_dispatch(6, 1, &p.object));
    CHECK(mm_text_object_dispatch(3, 0, &p.object) == 1 && !p.target.draw);
    CHECK(mm_text_object_dispatch(3, 1, &p.object) == 1 && p.target.draw && p.target.dispatched == 2);
    for (result = 0; result >= -2; --result)
    {
        reset(&p, 'H', result);
        p.target.alloc_partial = result < 0;
        out = &p;
        CHECK(mm_text_object_fetch(3, &at, &out, &bytes, &p.object) == (result ? result : 1));
        CHECK(out == (result ? p.target.allocation : 0) && at == 3 && bytes == 7);
        CHECK(!strcmp(p.trace.log, "aszsH"));
    }
}
static void objects(unsigned unit)
{
    Pair p;
    int result;
    MMI32 ref;
    init(&p, unit);
    CHECK(mm_text_object_insert("new", 4, 2, 1, 7, &p.object) == 1);
    CHECK(p.target.characters == 9 && value(&p.target, 192, 2) == 1);
    CHECK(p.target.bumps == 2 && p.target.notifications == 1 && p.target.flags == 7);
    CHECK(p.target.notify_inserted == 1 && p.target.notify_deleted == 1);
    reset(&p, 0, 0);
    CHECK(mm_text_object_apply("new", 4, 2, 3, &p.object) == 1);
    CHECK(p.target.arguments[0].fixed == 0xc0000001UL && p.target.notifications == 2);
    CHECK(strstr(p.trace.log, "rwrM"));
    CHECK(mm_text_object_apply("other", 6, 2, 0, &p.object) == 1);
    ref = value(&p.target, 192, 2);
    CHECK(ref == 2 && p.target.arguments[0].free && !strcmp((const char *)p.target.arguments[1].bytes, "other"));
    reset(&p, 0, 0);
    CHECK(!mm_text_object_apply("x", 2, 3, 0, &p.object));
    CHECK(!strcmp(p.trace.log, "!") && p.target.diagnostics[MM_TEXT_OBJECT_NOT_AT] == 1);
    for (result = 0; result >= -2; --result)
    {
        init(&p, unit);
        reset(&p, 'A', result);
        CHECK(mm_text_object_insert("new", 4, 2, 0, 0, &p.object) == result);
        CHECK(p.target.cancels == !result && !p.target.diagnostics[MM_TEXT_ARG_INDEX]);
        init(&p, unit);
        p.object.target.has_character = 1;
        reset(&p, 'R', result);
        CHECK(mm_text_object_insert("new", 4, 2, 0, 0, &p.object) == result);
        CHECK(p.target.slots == 2 && p.target.arguments[0].free == !result && !p.target.arguments[1].free);
        CHECK((p.target.arguments[1].fixed >> 24) == 2 && p.target.cancels == !result);
        init(&p, unit);
        p.object.target.has_character = 1;
        reset(&p, 'D', result);
        CHECK(mm_text_object_insert("new", 4, 2, 0, 0, &p.object) == result);
        CHECK(p.target.slots == 2 && p.target.arguments[0].free == !result);
        CHECK(p.target.arguments[1].free == !result && p.target.cancels == !result);
        if (!result)
            CHECK(strstr(p.trace.log, "DfoC"));
        init(&p, unit);
        reset(&p, 'X', result);
        CHECK(mm_text_object_insert("new", 4, 2, 0, 0, &p.object) == (result ? result : 1));
        CHECK(!p.target.arguments[0].free && p.target.diagnostics[MM_TEXT_OBJECT_RESERVED_INSERT] == !result);
        CHECK(p.target.commits == !result && p.target.characters == 10);
        init(&p, unit);
        seed(&p.target, 192, 2, argument(&p.target, 192, "old"));
        reset(&p, 'A', result);
        CHECK(mm_text_object_apply("new", 4, 2, 0, &p.object) == result);
        CHECK(value(&p.target, 192, 2) == 1 && !p.target.arguments[0].free && p.target.cancels == !result);
    }
    init(&p, unit);
    p.object.target.has_character = 1;
    reset(&p, 'F', 0);
    CHECK(!mm_text_object_insert("new", 4, 2, 0, 0, &p.object));
    CHECK(p.target.slots == 1 && p.target.arguments[0].free && p.target.cancels == 1);
    init(&p, unit);
    p.object.target.has_character = 1;
    CHECK(mm_text_object_insert("new", 4, 2, 0, 0, &p.object) == 1);
    CHECK(value(&p.target, 2, 2) > 0 && value(&p.target, 192, 2) == 1);
}
int main(void)
{
    unsigned unit;
    for (unit = 1; unit <= 2; ++unit)
    {
        properties(unit);
        document(unit);
        queries(unit);
        objects(unit);
    }
    puts("text copy tests passed");
    return 0;
}
