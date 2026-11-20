#include "mm/text_properties.h"
#include "mm/caption_edit.h"
#include "mm/wide.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok, int line)
{
    if (!ok)
    {
        fprintf(stderr, "text properties failed at %d\n", line);
        exit(1);
    }
}
#define CHECK(x) check((x), __LINE__)
typedef struct Slot
{
    unsigned char bytes[64];
    MMI32 length;
    MMU32 fixed;
    int free;
} Slot;
typedef struct Host
{
    MMTextPropertyServices s;
    MMTextPropertyEntry entries[256];
    MMI32 count;
    MMTextPropertyChange changes[8];
    Slot slots[32];
    MMI32 slots_count;
    void *group;
    int clears, diagnostics[4], create_result, add_result, partial, inserted, fail_insert, insert_result;
    int para_result, para_calls, publications, created;
    MMI32 para_at, para_end;
    unsigned char text[32];
    MMI32 characters;
} Host;
static void *get_owner(void *v, int slot)
{
    CHECK(slot == MM_TEXT_OWNER_ARGUMENTS);
    return ((Host *)v)->group;
}
static void set_owner(void *v, int slot, void *p)
{
    Host *h = (Host *)v;
    CHECK(slot == MM_TEXT_OWNER_ARGUMENTS && !h->group && p == h->slots);
    h->group = p;
    ++h->publications;
}
static int create(void *v, MMI32 bytes, void **out)
{
    Host *h = (Host *)v;
    CHECK(bytes > 0 && bytes <= 64 && !h->group);
    ++h->created;
    if (h->create_result > 0 || h->partial)
        *out = h->slots;
    return h->create_result;
}
static MMI32 arg_count(void *v, void *p)
{
    Host *h = (Host *)v;
    CHECK(p == h->slots);
    return h->slots_count;
}
static int free_slot(void *v, void *p, MMI32 i)
{
    Host *h = (Host *)v;
    CHECK(p == h->slots && i >= 0 && i < h->slots_count);
    return h->slots[i].free;
}
static const void *data(void *v, void *p, MMI32 i, MMI32 *bytes)
{
    Host *h = (Host *)v;
    CHECK(!free_slot(v, p, i));
    *bytes = h->slots[i].length;
    return h->slots[i].bytes;
}
static MMU32 fixed(void *v, void *p, MMI32 i)
{
    Host *h = (Host *)v;
    CHECK(!free_slot(v, p, i));
    return h->slots[i].fixed;
}
static void put_fixed(void *v, void *p, MMI32 i, MMU32 n)
{
    Host *h = (Host *)v;
    CHECK(!free_slot(v, p, i));
    h->slots[i].fixed = n;
}
static int add(void *v, void *p, const void *bytes, MMI32 length, MMU32 value, MMI32 *out)
{
    Host *h = (Host *)v;
    MMI32 i;
    CHECK(p == h->slots && length > 0 && length <= 64);
    *out = 31;
    if (h->add_result <= 0)
        return h->add_result;
    for (i = 0; i < h->slots_count && !h->slots[i].free; ++i)
    {
    }
    CHECK(i < 32);
    if (i == h->slots_count)
        ++h->slots_count;
    h->slots[i].free = 0;
    h->slots[i].length = length;
    h->slots[i].fixed = value;
    memcpy(h->slots[i].bytes, bytes, (size_t)length);
    *out = i;
    return 1;
}
static void remove_arg(void *v, void *p, MMI32 i)
{
    Host *h = (Host *)v;
    CHECK(!free_slot(v, p, i));
    h->slots[i].free = 1;
}
static void diagnostic(void *v, int kind)
{
    Host *h = (Host *)v;
    CHECK(kind >= 0 && kind < 4);
    ++h->diagnostics[kind];
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
    CHECK(i >= 0 && i <= h->count && h->count < 256);
    if (++h->inserted == h->fail_insert)
        return h->insert_result;
    memmove(h->entries + i + 1, h->entries + i, (size_t)(h->count - i) * sizeof(*p));
    h->entries[i] = *p;
    ++h->count;
    return 1;
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
    ++((Host *)v)->clears;
}
static void change(void *v, MMI32 i, MMTextPropertyChange *out)
{
    Host *h = (Host *)v;
    CHECK(i >= 0 && i < 8);
    *out = h->changes[i];
}
static int paragraph_end(void *v, MMI32 cp, MMI32 *out)
{
    Host *h = (Host *)v;
    MMI32 i;
    ++h->para_calls;
    h->para_at = cp;
    if (h->para_result <= 0)
        return h->para_result;
    if (cp < 0)
    {
        *out = 0;
        return 1;
    }
    for (i = cp; i < h->characters; ++i)
        if (h->text[i] == '\r')
        {
            *out = i + 1;
            return 1;
        }
    *out = h->characters;
    return 1;
}
static void init(Host *h)
{
    MMTextArgumentServices *a;
    MMTextPropertyServices *s;
    memset(h, 0, sizeof(*h));
    h->create_result = h->add_result = h->para_result = 1;
    h->insert_result = -1;
    memcpy(h->text, "ab\rcd\ref\r", 9);
    h->characters = 9;
    s = &h->s;
    s->context = h;
    s->count = count;
    s->get = get;
    s->put = put;
    s->insert = insert;
    s->remove = remove_entry;
    s->clear_cache = clear_cache;
    s->change = change;
    s->paragraph_end = paragraph_end;
    s->diagnostic = diagnostic;
    a = &s->arguments;
    a->context = h;
    a->objects.context = h;
    a->objects.get = get_owner;
    a->objects.set = set_owner;
    a->create = create;
    a->count = arg_count;
    a->free_slot = free_slot;
    a->data = data;
    a->fixed = fixed;
    a->put_fixed = put_fixed;
    a->add = add;
    a->remove = remove_arg;
    a->diagnostic = diagnostic;
}
static MMU32 key(unsigned kind, MMI32 cp)
{
    return ((MMU32)kind << 24) | (MMU32)cp;
}
static MMI32 ensure(Host *h, unsigned kind, const void *bytes, MMI32 length)
{
    MMI32 result = -1;
    CHECK(mm_text_argument_ensure(kind, bytes, length, &result, &h->s.arguments) == 1);
    return result;
}
static void arguments(void)
{
    Host h;
    MMI32 a, b, c, out;
    unsigned kind;
    int failure, partial;
    const unsigned char bytes[] = {1, 0, 2, 3};
    init(&h);
    for (kind = 0; kind < 256; ++kind)
    {
        int expected = (kind < 64 || (kind >= 128 && kind < 160)) ? kind == 2 : !(kind & 1);
        CHECK(mm_text_property_has_argument(kind) == expected);
    }
    a = ensure(&h, 64, bytes, 4);
    b = ensure(&h, 64, bytes, 3);
    c = ensure(&h, 160, bytes, 4);
    CHECK(a == 1 && b == 2 && c == 3 && h.publications == 1 && h.created == 1);
    CHECK(ensure(&h, 64, bytes, 4) == a && h.slots[0].fixed == 0x40000002UL);
    /* Two identical slots are legal; ensure must choose the highest live one. */
    h.slots[3] = h.slots[0];
    h.slots_count = 4;
    CHECK(ensure(&h, 64, bytes, 4) == 4 && h.slots[3].fixed == 0x40000003UL);
    h.slots[3].free = 1;
    CHECK(ensure(&h, 64, bytes, 4) == a && h.slots[0].fixed == 0x40000003UL);
    mm_text_argument_ref(a, 0, &h.s.arguments);
    CHECK(h.slots[0].fixed == 0x40000002UL);
    mm_text_argument_ref(a, 1, &h.s.arguments);
    CHECK(h.slots[0].fixed == 0x40000003UL);
    mm_text_argument_ref(b, 0, &h.s.arguments);
    CHECK(h.slots[1].free);
    CHECK(ensure(&h, 192, "new", 3) == b && h.slots[1].fixed == 0xc0000001UL);
    mm_text_property_ref(4, a, 1, &h.s.arguments);
    mm_text_property_ref(128, a, 1, &h.s.arguments);
    mm_text_property_ref(65, a, 1, &h.s.arguments);
    mm_text_property_ref(64, 0, 1, &h.s.arguments);
    mm_text_property_ref(64, -1, 1, &h.s.arguments);
    CHECK(h.slots[0].fixed == 0x40000003UL);
    mm_text_property_ref(64, a, 0, &h.s.arguments);
    CHECK(h.slots[0].fixed == 0x40000002UL);
    mm_text_argument_ref(0, 0, &h.s.arguments);
    mm_text_argument_ref(5, 1, &h.s.arguments);
    mm_text_argument_ref(4, 1, &h.s.arguments);
    CHECK(h.diagnostics[MM_TEXT_ARG_INDEX] == 3);
    h.slots[0].fixed = 0x40000000UL;
    mm_text_argument_ref(a, 0, &h.s.arguments);
    CHECK(h.slots[0].free);
    h.changes[0].kind = 160;
    h.changes[0].value = (MMU32)c;
    h.changes[1].kind = 192;
    h.changes[1].value = (MMU32)b;
    mm_text_properties_release(2, &h.s);
    CHECK(h.slots[1].free && h.slots[2].free);
    for (failure = 0; failure >= -2; --failure)
        for (partial = 0; partial < 2; ++partial)
        {
            init(&h);
            h.create_result = failure;
            h.partial = partial;
            out = 123;
            CHECK(mm_text_argument_ensure(2, bytes, 4, &out, &h.s.arguments) == failure && out == 123);
            CHECK(h.publications == partial && (h.group != 0) == partial);
            init(&h);
            h.add_result = failure;
            out = 123;
            CHECK(mm_text_argument_ensure(2, bytes, 4, &out, &h.s.arguments) == failure && out == 123);
            CHECK(h.group == h.slots && h.publications == 1 && h.slots_count == 0);
        }
    init(&h);
    mm_text_argument_ref(1, 0, &h.s.arguments);
    CHECK(h.diagnostics[MM_TEXT_ARG_INDEX] == 1);
    a = ensure(&h, 64, bytes, 4);
    h.slots[a - 1].fixed = 0x40ffffffUL;
    CHECK(ensure(&h, 64, bytes, 4) == a && h.diagnostics[MM_TEXT_ARG_REFS] == 1 &&
          h.diagnostics[MM_TEXT_ARG_OVERFLOW] == 1);
    h.slots[a - 1].fixed = 0x40000000UL;
    mm_text_argument_ref(a, 1, &h.s.arguments);
    CHECK(h.slots[a - 1].fixed == 0x40000001UL && h.diagnostics[MM_TEXT_ARG_REFS] == 2);
}
static void lookup(void)
{
    Host h;
    MMI32 i, cp, end, index, expected_index, expected_end;
    unsigned type;
    MMTextPropertyEntry entry, old;
    init(&h);
    h.count = 8;
    h.entries[0].key = key(1, 2);
    h.entries[1].key = key(1, 4);
    h.entries[2].key = key(1, 7);
    h.entries[3].key = key(3, 0);
    h.entries[4].key = key(128, 3);
    h.entries[5].key = key(128, 6);
    h.entries[6].key = key(192, 4);
    h.entries[7].key = key(255, 8);
    for (i = 0; i < h.count; ++i)
        h.entries[i].value = i + 1;
    for (type = 0; type < 256; ++type)
        for (cp = 0; cp < 12; ++cp)
        {
            MMU32 search = key(type, cp);
            int found = 0;
            old.key = 0xdeadbeefUL;
            old.value = 123;
            entry = old;
            expected_index = 0;
            expected_end = 9;
            for (i = 0; i < h.count && h.entries[i].key <= search; ++i)
            {
                expected_index = i + 1;
                if ((h.entries[i].key >> 24) == type)
                    found = 1;
            }
            if (i < h.count && (h.entries[i].key >> 24) == type)
                expected_end = (MMI32)(h.entries[i].key & 0xffffffUL);
            if (found)
                --expected_index;
            CHECK(mm_text_property_find(search, 9, &entry, &end, &index, &h.s) == found);
            CHECK(end == expected_end && index == expected_index);
            CHECK(found ? entry.key == h.entries[index].key && entry.value == h.entries[index].value
                        : !memcmp(&entry, &old, sizeof(old)));
            CHECK(mm_text_property_find(search, 9, 0, 0, 0, &h.s) == found);
        }
    for (i = -1; i <= h.count; ++i)
    {
        entry = old;
        end = 123;
        if (i >= 0 && i < h.count)
        {
            CHECK(mm_text_property_fetch(i, 9, &entry, &end, &h.s));
            CHECK(entry.key == h.entries[i].key && entry.value == h.entries[i].value);
            CHECK(end == (i + 1 < h.count && (h.entries[i + 1].key >> 24) == (entry.key >> 24)
                              ? (MMI32)(h.entries[i + 1].key & 0xffffffUL)
                              : 9));
        }
        else
        {
            CHECK(!mm_text_property_fetch(i, 9, &entry, &end, &h.s));
            CHECK(end == 123 && !memcmp(&entry, &old, sizeof(old)));
        }
    }
}
static MMI32 value_at(Host *h, unsigned type, MMI32 cp)
{
    MMI32 i, value = 0;
    for (i = 0; i < h->count; ++i)
        if ((h->entries[i].key >> 24) == type && (h->entries[i].key & 0xffffffUL) <= (MMU32)cp)
            value = h->entries[i].value;
    return value;
}
static void ordered(Host *h)
{
    MMI32 i;
    for (i = 1; i < h->count; ++i)
        CHECK(h->entries[i - 1].key < h->entries[i].key);
}
static MMU32 random_word(MMU32 *state)
{
    *state = *state * 1664525U + 1013904223U;
    return *state;
}
static void apply(void)
{
    Host h;
    MMU32 model[6][9], state = 12345, mask, value;
    const unsigned types[] = {1, 3, 4, 65, 128, 129};
    MMI32 iteration, cp, span, n, j, k, at, ref;
    unsigned type;
    int failure;
    init(&h);
    memset(model, 0, sizeof(model));
    for (iteration = 0; iteration < 400; ++iteration)
    {
        cp = (MMI32)(random_word(&state) % 9);
        span = 1 + (MMI32)(random_word(&state) % (9 - (MMU32)cp));
        n = 1 + (MMI32)(random_word(&state) % 4);
        for (j = 0; j < n; ++j)
        {
            type = (unsigned)(random_word(&state) % 6);
            mask = random_word(&state);
            value = random_word(&state) & mask;
            h.changes[j].kind = types[type];
            h.changes[j].value = value;
            h.changes[j].mask = mask;
        }
        for (j = n; j-- > 0;)
            for (k = 0; k < 6; ++k)
                if (types[k] == h.changes[j].kind)
                    for (at = cp; at < cp + span; ++at)
                        model[k][at] = h.changes[j].value | (model[k][at] & ~h.changes[j].mask);
        CHECK(mm_text_properties_apply(cp, span, 9, n, &h.s) == 1 && h.clears == iteration + 1);
        ordered(&h);
        for (k = 0; k < 6; ++k)
            for (at = 0; at < 9; ++at)
                CHECK((MMU32)value_at(&h, types[k], at) == model[k][at]);
    }
    init(&h);
    ref = ensure(&h, 64, "x", 1);
    h.changes[0].kind = 64;
    h.changes[0].value = (MMU32)ref;
    h.changes[0].mask = 0xffffffffUL;
    CHECK(mm_text_properties_apply(2, 3, 9, 1, &h.s) == 1 && h.slots[ref - 1].fixed == 0x40000001UL);
    h.changes[0].value = 0;
    CHECK(mm_text_properties_apply(0, 9, 9, 1, &h.s) == 1 && h.slots[ref - 1].free && !h.count);
    for (failure = -1; failure >= -2; --failure)
        for (j = 1; j <= 2; ++j)
        {
            init(&h);
            ref = ensure(&h, 64, "x", 1);
            h.fail_insert = j;
            h.insert_result = failure;
            h.changes[0].kind = 64;
            h.changes[0].value = (MMU32)ref;
            h.changes[0].mask = 0xffffffffUL;
            CHECK(mm_text_properties_apply(2, 3, 9, 1, &h.s) == failure && h.count == j - 1 && h.clears == 1);
            CHECK(h.slots[ref - 1].fixed == 0x40000002UL && !h.diagnostics[MM_TEXT_PROP_INSERT]);
        }
    init(&h);
    h.fail_insert = 1;
    h.insert_result = 0;
    h.changes[0].kind = 1;
    h.changes[0].value = 7;
    h.changes[0].mask = 0xffffffffUL;
    CHECK(mm_text_properties_apply(0, 9, 9, 1, &h.s) == 1 && h.diagnostics[MM_TEXT_PROP_INSERT] == 1);
    init(&h);
    CHECK(mm_text_properties_apply(0, 1, 9, 0, &h.s) == 1 && h.clears == 1);
}
/* Independent per-kind reconstruction oracle: keep entries outside the removed
 * interval, then relocate only its final entry under the retail boundary rule. */
