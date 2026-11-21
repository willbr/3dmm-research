#ifndef TEXT_FORMAT_TEST_HOST_H
#define TEXT_FORMAT_TEST_HOST_H
#include "mm/text_format.h"
#include "mm/text.h"
#include "mm/wide.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "text format failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check(!!(x), __LINE__)
typedef struct Host
{
    MMTextFormatConfig config;
    MMTextFormatEditServices edit;
    MMTextPropertyEntry entries[64];
    MMI32 count;
    unsigned char bytes[523];
    MMI32 length;
    MMU32 fixed;
    int free;
    void *group;
    unsigned name_count;
    char log[512], fail;
    unsigned used;
    int result, occurrence, seen, partial, reads, valid, warnings, dirty, range_published, prepared, mutate_undo,
        reserve_false;
    MMI32 range_first, range_end, expected_first, expected_end;
    int start_response, end_response, is_start, next_response;
    MMTextCharacter input;
} Host;
static void event(Host *h, char c)
{
    CHECK(h->used < 511);
    h->log[h->used++] = c;
    h->log[h->used] = 0;
}
static int effect(Host *h, char c)
{
    event(h, c);
    return h->fail == c && ++h->seen == h->occurrence ? h->result : 1;
}
static void *owner(void *v, int slot)
{
    CHECK(slot == MM_TEXT_OWNER_ARGUMENTS);
    return ((Host *)v)->group;
}
static void publish(void *v, int slot, void *p)
{
    Host *h = (Host *)v;
    CHECK(slot == MM_TEXT_OWNER_ARGUMENTS && !h->group && p == h->bytes);
    h->group = p;
    event(h, 'p');
}
static int create(void *v, MMI32 bytes, void **out)
{
    Host *h = (Host *)v;
    int r = effect(h, 'G');
    CHECK(bytes >= 10 && bytes <= 522);
    if (r > 0 || h->partial)
        *out = h->bytes;
    return r;
}
static MMI32 argument_count(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == h->bytes);
    return h->length ? 1 : 0;
}
static int free_slot(void *v, void *p, MMI32 i)
{
    Host *h = (Host *)v;
    CHECK(p == h->bytes && i == 0);
    return h->free;
}
static const void *data(void *v, void *p, MMI32 i, MMI32 *bytes)
{
    Host *h = (Host *)v;
    CHECK(p == h->bytes && i == 0 && !h->free);
    ++h->reads;
    *bytes = h->length;
    return h->bytes;
}
static MMU32 fixed(void *v, void *p, MMI32 i)
{
    Host *h = (Host *)v;
    CHECK(!free_slot(v, p, i));
    return h->fixed;
}
static void put_fixed(void *v, void *p, MMI32 i, MMU32 n)
{
    Host *h = (Host *)v;
    CHECK(!free_slot(v, p, i));
    h->fixed = n;
}
static int add(void *v, void *p, const void *bytes, MMI32 length, MMU32 value, MMI32 *index)
{
    Host *h = (Host *)v;
    int r = effect(h, 'A');
    CHECK(p == h->bytes && (!h->length || h->free) && length <= 522);
    if (r > 0)
    {
        memcpy(h->bytes, bytes, (size_t)length);
        h->length = length;
        h->fixed = value;
        h->free = 0;
        *index = 0;
    }
    return r;
}
static void remove_argument(void *v, void *p, MMI32 i)
{
    Host *h = (Host *)v;
    CHECK(!free_slot(v, p, i));
    h->free = 1;
    event(h, 'f');
}
static void diagnostic(void *v, int kind)
{
    (void)v;
    (void)kind;
    CHECK(0);
}
static MMI32 count(void *v)
{
    return ((Host *)v)->count;
}
static void get(void *v, MMI32 i, MMTextPropertyEntry *out)
{
    Host *h = (Host *)v;
    CHECK(i >= 0 && i < h->count);
    *out = h->entries[i];
}
static void put(void *v, MMI32 i, const MMTextPropertyEntry *p)
{
    Host *h = (Host *)v;
    CHECK(i >= 0 && i < h->count);
    h->entries[i] = *p;
}
static int insert(void *v, MMI32 i, const MMTextPropertyEntry *p)
{
    Host *h = (Host *)v;
    int r = effect(h, 'X');
    CHECK(i >= 0 && i <= h->count && h->count < 64);
    if (r > 0)
    {
        memmove(h->entries + i + 1, h->entries + i, (size_t)(h->count - i) * sizeof(*p));
        h->entries[i] = *p;
        ++h->count;
    }
    return r;
}
static void remove_entry(void *v, MMI32 i)
{
    Host *h = (Host *)v;
    CHECK(i >= 0 && i < h->count);
    --h->count;
    memmove(h->entries + i, h->entries + i + 1, (size_t)(h->count - i) * sizeof(h->entries[0]));
}
static void clear_cache(void *v)
{
    event((Host *)v, 'k');
}
static int font_name(void *v, MMI32 font, MMMovieTitle *name)
{
    Host *h = (Host *)v;
    unsigned short text[255];
    unsigned char narrow[255];
    unsigned i;
    int r = effect(h, 'F');
    CHECK(font == 17);
    for (i = 0; i < h->name_count; ++i)
    {
        text[i] = (unsigned short)(i == 1 ? 0 : 'A' + i % 26);
        narrow[i] = (unsigned char)text[i];
    }
    if (r > 0)
        CHECK(mm_movie_title_set(name, name->unit == 1 ? (void *)narrow : (void *)text, h->name_count));
    return r;
}
static int valid_font(void *v, MMI32 font)
{
    Host *h = (Host *)v;
    CHECK(font == 17);
    ++h->valid;
    return effect(h, 'L');
}
static void bad_font_record(void *v)
{
    ++((Host *)v)->warnings;
}
static int query(void *v, int kind, MMI32 cp, MMI32 *out)
{
    Host *h = (Host *)v;
    int r = effect(h, "SEIN"[kind]);
    (void)cp;
    if (r > 0)
        *out = kind == MM_TEXT_PARA_START      ? h->start_response
               : kind == MM_TEXT_PARA_END      ? h->end_response
               : kind == MM_TEXT_PARA_IS_START ? h->is_start
                                               : h->next_response;
    return r;
}
static void range(void *v, MMI32 first, MMI32 end)
{
    Host *h = (Host *)v;
    h->range_first = first;
    h->range_end = end;
    h->range_published = 1;
    event(h, 'P');
}
static void bump(void *v)
{
    event((Host *)v, 'b');
}
static void validate(void *v)
{
    event((Host *)v, 'v');
}
static int undo(void *v, MMI32 first, MMI32 end, MMI32 inserted)
{
    Host *h = (Host *)v;
    CHECK(first == h->expected_first && end == h->expected_end && inserted == end - first);
    if (h->range_published)
        CHECK(first == h->range_first && end == h->range_end);
    if (h->mutate_undo)
    {
        h->config.font = 17;
        h->input.height = 31;
    }
    return effect(h, 'U');
}
static int cancel(void *v)
{
    return effect((Host *)v, 'C');
}
static int commit(void *v)
{
    return effect((Host *)v, 'M');
}
static int reserve(void *v, MMI32 n)
{
    Host *h = (Host *)v;
    CHECK(n >= 0 && n <= 8);
    if (h->reserve_false)
    {
        event(h, 'R');
        return 0;
    }
    return effect(h, 'R');
}
static int invalidate(void *v, MMI32 cp, MMI32 span, MMU32 flags)
{
    Host *h = (Host *)v;
    CHECK(cp == h->expected_first && span == h->expected_end - cp && flags == 7);
    h->dirty = 1;
    return effect(h, 'V');
}
#ifndef MM_TEXT_FORMAT_NO_LATE_INPUT
static MMI32 characters(void *v)
{
    event((Host *)v, 'q');
    return 9;
}
static void read_input(void *v, int paragraph, MMTextFormatConfig *c, void *value, void *old, int *has_old)
{
    Host *h = (Host *)v;
    (void)old;
    CHECK(!paragraph && strchr(h->log, 'U'));
    ++h->prepared;
    event(h, 'T');
    *c = h->config;
    *(MMTextCharacter *)value = h->input;
    *has_old = 0;
}
#endif
static void init(Host *h, unsigned unit, int big)
{
    MMTextFormatServices *s;
    MMTextArgumentServices *a;
    MMTextPropertyServices *p;
    memset(h, 0, sizeof(*h));
    h->config.text.unit = unit;
    h->config.text.big = big;
    h->config.text.system =
        unit == 1 ? (big ? MM_TEXT_MAC : MM_TEXT_WINDOWS) : (big ? MM_TEXT_UTF16_BE : MM_TEXT_UTF16_LE);
    h->config.font = 11;
    h->config.height = 12;
    h->config.black = 0x01000000UL;
    h->config.clear = 0xffffffffUL;
    h->config.tab = 18;
    h->config.line = h->config.after = 256;
    h->config.signed_offset = unit == 1 && !big;
    h->name_count = 3;
    h->occurrence = 1;
    h->result = -1;
    h->expected_first = 2;
    h->expected_end = 5;
    h->start_response = 0;
    h->end_response = 6;
    h->next_response = 6;
    h->edit.context = h;
    s = &h->edit.format;
    s->context = h;
    s->font_name = font_name;
    s->valid_font = valid_font;
    s->bad_font_record = bad_font_record;
    p = &s->properties;
    p->context = h;
    p->rows.context = h;
    p->rows.count = count;
    p->rows.get = get;
    p->rows.put = put;
    p->rows.insert = insert;
    p->rows.remove = remove_entry;
    p->clear_cache = clear_cache;
    p->diagnostic = diagnostic;
    a = &p->arguments;
    a->context = h;
    a->objects.context = h;
    a->objects.get = owner;
    a->objects.set = publish;
    a->create = create;
    a->rows.context = h;
    a->rows.count = argument_count;
    a->rows.free_slot = free_slot;
    a->rows.data = data;
    a->rows.fixed = fixed;
    a->rows.put_fixed = put_fixed;
    a->rows.add = add;
    a->rows.remove = remove_argument;
    a->diagnostic = diagnostic;
    h->edit.paragraphs.context = h;
    h->edit.paragraphs.query = query;
    h->edit.publish_range = range;
    h->edit.bump = bump;
    h->edit.validate = validate;
    h->edit.undo = undo;
    h->edit.cancel = cancel;
    h->edit.commit = commit;
    h->edit.reserve = reserve;
    h->edit.invalidate = invalidate;
    h->input.style = 3;
    h->input.font = 17;
    h->input.height = 19;
    h->input.offset = -7;
    h->input.foreground = 0x01010203UL;
    h->input.background = 0x01040506UL;
}
#endif
