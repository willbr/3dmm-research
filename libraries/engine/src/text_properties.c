#include "mm/text_properties.h"
#include "mm/wide.h"
#include <string.h>
static unsigned kind(MMU32 key)
{
    return (unsigned)(key >> 24);
}
static MMI32 position(MMU32 key)
{
    return (MMI32)(key & 0xffffffUL);
}
static MMU32 key_at(unsigned type, MMI32 cp)
{
    return ((MMU32)type << 24) | ((MMU32)cp & 0xffffffUL);
}
static MMI32 live_count(void *context, void *owner)
{
    const MMTextLiveArgumentGroup *binding = context;
    return (MMI32)binding->view(owner).count;
}
static unsigned char *live_record(const MMGroup *rows, MMI32 index, MMI32 *bytes)
{
    MMGroupLocation row;
    if (bytes)
        *bytes = 0;
    if (!rows || rows->fixed_bytes != sizeof(MMU32) ||
        mm_group_read_slot(rows, (MMU32)index, &row) != MM_GROUP_SLOT_PRESENT)
        return NULL;
    if (bytes)
        *bytes = row.bytes - (MMI32)rows->fixed_bytes;
    return rows->storage->buffers[0].data + row.offset;
}
unsigned char *mm_text_argument_payload(const MMGroup *rows, MMI32 index, MMI32 *bytes)
{
    unsigned char *record = live_record(rows, index, bytes);
    return record ? record + sizeof(MMU32) : NULL;
}
static unsigned char *live_row(void *context, void *owner, MMI32 index)
{
    const MMTextLiveArgumentGroup *binding = context;
    MMGroup rows = binding->view(owner);
    return live_record(&rows, index, NULL);
}
static int live_free(void *context, void *owner, MMI32 index)
{
    return live_row(context, owner, index) == NULL;
}
static const void *live_data(void *context, void *owner, MMI32 index, MMI32 *bytes)
{
    const MMTextLiveArgumentGroup *binding = context;
    MMGroup rows = binding->view(owner);
    return mm_text_argument_payload(&rows, index, bytes);
}
static MMU32 live_fixed(void *context, void *owner, MMI32 index)
{
    const unsigned char *row = live_row(context, owner, index);
    MMU32 value = 0;
    if (row)
        memcpy(&value, row, sizeof(value));
    return value;
}
static void live_put_fixed(void *context, void *owner, MMI32 index, MMU32 value)
{
    unsigned char *row = live_row(context, owner, index);
    if (row)
        memcpy(row, &value, sizeof(value));
}
static int live_add(void *context, void *owner, const void *bytes, MMI32 count, MMU32 fixed, MMI32 *out)
{
    const MMTextLiveArgumentGroup *binding = context;
    MMGroup rows = binding->view(owner);
    MMU32 index;
    int result;
    if (rows.fixed_bytes != sizeof(fixed))
        return 0;
    result = mm_group_add(&rows, (MMU32)count, bytes, &fixed, &index);
    binding->publish(owner, &rows);
    if (result != MM_STORAGE_OK)
        return 0;
    *out = (MMI32)index;
    return 1;
}
static void live_remove(void *context, void *owner, MMI32 index)
{
    const MMTextLiveArgumentGroup *binding = context;
    MMGroup rows = binding->view(owner);
    if (mm_group_delete(&rows, (MMU32)index) != MM_STORAGE_OK && binding->bad_delete)
        binding->bad_delete(owner);
    binding->publish(owner, &rows);
}
void mm_text_live_argument_rows(MMTextArgumentRows *s, const MMTextLiveArgumentGroup *group)
{
    s->context = (void *)group;
    s->count = live_count;
    s->free_slot = live_free;
    s->data = live_data;
    s->fixed = live_fixed;
    s->put_fixed = live_put_fixed;
    s->add = live_add;
    s->remove = live_remove;
}
static MMI32 property_count(void *context)
{
    const MMTextPropertyArray *binding = context;
    int big;
    return (MMI32)binding->view(binding->owner, &big).count;
}
static void property_get(void *context, MMI32 index, MMTextPropertyEntry *entry)
{
    const MMTextPropertyArray *binding = context;
    int big;
    MMArray rows = binding->view(binding->owner, &big);
    MMU32 words[2] = {0, 0};
    mm_text_property_array_read(&rows, (MMU32)index, big, words);
    entry->key = words[0];
    entry->value = mm_signed_word(words[1]);
}
static int property_write(const MMTextPropertyArray *binding, MMI32 index, const MMTextPropertyEntry *entry,
                          int insert)
{
    int big, ok;
    MMArray rows = binding->view(binding->owner, &big);
    MMU32 words[2];
    words[0] = entry->key;
    words[1] = (MMU32)entry->value;
    ok = mm_text_property_array_write(&rows, (MMU32)index, big, words, insert);
    if (insert)
        binding->publish(binding->owner, &rows);
    return ok;
}
static void property_put(void *context, MMI32 index, const MMTextPropertyEntry *entry)
{
    const MMTextPropertyArray *binding = context;
    if (!property_write(binding, index, entry, 0) && binding->bad_mutation)
        binding->bad_mutation(binding->owner);
}
static int property_insert(void *context, MMI32 index, const MMTextPropertyEntry *entry)
{
    const MMTextPropertyArray *binding = context;
    return property_write(binding, index, entry, 1) ? 1 : binding->insert_failure;
}
static void property_remove(void *context, MMI32 index)
{
    const MMTextPropertyArray *binding = context;
    int big;
    MMArray rows = binding->view(binding->owner, &big);
    if (mm_array_delete(&rows, (MMU32)index, 1) != MM_STORAGE_OK && binding->bad_mutation)
        binding->bad_mutation(binding->owner);
    binding->publish(binding->owner, &rows);
}
int mm_text_property_array_reserve(const MMTextPropertyArray *binding, MMI32 count)
{
    int big;
    MMArray rows = binding->view(binding->owner, &big);
    int result = mm_array_reserve(&rows, (MMU32)count, 0);
    binding->publish(binding->owner, &rows);
    return result == MM_STORAGE_OK;
}
void mm_text_property_array_rows(MMTextPropertyRows *s, const MMTextPropertyArray *binding)
{
    s->context = (void *)binding;
    s->count = property_count;
    s->get = property_get;
    s->put = property_put;
    s->insert = property_insert;
    s->remove = property_remove;
}
int mm_text_property_find(MMU32 key, MMI32 characters, MMTextPropertyEntry *out, MMI32 *end, MMI32 *index,
                          const MMTextPropertyServices *s)
{
    const MMTextPropertyRows *rows = &s->rows;
    MMI32 lo = 0, hi = rows->count(rows->context), i;
    MMTextPropertyEntry entry;
    int found = 0;
    while (lo < hi)
    {
        i = (lo + hi) / 2;
        rows->get(rows->context, i, &entry);
        if (entry.key < key)
            lo = i + 1;
        else if (entry.key > key)
            hi = i;
        else
        {
            lo = i + 1;
            found = 1;
            break;
        }
    }
    if (index)
        *index = lo;
    if (found || (lo > 0 && (rows->get(rows->context, lo - 1, &entry), kind(entry.key) == kind(key))))
    {
        if (out)
            *out = entry;
        if (index)
            *index = lo - 1;
        found = 1;
    }
    if (end)
    {
        *end = characters;
        if (lo < rows->count(rows->context))
        {
            rows->get(rows->context, lo, &entry);
            if (kind(entry.key) == kind(key))
                *end = position(entry.key);
        }
    }
    return found;
}
int mm_text_property_fetch(MMI32 index, MMI32 characters, MMTextPropertyEntry *out, MMI32 *end,
                           const MMTextPropertyServices *s)
{
    const MMTextPropertyRows *rows = &s->rows;
    MMTextPropertyEntry entry, next;
    if (index < 0 || index >= rows->count(rows->context))
        return 0;
    rows->get(rows->context, index, &entry);
    if (out)
        *out = entry;
    if (end)
    {
        *end = characters;
        if (index + 1 < rows->count(rows->context))
        {
            rows->get(rows->context, index + 1, &next);
            if (kind(next.key) == kind(entry.key))
                *end = position(next.key);
        }
    }
    return 1;
}
int mm_text_property_has_argument(unsigned type)
{
    if (type < 64 || (type >= 128 && type < 160))
        return type == 2;
    return !(type & 1);
}
int mm_text_argument_ensure(unsigned type, const void *bytes, MMI32 n, MMI32 *out, const MMTextArgumentServices *s)
{
    const MMTextArgumentRows *rows = &s->rows;
    void *group = s->objects.get(s->objects.context, MM_TEXT_OWNER_ARGUMENTS);
    const void *data;
    MMI32 i, length;
    MMU32 fixed;
    int result;
    if (s->validate_input)
        s->validate_input(s->context, type, bytes, n);
    if (!group)
    {
        result = s->create(s->context, n, &group);
        if (group)
            s->objects.set(s->objects.context, MM_TEXT_OWNER_ARGUMENTS, group);
        if (result <= 0)
            return result;
    }
    for (i = rows->count(rows->context, group); i-- > 0;)
    {
        if (rows->free_slot(rows->context, group, i))
            continue;
        data = rows->data(rows->context, group, i, &length);
        if (length != n || memcmp(data, bytes, (size_t)n))
            continue;
        fixed = rows->fixed(rows->context, group, i);
        if (kind(fixed) != type)
            continue;
        *out = i + 1;
        if (!(fixed & 0xffffffUL) || (fixed & 0xffffffUL) >= 0xffffffUL)
            s->diagnostic(s->context, MM_TEXT_ARG_REFS);
        ++fixed;
        if (kind(fixed) != type)
            s->diagnostic(s->context, MM_TEXT_ARG_OVERFLOW);
        rows->put_fixed(rows->context, group, i, fixed);
        return 1;
    }
    result = rows->add(rows->context, group, bytes, n, ((MMU32)type << 24) | 1, &i);
    if (result > 0)
        *out = i + 1;
    return result;
}
void mm_text_argument_ref(MMI32 ref, int add, const MMTextArgumentServices *s)
{
    const MMTextArgumentRows *rows = &s->rows;
    void *group = s->objects.get(s->objects.context, MM_TEXT_OWNER_ARGUMENTS);
    MMU32 fixed, refs;
    if (!group || ref < 1 || ref > rows->count(rows->context, group) || rows->free_slot(rows->context, group, ref - 1))
    {
        s->diagnostic(s->context, MM_TEXT_ARG_INDEX);
        return;
    }
    fixed = rows->fixed(rows->context, group, ref - 1);
    refs = fixed & 0xffffffUL;
    if (add)
    {
        if (!refs || refs >= 0xffffffUL)
            s->diagnostic(s->context, MM_TEXT_ARG_REFS);
        rows->put_fixed(rows->context, group, ref - 1, fixed + 1);
    }
    else if (refs <= 1)
        rows->remove(rows->context, group, ref - 1);
    else
        rows->put_fixed(rows->context, group, ref - 1, fixed - 1);
}
void mm_text_property_ref(unsigned type, MMI32 value, int add, const MMTextArgumentServices *s)
{
    if (value > 0 && mm_text_property_has_argument(type))
        mm_text_argument_ref(value, add, s);
}
void mm_text_properties_release(MMI32 count, const MMTextPropertyServices *s)
{
    MMI32 i;
    MMTextPropertyChange change;
    for (i = 0; i < count; ++i)
    {
        s->change(s->context, i, &change);
        mm_text_property_ref(change.kind, mm_signed_word(change.value), 0, &s->arguments);
    }
}
int mm_text_properties_adjust(MMI32 cp, MMI32 ins, MMI32 del, MMI32 characters, const MMTextPropertyServices *s)
{
    const MMTextPropertyRows *rows = &s->rows;
    MMTextPropertyEntry entry;
    MMI32 i = 0, first, at, end;
    unsigned type;
    int before, keep, result;
    s->clear_cache(s->context);
    while (i < rows->count(rows->context))
    {
        rows->get(rows->context, i, &entry);
        at = position(entry.key);
        if (at < cp)
        {
            ++i;
            continue;
        }
        type = kind(entry.key);
        if (at >= cp + del + (type < 192))
        {
            entry.key = key_at(type, at + ins - del);
            rows->put(rows->context, i++, &entry);
            continue;
        }
        if (type >= 192)
        {
            mm_text_property_ref(type, entry.value, 0, &s->arguments);
            rows->remove(rows->context, i);
            continue;
        }
        before = at == cp;
        first = i;
        while (at <= cp + del)
        {
            if (++i >= rows->count(rows->context))
                break;
            rows->get(rows->context, i, &entry);
            if (kind(entry.key) != type)
                break;
            at = position(entry.key);
        }
        keep = 1;
        if (type < 128 || before)
        {
            rows->get(rows->context, --i, &entry);
            entry.key = key_at(type, type < 128 ? cp + ins : cp);
            rows->put(rows->context, i, &entry);
        }
        else
        {
            result = s->paragraph_end(s->context, cp + ins - 1, &end);
            if (result <= 0)
                return result;
            if (end < characters &&
                (i >= rows->count(rows->context) || (rows->get(rows->context, i, &entry), kind(entry.key) != type) ||
                 position(entry.key) + ins - del > end))
            {
                rows->get(rows->context, --i, &entry);
                entry.key = key_at(type, end);
                rows->put(rows->context, i, &entry);
            }
            else
                keep = 0;
        }
        while (first < i)
        {
            rows->get(rows->context, --i, &entry);
            mm_text_property_ref(type, entry.value, 0, &s->arguments);
            rows->remove(rows->context, i);
        }
        if (keep)
            ++i;
    }
    return 1;
}
static int insert(MMI32 *index, const MMTextPropertyEntry *entry, const MMTextPropertyServices *s)
{
    int result = s->rows.insert(s->rows.context, (*index)++, entry);
    if (!result)
        s->diagnostic(s->context, MM_TEXT_PROP_INSERT);
    return result < 0 ? result : 1;
}
static int apply(MMI32 cp, MMI32 span, MMI32 characters, const MMTextPropertyChange *changes,
                 MMI32 count, const MMTextPropertyServices *s)
{
    const MMTextPropertyRows *rows = &s->rows;
    MMTextPropertyEntry entry, current, previous;
    MMTextPropertyChange change;
    MMI32 n, i, end, limit = cp + span < characters ? cp + span : characters;
    MMU32 revert, last;
    int result;
    s->clear_cache(s->context);
    for (n = count; n-- > 0;)
    {
        if (changes)
            change = changes[n];
        else
            s->change(s->context, n, &change);
        entry.key = key_at(change.kind, cp);
        entry.value = mm_signed_word(change.value);
        if (!mm_text_property_find(entry.key, characters, &current, 0, &i, s))
        {
            revert = 0;
            if (entry.value)
            {
                mm_text_property_ref(change.kind, entry.value, 1, &s->arguments);
                result = insert(&i, &entry, s);
                if (result < 0)
                    return result;
            }
            last = (MMU32)entry.value;
        }
        else
        {
            revert = (MMU32)current.value;
            if (position(current.key) < cp)
            {
                ++i;
                if (entry.value != current.value)
                {
                    current.value = mm_signed_word(change.value | ((MMU32)current.value & ~change.mask));
                    current.key = entry.key;
                    mm_text_property_ref(change.kind, current.value, 1, &s->arguments);
                    result = insert(&i, &current, s);
                    if (result < 0)
                        return result;
                }
                last = (MMU32)current.value;
            }
            else
            {
                last = 0;
                if (i > 0)
                {
                    rows->get(rows->context, i - 1, &previous);
                    if (kind(previous.key) == change.kind)
                        last = (MMU32)previous.value;
                }
            }
        }
        mm_text_property_ref(change.kind, mm_signed_word(revert), 1, &s->arguments);
        for (;;)
        {
            if (i >= rows->count(rows->context))
            {
                end = characters;
                break;
            }
            rows->get(rows->context, i, &current);
            if (kind(current.key) != change.kind)
            {
                end = characters;
                break;
            }
            end = position(current.key);
            if (end >= limit)
            {
                if (end == limit && last == (MMU32)current.value)
                {
                    mm_text_property_ref(change.kind, current.value, 0, &s->arguments);
                    rows->remove(rows->context, i);
                }
                break;
            }
            mm_text_property_ref(change.kind, mm_signed_word(revert), 0, &s->arguments);
            revert = (MMU32)current.value;
            current.value = mm_signed_word(change.value | ((MMU32)current.value & ~change.mask));
            if (last == (MMU32)current.value)
                rows->remove(rows->context, i);
            else
            {
                mm_text_property_ref(change.kind, current.value, 1, &s->arguments);
                rows->put(rows->context, i++, &current);
                last = (MMU32)current.value;
            }
        }
        if (end > limit && revert != last)
        {
            current.key = key_at(change.kind, limit);
            current.value = mm_signed_word(revert);
            result = insert(&i, &current, s);
            if (result < 0)
                return result;
        }
        else
            mm_text_property_ref(change.kind, mm_signed_word(revert), 0, &s->arguments);
        mm_text_property_ref(change.kind, mm_signed_word(change.value), 0, &s->arguments);
    }
    return 1;
}
int mm_text_properties_apply(MMI32 cp, MMI32 span, MMI32 characters, MMI32 count, const MMTextPropertyServices *s)
{
    return apply(cp, span, characters, 0, count, s);
}
int mm_text_properties_apply_changes(MMI32 cp, MMI32 span, MMI32 characters, const MMTextPropertyChange *changes,
                                    MMI32 count, const MMTextPropertyServices *s)
{
    return apply(cp, span, characters, changes, count, s);
}