static void adjust_model(Host *h, MMI32 cp, MMI32 ins, MMI32 del, MMTextPropertyEntry *out, MMI32 *count_out)
{
    unsigned type;
    MMI32 i, last, next, end, result, n = 0, at;
    int before;
    for (type = 0; type < 256; ++type)
    {
        last = -1;
        next = -1;
        before = 0;
        for (i = 0; i < h->count; ++i)
            if ((h->entries[i].key >> 24) == type)
            {
                at = (MMI32)(h->entries[i].key & 0xffffffUL);
                if (at < cp)
                    out[n++] = h->entries[i];
                else if (at < cp + del + (type < 192))
                {
                    if (last < 0)
                        before = at == cp;
                    last = i;
                }
                else if (next < 0)
                    next = i;
            }
        if (last >= 0 && type < 192)
        {
            if (type < 128)
                end = cp + ins;
            else if (before)
                end = cp;
            else
            {
                result = paragraph_end(h, cp + ins - 1, &end);
                CHECK(result == 1);
            }
            if (type < 128 || before ||
                (end < h->characters && (next < 0 || (MMI32)(h->entries[next].key & 0xffffffUL) + ins - del > end)))
            {
                out[n] = h->entries[last];
                out[n++].key = key(type, end);
            }
        }
        if (next >= 0)
            for (i = next; i < h->count && (h->entries[i].key >> 24) == type; ++i)
            {
                out[n] = h->entries[i];
                out[n++].key = key(type, (MMI32)(h->entries[i].key & 0xffffffUL) + ins - del);
            }
    }
    *count_out = n;
}
static void adjust(void)
{
    Host h;
    MMTextPropertyEntry expected[256], seed[32];
    MMI32 cp, ins, del, n, i, seed_count, ref;
    const MMI32 points[] = {0, 2, 3, 5, 6, 8};
    const unsigned types[] = {1, 65, 128, 129};
    init(&h);
    n = 0;
    for (i = 0; i < 4; ++i)
        for (cp = 0; cp < 6; ++cp)
        {
            seed[n].key = key(types[i], points[cp]);
            seed[n++].value = cp + 1;
        }
    seed[n].key = key(192, 4);
    seed[n++].value = 1;
    seed_count = n;
    for (cp = 0; cp < 9; ++cp)
        for (del = 0; del < 9 - cp; ++del)
            for (ins = 0; ins < 4; ++ins)
            {
                init(&h);
                ref = ensure(&h, 192, "obj", 3);
                CHECK(ref == 1);
                memcpy(h.entries, seed, (size_t)seed_count * sizeof(seed[0]));
                h.count = seed_count;
                memmove(h.text + cp + ins, h.text + cp + del, (size_t)(9 - cp - del));
                memset(h.text + cp, 'X', (size_t)ins);
                h.characters = 9 + ins - del;
                adjust_model(&h, cp, ins, del, expected, &n);
                h.para_calls = 0;
                CHECK(mm_text_properties_adjust(cp, ins, del, h.characters, &h.s) == 1 && h.clears == 1);
                CHECK(h.count == n && !memcmp(h.entries, expected, (size_t)n * sizeof(expected[0])));
                ordered(&h);
                CHECK(h.slots[0].free == (4 >= cp && 4 < cp + del));
            }
    for (i = 0; i >= -2; --i)
    {
        init(&h);
        h.count = 2;
        h.entries[0].key = key(1, 2);
        h.entries[0].value = 1;
        h.entries[1].key = key(128, 2);
        h.entries[1].value = 2;
        h.para_result = i;
        CHECK(mm_text_properties_adjust(1, 2, 3, 8, &h.s) == i && h.para_calls == 1 && h.para_at == 2);
        CHECK(h.entries[0].key == key(1, 3) && h.entries[1].key == key(128, 2) && h.clears == 1);
    }
    init(&h);
    CHECK(mm_text_properties_adjust(0, 0, 0, 9, &h.s) == 1 && h.clears == 1);
}
static size_t caption_test_encode(void *p, const unsigned short *units, size_t count, char *out, size_t capacity)
{
    size_t bytes;
    (void)p;
    if (count != 1)
        return 0;
    bytes = units[0] == 0x3042 ? 2 : 1;
    if (!out)
        return bytes;
    if (capacity < bytes)
        return 0;
    out[0] = bytes == 2 ? (char)0x82 : '?';
    if (bytes == 2)
        out[1] = (char)0xa0;
    return bytes;
}

