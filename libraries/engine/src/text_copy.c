#include "mm/text_copy.h"
#include "mm/wide.h"
#include <string.h>
static MMU32 key(unsigned kind, MMI32 cp)
{
    return ((MMU32)kind << 24) | ((MMU32)cp & 0xffffffUL);
}
static MMI32 position(MMU32 key)
{
    return (MMI32)(key & 0xffffffUL);
}
static int import(MMI32 index, unsigned kind, int lock_first, MMI32 *out, const MMTextCopyServices *s)
{
    const void *bytes;
    MMI32 count;
    int r;
    if (lock_first)
    {
        bytes = s->lock_source(s->context, index);
        count = s->source_bytes(s->context, index);
    }
    else
    {
        count = s->source_bytes(s->context, index);
        bytes = s->lock_source(s->context, index);
    }
    r = mm_text_argument_ensure(kind, bytes, count, out, &s->target.edit.format.properties.arguments);
    s->unlock_source(s->context);
    return r;
}
static int apply(MMI32 at, MMI32 span, const MMTextPropertyChange *change, const MMTextCopyServices *s)
{
    if (s->target.validate_apply)
        s->target.validate_apply(s->target.context, at, span);
    return mm_text_properties_apply_changes(at, span, s->target.characters(s->target.context), change, 1,
                                            &s->target.edit.format.properties);
}
int mm_text_copy_properties(MMI32 from, MMI32 at, MMI32 source_span, MMI32 target_span, unsigned first, unsigned end,
                            const MMTextCopyServices *s)
{
    MMTextPropertyEntry entry;
    MMTextPropertyChange change;
    const MMTextPropertyServices *target = &s->target.edit.format.properties;
    MMI32 i, a, b, value;
    unsigned kind;
    int r;
    if (s->check_properties)
        s->check_properties(s->context, from, at, source_span, target_span);
    change.kind = 0;
    change.value = 0;
    change.mask = 0xffffffffUL;
    mm_text_property_find(key(first, 0), s->target.characters(s->target.context), 0, 0, &i, target);
    while (mm_text_property_fetch(i++, s->target.characters(s->target.context), &entry, 0, target))
    {
        kind = (unsigned)(entry.key >> 24);
        if (kind >= end)
            break;
        if (change.kind == kind)
            continue;
        change.kind = kind;
        r = s->target.edit.reserve(s->target.edit.context, 2);
        if (r < 0)
            return r;
        if (r)
        {
            r = apply(at, target_span, &change, s);
            if (r < 0)
                return r;
        }
        else
            s->warning(s->context, MM_TEXT_COPY_GROW);
    }
    mm_text_property_find(key(first, 0), s->source_characters(s->context), 0, 0, &i, &s->source);
    while (mm_text_property_fetch(i++, s->source_characters(s->context), &entry, &b, &s->source))
    {
        kind = (unsigned)(entry.key >> 24);
        if (kind >= end)
            break;
        a = position(entry.key);
        if (b <= from || a >= from + source_span)
            continue;
        if (b >= from + source_span)
            b = from + target_span;
        a += at - from;
        b += at - from;
        if (a < at)
            a = at;
        if (b > at + target_span)
            b = at + target_span;
        if (a >= b)
            continue;
        change.kind = kind;
        if (mm_text_property_has_argument(kind) && entry.value > 0)
        {
            r = import(entry.value - 1, kind, 0, &value, s);
            if (r < 0)
                return r;
            if (!r)
            {
                s->warning(s->context, MM_TEXT_COPY_ARGUMENT);
                continue;
            }
            change.value = (MMU32)value;
        }
        else
            change.value = (MMU32)entry.value;
        r = s->target.edit.reserve(s->target.edit.context, 2);
        if (r < 0)
            return r;
        if (r)
        {
            r = apply(a, b - a, &change, s);
            if (r < 0)
                return r;
        }
        else
        {
            s->warning(s->context, MM_TEXT_COPY_GROW);
            mm_text_property_ref(kind, mm_signed_word(change.value), 0, &target->arguments);
        }
    }
    return 1;
}
int mm_text_copy_document(MMI32 from, MMI32 count, MMI32 at, MMI32 deleted, MMU32 flags, const MMTextCopyServices *s)
{
    MMTextReplaceRequest q;
    MMTextPropertyEntry entry, fresh;
    const MMTextPropertyServices *target = &s->target.edit.format.properties;
    MMI32 protected_tail = 0, a, b, source_span, i, index, cp;
    unsigned kind;
    int r, t;
    if (s->target.characters(s->target.context) + count >= 0x00800000L)
    {
        s->target.too_much(s->target.context);
        return 0;
    }
    r = s->target.edit.undo(s->target.edit.context, at, at + deleted, count);
    if (r <= 0)
        return r;
    if (at + deleted == s->target.characters(s->target.context))
    {
        r = s->check_tail(s->context, from + count - 1, count, deleted);
        if (r <= 0)
            return r;
        --deleted;
        --count;
        protected_tail = 1;
    }
    if (count > 0 || deleted > 0)
    {
        memset(&q, 0, sizeof(q));
        q.kind = MM_TEXT_REPLACE_STREAM;
        q.stream = s->source_stream(s->context);
        q.source = from;
        q.inserted = count;
        q.at = at;
        q.deleted = deleted;
        r = s->target.replace_virtual(s->target.context, &q);
        if (r < 0)
            return r;
        if (!r)
        {
            t = s->target.edit.cancel(s->target.edit.context);
            return t < 0 ? t : 0;
        }
    }
    if (count > 0)
    {
        r = mm_text_copy_properties(from, at, count, count, 0, 128, s);
        if (r < 0)
            return r;
    }
    if (count + protected_tail > 0)
    {
        a = at;
        b = at + count;
        r = mm_text_paragraph_bounds(&a, &b, s->target.characters(s->target.context), 0, &s->target.edit.paragraphs);
        if (r <= 0)
            return r;
        source_span = count - a + at;
        if (source_span > 0 && a < b)
        {
            r = mm_text_copy_properties(from + a - at, a, source_span, b - a, 128, 192, s);
            if (r < 0)
                return r;
        }
    }
    if (count > 0)
    {
        mm_text_property_find(key(192, from), s->source_characters(s->context), 0, 0, &i, &s->source);
        while (i < s->source.count(s->source.context))
        {
            s->source.get(s->source.context, i++, &entry);
            cp = position(entry.key);
            if (cp < from || cp >= from + count)
                continue;
            kind = (unsigned)(entry.key >> 24);
            fresh.key = key(kind, at + cp - from);
            /* The native object pass imports every kind as an argument, even
             * client odd kinds classified inline elsewhere. */
            r = import(entry.value - 1, kind, 1, &fresh.value, s);
            if (r < 0)
                return r;
            if (!r)
                continue;
            if (mm_text_property_find(fresh.key, s->target.characters(s->target.context), 0, 0, &index, target))
                ++index;
            r = target->insert(target->context, index, &fresh);
            if (r < 0)
                return r;
            if (!r)
                mm_text_argument_ref(fresh.value, 0, &target->arguments);
        }
    }
    r = s->target.edit.commit(s->target.edit.context);
    if (r <= 0)
        return r;
    s->target.edit.validate(s->target.edit.context);
    return s->target.notify(s->target.context, at, count + protected_tail, deleted + protected_tail, flags);
}
static int exact(MMI32 cp, MMTextPropertyEntry *entry, MMI32 *index, const MMTextObjectServices *s)
{
    const MMTextPropertyServices *p = &s->target.edit.format.properties;
    return mm_text_property_find(key(192, cp), s->target.characters(s->target.context), entry, 0, index, p) &&
           position(entry->key) == cp;
}
int mm_text_object_fetch(MMI32 cp, MMI32 *at, void **owned, MMI32 *bytes, const MMTextObjectServices *s)
{
    const MMTextPropertyServices *p = &s->target.edit.format.properties;
    MMTextPropertyEntry entry;
    MMI32 index, n, ignored;
    void *group;
    const void *data;
    int r;
    r = mm_text_property_find(key(192, cp), s->target.characters(s->target.context), &entry, at, &index, p);
    if (r && cp == position(entry.key))
        *at = cp;
    if (s->publish_at)
        s->publish_at(s->context, *at);
    if (!(r && cp == position(entry.key)))
    {
        if (*at >= s->target.characters(s->target.context))
            return 0;
        if (r)
            ++index;
        p->get(p->context, index, &entry);
    }
    if (bytes)
    {
        *bytes = s->bytes(s->context, entry.value - 1);
        if (s->publish_bytes)
            s->publish_bytes(s->context, *bytes);
    }
    if (owned)
    {
        if (s->check_reference)
            s->check_reference(s->context, entry.value - 1);
        n = s->bytes(s->context, entry.value - 1);
        r = s->allocate(s->context, n, owned);
        if (r < 0)
            return r;
        if (r)
        {
            group = p->arguments.objects.get(p->arguments.objects.context, MM_TEXT_OWNER_ARGUMENTS);
            data = p->arguments.data(p->arguments.context, group, entry.value - 1, &ignored);
            memcpy(*owned, data, (size_t)n);
        }
    }
    return 1;
}
int mm_text_object_dispatch(MMI32 cp, int draw, const MMTextObjectServices *s)
{
    MMTextPropertyEntry entry;
    if (!exact(cp, &entry, 0, s))
        return 0;
    return s->dispatch(s->context, draw, entry.value - 1, (unsigned)(entry.key >> 24));
}
int mm_text_object_insert(const void *bytes, MMI32 length, MMI32 cp, MMI32 deleted, MMU32 flags,
                          const MMTextObjectServices *s)
{
    MMTextPropertyEntry entry;
    MMTextPropertyChange changes[4];
    MMTextFormatConfig config;
    MMTextCharacter value, old;
    MMTextReplaceRequest q;
    unsigned short marker = 1;
    unsigned char narrow_marker = 1;
    const MMTextPropertyServices *p = &s->target.edit.format.properties;
    MMI32 reference = 0, count = 0, index;
    int r, t, has_old;
    s->target.edit.bump(s->target.edit.context);
    r = s->target.edit.undo(s->target.edit.context, cp, cp + deleted, 1);
    if (r <= 0)
        return r;
    r = mm_text_argument_ensure(192, bytes, length, &reference, &p->arguments);
    if (r < 0)
        return r;
    if (!r)
        goto fail;
    if (s->target.has_character)
    {
        s->target.edit.read_input(s->target.edit.context, 0, &config, &value, &old, &has_old);
        r = mm_text_format_changes(0, &config, &value, 0, changes, &count, &s->target.edit.format);
        if (r < 0)
            return r;
        if (!r)
            goto fail;
    }
    r = s->target.edit.reserve(s->target.edit.context, 2 * count + 1);
    if (r < 0)
        return r;
    if (!r)
        goto fail;
    memset(&q, 0, sizeof(q));
    q.kind = MM_TEXT_REPLACE_BYTES;
    q.at = cp;
    q.inserted = 1;
    q.deleted = deleted;
    q.data = s->unit == 1 ? (const void *)&narrow_marker : (const void *)&marker;
    r = s->target.replace_virtual(s->target.context, &q);
    if (r < 0)
        return r;
    if (!r)
    {
        for (index = 0; index < count; ++index)
            mm_text_property_ref(changes[index].kind, mm_signed_word(changes[index].value), 0, &p->arguments);
        goto fail;
    }
    if (count > 0)
    {
        if (s->target.validate_apply)
            s->target.validate_apply(s->target.context, cp, 1);
        r = mm_text_properties_apply_changes(cp, 1, s->target.characters(s->target.context), changes, count, p);
        if (r < 0)
            return r;
    }
    entry.key = key(192, cp);
    entry.value = reference;
    if (mm_text_property_find(entry.key, s->target.characters(s->target.context), 0, 0, &index, p))
        ++index;
    r = p->insert(p->context, index, &entry);
    if (r < 0)
        return r;
    if (!r)
        s->diagnostic(s->context, MM_TEXT_OBJECT_RESERVED_INSERT);
    r = s->target.edit.commit(s->target.edit.context);
    if (r <= 0)
        return r;
    s->target.edit.bump(s->target.edit.context);
    s->target.edit.validate(s->target.edit.context);
    return s->target.notify(s->target.context, cp, 1, deleted, flags);
fail:
    mm_text_property_ref(192, reference, 0, &p->arguments);
    t = s->target.edit.cancel(s->target.edit.context);
    return t < 0 ? t : 0;
}
int mm_text_object_apply(const void *bytes, MMI32 length, MMI32 cp, MMU32 flags, const MMTextObjectServices *s)
{
    MMTextPropertyEntry entry;
    MMI32 index, old;
    int r, t;
    const MMTextPropertyServices *p = &s->target.edit.format.properties;
    if (!exact(cp, &entry, &index, s))
    {
        s->diagnostic(s->context, MM_TEXT_OBJECT_NOT_AT);
        return 0;
    }
    s->target.edit.bump(s->target.edit.context);
    r = s->target.edit.undo(s->target.edit.context, cp, cp + 1, 1);
    if (r <= 0)
        return r;
    old = entry.value;
    r = mm_text_argument_ensure(192, bytes, length, &entry.value, &p->arguments);
    if (r < 0)
        return r;
    if (!r)
    {
        t = s->target.edit.cancel(s->target.edit.context);
        return t < 0 ? t : 0;
    }
    p->put(p->context, index, &entry);
    mm_text_property_ref(192, old, 0, &p->arguments);
    r = s->target.edit.commit(s->target.edit.context);
    if (r <= 0)
        return r;
    s->target.edit.bump(s->target.edit.context);
    s->target.edit.validate(s->target.edit.context);
    return s->target.notify(s->target.context, cp, 1, 1, flags);
}
