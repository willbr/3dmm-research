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
int mm_text_property_find(MMU32 key, MMI32 characters, MMTextPropertyEntry *out, MMI32 *end, MMI32 *index,
                          const MMTextPropertyServices *s)
{
    MMI32 lo = 0, hi = s->count(s->context), i;
    MMTextPropertyEntry entry;
    int found = 0;
    while (lo < hi)
    {
        i = (lo + hi) / 2;
        s->get(s->context, i, &entry);
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
    if (found || (lo > 0 && (s->get(s->context, lo - 1, &entry), kind(entry.key) == kind(key))))
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
        if (lo < s->count(s->context))
        {
            s->get(s->context, lo, &entry);
            if (kind(entry.key) == kind(key))
                *end = position(entry.key);
        }
    }
    return found;
}
int mm_text_property_fetch(MMI32 index, MMI32 characters, MMTextPropertyEntry *out, MMI32 *end,
                           const MMTextPropertyServices *s)
{
    MMTextPropertyEntry entry, next;
    if (index < 0 || index >= s->count(s->context))
        return 0;
    s->get(s->context, index, &entry);
    if (out)
        *out = entry;
    if (end)
    {
        *end = characters;
        if (index + 1 < s->count(s->context))
        {
            s->get(s->context, index + 1, &next);
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
    void *group = s->objects.get(s->objects.context, MM_TEXT_OWNER_ARGUMENTS);
    const void *data;
    MMI32 i, length;
    MMU32 fixed;
    int result;
    if (!group)
    {
        result = s->create(s->context, n, &group);
        if (group)
            s->objects.set(s->objects.context, MM_TEXT_OWNER_ARGUMENTS, group);
        if (result <= 0)
            return result;
    }
    for (i = s->count(s->context, group); i-- > 0;)
    {
        if (s->free_slot(s->context, group, i))
            continue;
        data = s->data(s->context, group, i, &length);
        if (length != n || memcmp(data, bytes, (size_t)n))
            continue;
        fixed = s->fixed(s->context, group, i);
        if (kind(fixed) != type)
            continue;
        *out = i + 1;
        if (!(fixed & 0xffffffUL) || (fixed & 0xffffffUL) >= 0xffffffUL)
            s->diagnostic(s->context, MM_TEXT_ARG_REFS);
        ++fixed;
        if (kind(fixed) != type)
            s->diagnostic(s->context, MM_TEXT_ARG_OVERFLOW);
        s->put_fixed(s->context, group, i, fixed);
        return 1;
    }
    result = s->add(s->context, group, bytes, n, ((MMU32)type << 24) | 1, &i);
    if (result > 0)
        *out = i + 1;
    return result;
}
void mm_text_argument_ref(MMI32 ref, int add, const MMTextArgumentServices *s)
{
    void *group = s->objects.get(s->objects.context, MM_TEXT_OWNER_ARGUMENTS);
    MMU32 fixed, refs;
    if (!group || ref < 1 || ref > s->count(s->context, group) || s->free_slot(s->context, group, ref - 1))
    {
        s->diagnostic(s->context, MM_TEXT_ARG_INDEX);
        return;
    }
    fixed = s->fixed(s->context, group, ref - 1);
    refs = fixed & 0xffffffUL;
    if (add)
    {
        if (!refs || refs >= 0xffffffUL)
            s->diagnostic(s->context, MM_TEXT_ARG_REFS);
        s->put_fixed(s->context, group, ref - 1, fixed + 1);
    }
    else if (refs <= 1)
        s->remove(s->context, group, ref - 1);
    else
        s->put_fixed(s->context, group, ref - 1, fixed - 1);
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
    MMTextPropertyEntry entry;
    MMI32 i = 0, first, at, end;
    unsigned type;
    int before, keep, result;
    s->clear_cache(s->context);
    while (i < s->count(s->context))
    {
        s->get(s->context, i, &entry);
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
            s->put(s->context, i++, &entry);
            continue;
        }
        if (type >= 192)
        {
            mm_text_property_ref(type, entry.value, 0, &s->arguments);
            s->remove(s->context, i);
            continue;
        }
        before = at == cp;
        first = i;
        while (at <= cp + del)
        {
            if (++i >= s->count(s->context))
                break;
            s->get(s->context, i, &entry);
            if (kind(entry.key) != type)
                break;
            at = position(entry.key);
        }
        keep = 1;
        if (type < 128 || before)
        {
            s->get(s->context, --i, &entry);
            entry.key = key_at(type, type < 128 ? cp + ins : cp);
            s->put(s->context, i, &entry);
        }
        else
        {
            result = s->paragraph_end(s->context, cp + ins - 1, &end);
            if (result <= 0)
                return result;
            if (end < characters &&
                (i >= s->count(s->context) || (s->get(s->context, i, &entry), kind(entry.key) != type) ||
                 position(entry.key) + ins - del > end))
            {
                s->get(s->context, --i, &entry);
                entry.key = key_at(type, end);
                s->put(s->context, i, &entry);
            }
            else
                keep = 0;
        }
        while (first < i)
        {
            s->get(s->context, --i, &entry);
            mm_text_property_ref(type, entry.value, 0, &s->arguments);
            s->remove(s->context, i);
        }
        if (keep)
            ++i;
    }
    return 1;
}
static int insert(MMI32 *index, const MMTextPropertyEntry *entry, const MMTextPropertyServices *s)
{
    int result = s->insert(s->context, (*index)++, entry);
    if (!result)
        s->diagnostic(s->context, MM_TEXT_PROP_INSERT);
    return result < 0 ? result : 1;
}
static int apply(MMI32 cp, MMI32 span, MMI32 characters, const MMTextPropertyChange *changes,
                 MMI32 count, const MMTextPropertyServices *s)
{
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
                    s->get(s->context, i - 1, &previous);
                    if (kind(previous.key) == change.kind)
                        last = (MMU32)previous.value;
                }
            }
        }
        mm_text_property_ref(change.kind, mm_signed_word(revert), 1, &s->arguments);
        for (;;)
        {
            if (i >= s->count(s->context))
            {
                end = characters;
                break;
            }
            s->get(s->context, i, &current);
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
                    s->remove(s->context, i);
                }
                break;
            }
            mm_text_property_ref(change.kind, mm_signed_word(revert), 0, &s->arguments);
            revert = (MMU32)current.value;
            current.value = mm_signed_word(change.value | ((MMU32)current.value & ~change.mask));
            if (last == (MMU32)current.value)
                s->remove(s->context, i);
            else
            {
                mm_text_property_ref(change.kind, current.value, 1, &s->arguments);
                s->put(s->context, i++, &current);
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