static size_t caption_test_decode(void *p, const void *input, size_t bytes, int encoding,
                                  unsigned short *out, size_t capacity)
{
    const unsigned char *data = input;
    (void)p;
    if (!bytes || encoding != MM_TEXT_WINDOWS || (out && !capacity))
        return 0;
    if (out)
        out[0] = bytes == 2 && data[0] == 0x82 && data[1] == 0xa0 ? 0x3042 : data[0];
    return 1;
}

static void caption_encoding(void)
{
    static const char utf8[] = "A\r\n\xe2\x82\xac\nZ\r";
    static const char unsupported[] = "\xe3\x81\x82\xf0\x9f\x98\x80";
    static const unsigned char ansi[] = {'A', 13, 0x80, 13, 'Z', 13};
    static const unsigned char wide[] = {0x42, 0x30, 0x3d, 0xd8, 0, 0xde};
    MMTextHost host = {NULL, caption_test_decode, caption_test_encode};
    unsigned short text[8];
    unsigned char output[16];
    size_t count = 12, bytes = 42;
    CHECK(!mm_caption_edit_decode(utf8, sizeof(utf8) - 1, text, 6, &count) && count == 12);
    CHECK(!mm_caption_edit_decode("a\xe2\x82", 3, text, 8, &count) && count == 12);
    CHECK(mm_caption_edit_decode(utf8, sizeof(utf8) - 1, text, 8, &count) && count == 6);
    CHECK(!mm_caption_edit_encode(NULL, text, count, output, 11, &bytes) && bytes == 42);
    CHECK(mm_caption_edit_encode(NULL, text, count, output, 12, &bytes) == MM_TEXT_WINDOWS && bytes == 6);
    CHECK(!memcmp(output, ansi, bytes));
    CHECK(mm_caption_edit_decode(unsupported, sizeof(unsupported) - 1, text, 8, &count) && count == 3);
    CHECK(mm_caption_edit_encode(NULL, text, count, output, 6, &bytes) == MM_TEXT_UTF16_LE && bytes == 6);
    CHECK(!memcmp(output, wide, bytes));
    CHECK(mm_text_ansi_from_units(&host, text, 1, (char *)output, 2, 1) == 2);
    CHECK(mm_caption_edit_encode(&host, text, 1, output, 2, &bytes) == MM_TEXT_UTF16_LE && bytes == 2);
    CHECK(!memcmp(output, wide, bytes));
    text[0] = 0xe9;
    CHECK(mm_caption_edit_encode(&host, text, 1, output, 2, &bytes) == MM_TEXT_UTF16_LE && bytes == 2);
    CHECK(output[0] == 0xe9 && !output[1]);
    CHECK(mm_caption_edit_encode(NULL, NULL, 0, NULL, 0, &bytes) == MM_TEXT_UTF16_LE && !bytes);
}

