#include "mm/text_copy.h"
#include "mm/byte_text.h"
#include "mm/text.h"
#include "text_argument_view_fixture.h"
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
    MMCollectionStorage *pending_source;
} Trace;
typedef struct Argument
{
    MMU32 fixed;
    unsigned char bytes[522];
    MMI32 length;
    int free;
} Argument;
typedef struct Doc
{
    Trace *trace;
    MMTextPropertyEntry entries[128];
    MMI32 count, characters, slots;
    Argument arguments[32];
    MMCollectionStorage argument_storage;
    MMGroupLocation argument_locations[32];
    void *group;
    MMTextReplaceRichServices rich;
    MMTextFormatConfig config;
    MMTextCharacter input;
    MMI32 undo_at, undo_end, undo_inserted, notify_at, notify_inserted, notify_deleted;
    MMU32 flags;
    MMTextReplaceRequest replacement;
    int warnings[2], diagnostics[4], undos, commits, cancels, notifications, bumps, tail;
    int alloc_partial, shrink_on_allocate, freed, dispatched, draw, dispatch_index;
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
    Doc *d = v;
    CHECK(kind < 256 && bytes && n > 0);
    if (d->trace->pending_source)
    {
        CHECK(d->trace->pending_source->buffers[0].locks == 1);
        d->trace->pending_source = NULL;
    }
    event(d, 'i');
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
static MMGroup argument_view(void *v)
{
    Doc *d = v;
    MMI32 i;
    event(d, 's');
    for (i = 0; i < d->slots; ++i)
    {
        Argument *slot = &d->arguments[i];
        d->argument_locations[i].offset = slot->free ? -1 : (MMI32)(i * sizeof(*slot));
        d->argument_locations[i].bytes = slot->free ? 0 : 4 + slot->length;
    }
    return text_argument_view(&d->argument_storage, d->arguments, sizeof(d->arguments),
                              d->argument_locations, (MMU32)d->slots);
}
static MMGroup source_arguments(void *v)
{
    Doc *d = &((Pair *)v)->source;
    d->trace->pending_source = &d->argument_storage;
    return argument_view(d);
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
static int allocate(void *v, MMI32 bytes, void **out)
{
    Doc *d = (Doc *)v;
    int r = effect(d, 'H');
    CHECK(bytes > 0 && bytes <= 522);
    if (r > 0 && d->shrink_on_allocate)
        d->arguments[0].length = 1;
    *out = r > 0 || d->alloc_partial ? d->allocation : 0;
    return r;
}
static void free_output(void *v, void *data)
{
    Doc *d = v;
    CHECK(data == d->allocation);
    ++d->freed;
    event(d, 'f');
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
    CHECK(mm_storage_init(&d->argument_storage, NULL) == MM_STORAGE_OK);
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
    p->rows.context = d;
    p->rows.count = count;
    p->rows.get = get;
    p->rows.put = put;
    p->rows.insert = insert;
    p->rows.remove = remove_entry;
    p->clear_cache = clear_cache;
    p->paragraph_end = paragraph_end;
    p->diagnostic = diagnostic;
    a = &p->arguments;
    a->context = d;
    a->objects.context = d;
    a->objects.get = owner;
    a->objects.set = publish;
    a->create = create;
    a->rows.context = d;
    a->rows.count = argument_count;
    a->rows.free_slot = free_slot;
    a->rows.data = data;
    a->rows.fixed = fixed;
    a->rows.put_fixed = put_fixed;
    a->rows.add = add;
    a->rows.remove = remove_argument;
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
    p->copy.source_arguments = source_arguments;
    p->copy.source_stream = source_stream;
    p->copy.check_tail = check_tail;
    p->copy.warning = warning;
    p->object.context = &p->target;
    p->object.unit = unit;
    p->object.target = p->target.rich;
    p->object.arguments = argument_view;
    p->object.allocate = allocate;
    p->object.free = free_output;
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
    CHECK(strstr(p.trace.log, "siGpA") && !p.source.argument_storage.buffers[0].locks);
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
        CHECK(!p.source.argument_storage.buffers[0].locks && !p.trace.pending_source);
        CHECK(p.target.count == 0 && p.target.warnings[MM_TEXT_COPY_ARGUMENT] == !result);
        init(&p, unit);
        seed(&p.source, 64, 0, argument(&p.source, 64, "payload"));
        reset(&p, 'R', result);
        CHECK(mm_text_copy_properties(0, 0, 4, 6, 64, 128, &p.copy) == (result ? result : 1));
        CHECK(!p.source.argument_storage.buffers[0].locks && p.target.slots == 1 && p.target.arguments[0].free == !result);
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
        CHECK(strstr(p.trace.log, "siGpA") && !p.source.argument_storage.buffers[0].locks);
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
            CHECK(!p.source.argument_storage.buffers[0].locks && !p.target.warnings[MM_TEXT_COPY_ARGUMENT]);
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
    CHECK(!strcmp(p.trace.log, "aszHs") && p.target.published_at == 3 && p.target.published_bytes == 7);
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
        CHECK(!strcmp(p.trace.log, "aszH"));
    }
    reset(&p, 0, 0);
    p.target.shrink_on_allocate = 1;
    memset(p.target.allocation, 0xa5, sizeof(p.target.allocation));
    CHECK(mm_text_object_fetch(3, &at, &out, &bytes, &p.object) == 1);
    CHECK(at == 3 && bytes == 7 && !out && p.target.freed == 1 && p.target.allocation[0] == 0xa5);
    CHECK(!strcmp(p.trace.log, "aszHsf"));
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
typedef struct ClipboardTest
{
    MMTextClipboardOperation operations[24];
    int count, fail, refs, rich, acquisitions, formats[2], size;
    MMI32 anchor, focus, first, inserted, deleted, caret;
    void *published;
} ClipboardTest;

static int clipboard_call(void *context, MMTextClipboardOperation operation, MMTextClipboardRequest *q)
{
    ClipboardTest *t = context;
    t->operations[t->count++] = operation;
    if ((int)operation == t->fail)
        return 0;
    switch (operation)
    {
    case MM_TEXT_CLIPBOARD_SELECTION:
        q->anchor = t->anchor;
        q->focus = t->focus;
        break;
    case MM_TEXT_CLIPBOARD_ACQUIRE:
        t->formats[t->acquisitions++] = q->format;
        if (q->format == MM_TEXT_CLIPBOARD_RICH && !t->rich)
            return 0;
        /* Fall through to produce the owned handle. */
    case MM_TEXT_CLIPBOARD_ALLOCATE:
        q->document = t;
        ++t->refs;
        break;
    case MM_TEXT_CLIPBOARD_RELEASE:
        CHECK(q->document == t && t->refs == 1);
        --t->refs;
        break;
    case MM_TEXT_CLIPBOARD_SIZE: q->inserted = t->size; break;
    case MM_TEXT_CLIPBOARD_IS_RICH: return t->rich;
    case MM_TEXT_CLIPBOARD_PUBLISH: t->published = q->document; break;
    case MM_TEXT_CLIPBOARD_COPY:
    case MM_TEXT_CLIPBOARD_PASTE_PLAIN:
    case MM_TEXT_CLIPBOARD_PASTE_RICH:
        t->first = q->first;
        t->inserted = q->inserted;
        t->deleted = q->deleted;
        break;
    case MM_TEXT_CLIPBOARD_CARET: t->caret = q->first; break;
    default: break;
    }
    return 1;
}

static void clipboard_init(ClipboardTest *t)
{
    memset(t, 0, sizeof(*t));
    t->fail = -1;
    t->anchor = 9;
    t->focus = 4;
    t->size = 4;
}

static void clipboard(void)
{
    ClipboardTest t;
    MMTextClipboardServices s = {0};
    s.context = &t;
    s.call = clipboard_call;
    s.kind = MM_TEXT_CLIPBOARD_RICH;
    clipboard_init(&t);
    CHECK(mm_text_clipboard_copy(0, &s) == 1 && t.count == 1 && !t.refs);
    t.count = 0;
    CHECK(mm_text_clipboard_copy(1, &s) == 1 && t.count == 7);
    CHECK(t.first == 4 && t.inserted == 5 && t.published == &t && t.refs == 1);
    CHECK(t.operations[2] == MM_TEXT_CLIPBOARD_SELECTION && t.operations[3] == MM_TEXT_CLIPBOARD_SUSPEND &&
          t.operations[5] == MM_TEXT_CLIPBOARD_RESUME);
    clipboard_init(&t);
    t.fail = MM_TEXT_CLIPBOARD_COPY;
    t.published = &t;
    CHECK(!mm_text_clipboard_copy(1, &s) && !t.refs && !t.published);
    CHECK(t.operations[t.count - 2] == MM_TEXT_CLIPBOARD_RELEASE);

    clipboard_init(&t);
    CHECK(!mm_text_clipboard_paste(0, 1, &s) && !t.count);
    CHECK(mm_text_clipboard_paste(1, 0, &s) == 1 && t.count == 1 && !t.acquisitions);
    t.count = 0;
    CHECK(mm_text_clipboard_paste(1, 1, &s) == 1);
    CHECK(t.acquisitions == 2 && t.formats[0] == MM_TEXT_CLIPBOARD_RICH && t.formats[1] == MM_TEXT_CLIPBOARD_PLAIN);
    CHECK(t.first == 4 && t.deleted == 5 && t.inserted == 3 && t.caret == 7 && !t.refs);
    CHECK(t.operations[7] == MM_TEXT_CLIPBOARD_IS_RICH && t.operations[8] == MM_TEXT_CLIPBOARD_PLAIN_STYLE &&
          t.operations[9] == MM_TEXT_CLIPBOARD_PASTE_PLAIN && t.operations[11] == MM_TEXT_CLIPBOARD_BUMP_UNDO);

    clipboard_init(&t);
    t.rich = 1;
    t.fail = MM_TEXT_CLIPBOARD_PASTE_RICH;
    CHECK(!mm_text_clipboard_paste(1, 1, &s) && !t.refs && !t.caret);
    CHECK(t.acquisitions == 1 && t.operations[t.count - 1] == MM_TEXT_CLIPBOARD_RELEASE);
    clipboard_init(&t);
    t.rich = t.size = 1;
    CHECK(mm_text_clipboard_paste(1, 1, &s) == 1 && t.count == 4 && !t.refs && !t.caret);

}

typedef struct ByteClipboard
{
    MMTextClipboardServices clipboard;
    MMByteTextEditServices edit;
    MMPieceTable view, document;
    void *published;
    int refs, shrink;
    char log[24], fail;
    unsigned count;
    MMI32 anchor, focus, at, inserted, deleted, caret;
} ByteClipboard;

static int byte_event(ByteClipboard *t, char c)
{
    CHECK(t->count + 1 < sizeof(t->log));
    t->log[t->count++] = c;
    t->log[t->count] = 0;
    return c == t->fail ? -2 : 1;
}

static int byte_call(void *context, MMTextClipboardOperation op, MMTextClipboardRequest *q)
{
    ByteClipboard *t = context;
    switch (op)
    {
    case MM_TEXT_CLIPBOARD_SELECTION:
        q->anchor = t->anchor;
        q->focus = t->focus;
        return byte_event(t, 's');
    case MM_TEXT_CLIPBOARD_HAS_FORMAT:
        CHECK(q->format == MM_TEXT_CLIPBOARD_STREAM);
        return byte_event(t, 'f');
    case MM_TEXT_CLIPBOARD_ACQUIRE:
        CHECK(q->format == MM_TEXT_CLIPBOARD_STREAM);
        q->document = &t->document;
        ++t->refs;
        return byte_event(t, 'g');
    case MM_TEXT_CLIPBOARD_ALLOCATE:
        CHECK(!t->document.length);
        q->document = &t->document;
        ++t->refs;
        return byte_event(t, 'a');
    case MM_TEXT_CLIPBOARD_RELEASE:
        CHECK(q->document == &t->document && t->refs == 1);
        --t->refs;
        return byte_event(t, 'r');
    case MM_TEXT_CLIPBOARD_PUBLISH:
        t->published = q->document;
        return byte_event(t, 'p');
    default:
        CHECK(0);
        return 0;
    }
}

static MMPieceTable *byte_pieces(void *v, void *p)
{
    ByteClipboard *t = v;
    CHECK(!p || (p == &t->document && t->refs == 1));
    byte_event(t, p ? 'd' : 'v');
    return p ? &t->document : &t->view;
}

static int byte_hide(void *v)
{
    ByteClipboard *t = v;
    if (t->shrink)
        CHECK(mm_piece_table_replace(&t->document, 0, 2, NULL, 0) == MM_DATA_OK);
    return byte_event(t, 'h');
}

static int byte_invalidate(void *v, MMI32 at, MMI32 inserted, MMI32 deleted)
{
    ByteClipboard *t = v;
    CHECK(!t->refs);
    t->at = at;
    t->inserted = inserted;
    t->deleted = deleted;
    return byte_event(t, 'i');
}

static int byte_caret(void *v, MMI32 at)
{
    ByteClipboard *t = v;
    t->caret = at;
    return byte_event(t, 'c');
}

static int byte_show(void *v)
{
    return byte_event(v, 'w');
}

static void byte_file_ref(void *v, void *p)
{
    (void)v;
    (void)p;
    CHECK(0);
}

static void byte_clipboard_init(ByteClipboard *t, const void *data, MMU32 length)
{
    MMPieceFiles files = {0};
    memset(t, 0, sizeof(*t));
    files.retain = byte_file_ref;
    files.release = byte_file_ref;
    CHECK(mm_piece_table_init(&t->view, NULL, &files, 0) == MM_DATA_OK);
    CHECK(mm_piece_table_init(&t->document, NULL, &files, 0) == MM_DATA_OK);
    CHECK(mm_piece_table_replace(&t->view, 0, 0, "0123456789", 10) == MM_DATA_OK);
    CHECK(mm_piece_table_replace(&t->document, 0, 0, data, length) == MM_DATA_OK);
    t->anchor = 9;
    t->focus = 4;
    t->edit.context = t;
    t->edit.pieces = byte_pieces;
    t->edit.hide = byte_hide;
    t->edit.invalidate = byte_invalidate;
    t->edit.caret = byte_caret;
    t->edit.show = byte_show;
    t->clipboard.context = t;
    t->clipboard.kind = MM_TEXT_CLIPBOARD_STREAM;
    t->clipboard.call = byte_call;
    t->clipboard.byte_edit = &t->edit;
}

static void byte_clipboard_dispose(ByteClipboard *t)
{
    CHECK(!t->refs);
    CHECK(mm_piece_table_dispose(&t->view) == MM_DATA_OK);
    CHECK(mm_piece_table_dispose(&t->document) == MM_DATA_OK);
}

static void check_bytes(MMPieceTable *pieces, const void *expected, MMU32 n)
{
    unsigned char bytes[16];
    CHECK(n <= sizeof(bytes) && pieces->length == n);
    CHECK(mm_piece_table_read(pieces, 0, bytes, n) == MM_DATA_OK && !memcmp(expected, bytes, n));
}

static void byte_clipboard(void)
{
    ByteClipboard t;
    int bad;
    for (bad = 0; bad <= 1; ++bad)
    {
        byte_clipboard_init(&t, NULL, 0);
        if (bad)
            t.anchor = 11;
        CHECK(mm_text_clipboard_copy(1, &t.clipboard) == !bad);
        CHECK(!strcmp(t.log, bad ? "sadvrp" : "sadvp"));
        if (!bad)
        {
            CHECK(t.published == &t.document && t.refs == 1);
            check_bytes(&t.document, "45678", 5);
            --t.refs;
        }
        else
            CHECK(!t.published && !t.document.length);
        check_bytes(&t.view, "0123456789", 10);
        byte_clipboard_dispose(&t);
    }

    byte_clipboard_init(&t, "A\0\rZ", 4);
    CHECK(mm_text_clipboard_paste(1, 0, &t.clipboard) == 1 && !strcmp(t.log, "f") && !t.refs);
    t.count = 0;
    CHECK(mm_text_clipboard_paste(1, 1, &t.clipboard) == 1 && !strcmp(t.log, "fgdhsvricw"));
    CHECK(t.at == 4 && t.inserted == 4 && t.deleted == 5 && t.caret == 8);
    check_bytes(&t.view, "0123A\0\rZ9", 9);
    byte_clipboard_dispose(&t);
    byte_clipboard_init(&t, "A\0\rZ", 4);
    t.shrink = 1;
    CHECK(!mm_text_clipboard_paste(1, 1, &t.clipboard) && !strcmp(t.log, "fgdhsvr") && !t.caret);
    check_bytes(&t.view, "0123456789", 10);
    byte_clipboard_dispose(&t);
    byte_clipboard_init(&t, NULL, 0);
    CHECK(mm_text_clipboard_paste(1, 1, &t.clipboard) == 1 && !strcmp(t.log, "fgdr"));
    t.count = 0;
    CHECK(mm_text_clipboard_clear(&t.clipboard) == 1 && !strcmp(t.log, "shvicw"));
    CHECK(t.at == 4 && !t.inserted && t.deleted == 5 && t.caret == 4);
    check_bytes(&t.view, "01239", 5);
    byte_clipboard_dispose(&t);

    byte_clipboard_init(&t, NULL, 0);
    CHECK(mm_byte_text_edit("\0N", 2, 9, 4, &t.edit) == 1 && !strcmp(t.log, "hvicw"));
    CHECK(t.at == 4 && t.inserted == 2 && t.deleted == 5 && t.caret == 6);
    check_bytes(&t.view, "0123\0N9", 7);
    t.count = 0;
    CHECK(!mm_byte_text_edit(NULL, 0, 5, 9, &t.edit) && !strcmp(t.log, "hv"));
    check_bytes(&t.view, "0123\0N9", 7);
    t.count = 0;
    t.fail = 'i';
    CHECK(mm_byte_text_edit("Q", 1, 5, 6, &t.edit) == -2 && !strcmp(t.log, "hvi"));
    check_bytes(&t.view, "0123\0Q9", 7);
    byte_clipboard_dispose(&t);

    CHECK(mm_byte_text_rebase(4, 4, 2, 5) == 4 && mm_byte_text_rebase(5, 4, 2, 5) == 5);
    CHECK(mm_byte_text_rebase(8, 4, 2, 5) == 6 && mm_byte_text_rebase(9, 4, 2, 5) == 6);
    CHECK(mm_byte_text_rebase(10, 4, 2, 5) == 7 && mm_byte_text_rebase(5, 4, 2, 0) == 7);
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
    clipboard();
    byte_clipboard();
    puts("text copy and clipboard tests passed");
    return 0;
}