static void caption_edit(void)
{
    static const unsigned short old_text[] = {'A', 'B', 'C'};
    static const unsigned short new_text[] = {'X', 'A', 'B', 'C'};
    static const unsigned short normalized[] = {'A', 13, 'B', 13, 'C', 13};
    unsigned short mixed[] = {'A', 13, 10, 'B', 10, 'C', 13};
    MMCaptionEditOptions options = {0}, fields = {0}, earlier;
    MMTextBoxState box = {0};
    MMCaptionEditFormat format;
    MMI32 ref;
    int diagnosed = 0;
    Host h;
    CHECK(mm_caption_edit_paragraph_end(mixed, 7, -1) == 3);
    CHECK(mm_caption_edit_paragraph_end(mixed, 7, 3) == 5);
    CHECK(mm_caption_edit_paragraph_end(mixed, 7, 7) == 8);
    CHECK(mm_caption_edit_normalize(mixed, 7, 13) == 6 &&
          !memcmp(mixed, normalized, sizeof(normalized)));

    options.width = 160;
    options.height = 40;
    options.first = 2;
    options.last = 4;
    options.scrolling = 1;
    CHECK(mm_caption_edit_write_box(&box, -3, 4, &options, NULL) == MM_CAPTION_EDIT_OK);
    CHECK(box.first == -2 && box.end == 1 && !box.story);
    CHECK(mm_caption_edit_read_box(&box, -3, 4, &fields) == MM_CAPTION_EDIT_OK);
    CHECK(fields.width == 160 && fields.height == 40 && fields.first == 2 && fields.last == 4 && fields.scrolling);
    earlier = options;
    options.x = 2147483647L;
    CHECK(mm_caption_edit_write_box(&box, -3, 4, &options, &earlier) == MM_CAPTION_EDIT_POSITION);
    CHECK(!box.left && box.right == 160);
    options.x = 0;
    box.first = -5;
    CHECK(mm_caption_edit_write_box(&box, -3, 4, &options, &earlier) == MM_CAPTION_EDIT_OK && box.first == -5);

    options.font_size = 18;
    options.bold = 1;
    options.italic = 1;
    options.alignment = 2;
    CHECK(mm_caption_edit_format(&options, NULL, &format) == MM_CAPTION_EDIT_OK && format.count == 3);
    CHECK(format.changes[0].kind == MM_CAPTION_STYLE && format.changes[0].mask == 3 &&
          format.changes[0].value == 3 && format.changes[2].value == 0x02000000UL);
    earlier = options;
    options.bold = 0;
    CHECK(mm_caption_edit_format(&options, &earlier, &format) == MM_CAPTION_EDIT_OK && format.count == 1);
    CHECK(format.changes[0].mask == 1 && !format.changes[0].value && !format.defaults);

    init(&h);
    ref = ensure(&h, 2, "font", 4);
    h.entries[0].key = key(1, 0);
    h.entries[0].value = 1;
    h.entries[1].key = key(1, 2);
    h.entries[1].value = 2;
    h.entries[2].key = key(2, 0);
    h.entries[2].value = ref;
    h.count = 3;
    CHECK(mm_caption_edit_replace(old_text, 3, new_text, 4, &h.s, &diagnosed) == MM_CAPTION_EDIT_OK);
    CHECK(h.count == 3 && h.entries[0].key == key(1, 0) && h.entries[0].value == 1 &&
          h.entries[1].key == key(1, 3) && h.entries[1].value == 2 &&
          h.entries[2].key == key(2, 0) && h.entries[2].value == ref);
    CHECK(!h.slots[ref - 1].free && h.slots[ref - 1].fixed == ((2UL << 24) | 1));
}

typedef struct CaptionFlow
{
    Host properties;
    MMCaptionEditWords selected;
    char steps[16], fail;
    int calls, diagnosed, rewrite;
} CaptionFlow;

static int caption_step(CaptionFlow *h, char step)
{
    CHECK(h->calls < 15);
    h->steps[h->calls++] = step;
    h->steps[h->calls] = 0;
    return h->fail != step;
}

static int caption_read(void *p, MMCaptionEditWords *out)
{
    static const unsigned short old[] = {'B'};
    out->text = old;
    out->count = 1;
    return caption_step(p, 'r');
}

static void caption_select(void *p, const MMCaptionEditWords *words)
{
    CaptionFlow *h = p;
    if (words)
        h->selected = *words;
    else
    {
        h->selected.text = NULL;
        h->selected.count = 0;
    }
    caption_step(h, words ? 's' : 'c');
}

static int caption_write(void *p, const MMCaptionEditWords *words)
{
    CaptionFlow *h = p;
    static const unsigned short normalized[] = {'A', 13, 'B'};
    CHECK(words->count == 3 && words->text == h->selected.text);
    CHECK(!memcmp(words->text, normalized, sizeof(normalized)));
    CHECK(h->properties.clears == (h->rewrite ? 2 : 1));
    return caption_step(h, 'w');
}

static void caption_flow(void)
{
    CaptionFlow h = {0};
    MMCaptionEditHost host = {0};
    MMCaptionEditInput before = {{0, 0, 90, 40, 1, 3, 0, 0, 18, 0, 0, 0, 1, 0, 0}, "Arial", "B"};
    MMCaptionEditInput value = before;
    MMCaptionEditPlan plan;
    MMTextBoxState box = {0};
    MMRichTextDefaults defaults = {0};
    int changed = -1;

    CHECK(mm_caption_edit_plan(&value, &before, 20, &plan) == MM_CAPTION_EDIT_NO_CHANGE);
    value.text = "";
    CHECK(mm_caption_edit_plan(&value, NULL, 20, &plan) == MM_CAPTION_EDIT_TEXT_LIMIT);
    CHECK(mm_caption_edit_plan(&value, NULL, 0, &plan) == MM_CAPTION_EDIT_OK && plan.text_changed);
    value = before;
    value.options.x = 20;
    CHECK(mm_caption_edit_plan(&value, &before, 20, &plan) == MM_CAPTION_EDIT_OK && !plan.text_changed);
    box.first = 1;
    box.end = 4;
    CHECK(mm_caption_edit_apply(&plan, 1, 3, &box, &defaults, &host, &changed) == MM_CAPTION_EDIT_OK);
    CHECK(!changed && box.left == 20 && box.right == 110);
    value.options.font_size = 22;
    CHECK(mm_caption_edit_apply(&plan, 1, 3, &box, &defaults, &host, &changed) == MM_CAPTION_EDIT_OK);
    CHECK(changed && defaults.height == 22);

    init(&h.properties);
    host.context = &h;
    host.read_text = caption_read;
    host.select_words = caption_select;
    host.properties = h.properties.s;
    host.diagnosed = &h.diagnosed;
    host.write_text = caption_write;
    value.text = "A\r\nB";
    value.options.bold = 1;
    value.options.x = 30;
    CHECK(mm_caption_edit_plan(&value, &before, 20, &plan) == MM_CAPTION_EDIT_OK && plan.text_changed);
    h.fail = 'w';
    changed = -1;
    CHECK(mm_caption_edit_apply(&plan, 1, 3, &box, &defaults, &host, &changed) == MM_CAPTION_EDIT_WRITE_TEXT);
    CHECK(!strcmp(h.steps, "rswc") && !h.selected.text && changed == -1 && box.left == 20);

    init(&h.properties);
    h.calls = 0;
    h.fail = 0;
    h.rewrite = 1;
    host.write_text = NULL;
    host.draft_text = caption_write;
    CHECK(mm_caption_edit_apply(&plan, 1, 3, &box, &defaults, &host, &changed) == MM_CAPTION_EDIT_OK);
    CHECK(!strcmp(h.steps, "rswc") && !h.selected.text && changed && box.left == 30);
    CHECK(h.properties.count == 1 && h.properties.entries[0].key == key(MM_CAPTION_STYLE, 0));
}

static int caption_clone_box(void *p, MMU32 original, MMU32 *number)
{
    CHECK(original == 5);
    *number = 11;
    return caption_step(p, 'c');
}
static int caption_read_box(void *p, MMU32 number, MMTextChunk *body)
{
    CHECK(number == 11);
    body->type = MM_TEXT_ROOT;
    body->number = 9;
    return caption_step(p, 'r');
}
static int caption_apply_box(void *p, int *changed)
{
    *changed = ((CaptionFlow *)p)->rewrite;
    return caption_step(p, 'e') ? MM_CAPTION_EDIT_OK : MM_CAPTION_EDIT_RANGE;
}
static int caption_write_body(void *p, MMTextChunk *body)
{
    body->type = MM_TEXT_ROOT;
    body->number = 25;
    return caption_step(p, 'b');
}
static int caption_link_body(void *p, MMU32 parent_type, MMU32 parent, MMU32 type, MMU32 number, MMU32 slot)
{
    CHECK(parent_type == MM_TEXT_BOX && parent == 11 && type == MM_TEXT_ROOT && number == 25 && slot == 3);
    return caption_step(p, 'a');
}
static void caption_unlink_body(void *p, MMU32 parent_type, MMU32 parent, MMU32 type, MMU32 number, MMU32 slot)
{
    CHECK(parent_type == MM_TEXT_BOX && parent == 11 && type == MM_TEXT_ROOT && number == 9 && slot == 3);
    caption_step(p, 'u');
}
static int caption_write_box(void *p, MMU32 number)
{
    CHECK(number == 11);
    return caption_step(p, 'w');
}

static void caption_archive_flow(void)
{
    CaptionFlow h = {0};
    MMMovieArchiveEditor editor = {0};
    MMTextBoxState box = {0};
    MMCaptionArchiveBox host = {NULL, NULL, caption_clone_box, caption_read_box, caption_apply_box,
                                caption_write_body, caption_write_box};
    const MMSceneArchiveText original = {0, 5};
    MMU32 number = 99;
    editor.context = &h;
    editor.adopt = caption_link_body;
    editor.unlink = caption_unlink_body;
    host.context = &h;
    host.box = &box;
    box.child = 3;
    h.rewrite = 1;
    h.fail = 'a';
    CHECK(mm_caption_edit_archive_box(&editor, &original, &host, &number) == MM_CAPTION_EDIT_WRITE_BODY);
    CHECK(number == 99 && !strcmp(h.steps, "creba"));
    h.calls = 0;
    h.fail = 0;
    CHECK(mm_caption_edit_archive_box(&editor, &original, &host, &number) == MM_CAPTION_EDIT_OK);
    CHECK(number == 11 && !strcmp(h.steps, "crebauw"));
    h.calls = 0;
    h.rewrite = 0;
    CHECK(mm_caption_edit_archive_box(&editor, &original, &host, &number) == MM_CAPTION_EDIT_OK);
    CHECK(number == 11 && !strcmp(h.steps, "crew"));
}

int main(void)
{
    arguments();
    lookup();
    apply();
    adjust();
    caption_encoding();
    caption_edit();
    caption_flow();
    caption_archive_flow();
    puts("Text property checks passed");
    return 0;
}
