#include "mm/text_format.h"
#include "mm/string_wire.h"
#include "mm/wide.h"
#include <string.h>
static MMU32 get(const unsigned char *p, unsigned bytes, int big)
{
    MMU32 v = 0;
    unsigned i;
    for (i = 0; i < bytes; ++i)
        v |= (MMU32)p[big ? bytes - 1 - i : i] << (i * 8);
    return v;
}
static void put(unsigned char *p, MMU32 v, unsigned bytes, int big)
{
    unsigned i;
    for (i = 0; i < bytes; ++i)
        p[big ? bytes - 1 - i : i] = (unsigned char)(v >> (i * 8));
}
static int short_value(MMU32 v)
{
    v &= 65535U;
    return v < 32768U ? (int)v : -(int)(65536UL - v);
}
static MMU32 pack(int high, int low)
{
    return (((MMU32)high & 65535U) << 16) | ((MMU32)low & 65535U);
}
static int font_argument(const MMTextFormatConfig *c, MMI32 font, MMI32 *reference, const MMTextFormatServices *s)
{
    unsigned char record[MM_TEXT_FONT_RECORD], body[514];
    MMStringWire wire;
    MMMovieTitle name;
    MMU32 n;
    unsigned i, v;
    int r;
    mm_movie_title_init(&name, c->text.unit);
    r = s->font_name(s->context, font, &name);
    if (r <= 0)
        return r < 0 ? r : MM_TEXT_FORMAT_FAILED;
    if (name.count > 255 || name.unit != c->text.unit)
        return MM_TEXT_FORMAT_FAILED;
    memset(&wire, 0, sizeof(wire));
    wire.buffer = body;
    wire.unit = c->text.unit;
    wire.current_encoding = c->text.system;
    wire.native_big_endian = c->text.big;
    put(body, name.count, c->text.unit, c->text.big);
    for (i = 0; i < name.count; ++i)
    {
        v = name.unit == 1 ? ((const unsigned char *)name.text)[i] : name.text[i];
        put(body + (i + 1) * c->text.unit, v, c->text.unit, c->text.big);
    }
    put(body + (name.count + 1) * c->text.unit, 0, c->text.unit, c->text.big);
    put(record, (MMU32)font, 4, c->text.big);
    put(record + 4, (MMU32)c->text.system, 2, c->text.big);
    n = mm_string_wire_size(&wire);
    if (!mm_string_wire_get(&wire, record + 6, MM_TEXT_FONT_RECORD - 6))
        return MM_TEXT_FORMAT_FAILED;
    r = mm_text_argument_ensure(2, record, (MMI32)n + 6, reference, &s->properties.arguments);
    return r ? r : MM_TEXT_FORMAT_FAILED;
}
int mm_text_character_delta(unsigned kind, const MMTextFormatConfig *c, const MMTextCharacter *value,
                            const MMTextCharacter *old, MMU32 *word, MMU32 *mask, const MMTextFormatServices *s)
{
    MMU32 v = 0, m = 0xffffffffUL;
    MMI32 reference;
    int r;
    *mask = m;
    switch (kind)
    {
    case 1:
        v = pack((int)(value->height - c->height),
                 (int)(((MMU32)value->offset & 255U) << 8) | (int)(value->style & 255U));
        if (old)
            m = pack(old->height != value->height ? -1 : 0,
                     (old->offset != value->offset ? 0xff00 : 0) | (int)((old->style ^ value->style) & 255U));
        break;
    case 2:
        if (old && old->font == value->font)
            m = 0;
        else if (value->font != c->font)
        {
            r = font_argument(c, value->font, &reference, s);
            if (r <= 0)
            {
                if (r == MM_TEXT_FORMAT_FAILED)
                    *mask = 0;
                return r;
            }
            v = (MMU32)reference;
        }
        break;
    case 3:
        if (old && old->foreground == value->foreground)
            m = 0;
        else
            v = value->foreground - c->black;
        break;
    case 4:
        if (old && old->background == value->background)
            m = 0;
        else
            v = value->background - c->clear;
        break;
    default:
        m = 0;
        break;
    }
    *mask = m;
    if (m)
        *word = v & m;
    return m != 0;
}
int mm_text_paragraph_delta(unsigned kind, const MMTextFormatConfig *c, const MMTextParagraph *value,
                            const MMTextParagraph *old, MMU32 *word, MMU32 *mask)
{
    MMU32 v = 0, m = 0xffffffffUL;
    switch (kind)
    {
    case 128:
        if (old)
        {
            if (old->justification == value->justification)
                m &= 0x00ffffffUL;
            if (old->indent == value->indent)
                m &= 0xff00ffffUL;
            if (old->tab == value->tab)
                m &= 0xffff0000UL;
        }
        v = ((MMU32)value->justification << 24) | ((MMU32)value->indent << 16) |
            ((MMU32)(value->tab - c->tab) & 65535U);
        break;
    case 129:
        if (old)
        {
            if (old->line == value->line)
                m &= 0x0000ffffUL;
            if (old->extra_line == value->extra_line)
                m &= 0xffff0000UL;
        }
        v = pack(value->line - c->line, value->extra_line);
        break;
    case 130:
        if (old)
        {
            if (old->after == value->after)
                m &= 0x0000ffffUL;
            if (old->extra_after == value->extra_after)
                m &= 0xffff0000UL;
        }
        v = pack(value->after - c->after, value->extra_after);
        break;
    default:
        m = 0;
        break;
    }
    *mask = m;
    if (m)
        *word = v & m;
    return m != 0;
}
static void release(const MMTextPropertyChange *changes, MMI32 count, const MMTextFormatServices *s)
{
    MMI32 i;
    for (i = 0; i < count; ++i)
        mm_text_property_ref(changes[i].kind, mm_signed_word(changes[i].value), 0, &s->properties.arguments);
}
int mm_text_format_changes(int paragraph, const MMTextFormatConfig *c, const void *value, const void *old,
                           MMTextPropertyChange *changes, MMI32 *count, const MMTextFormatServices *s)
{
    MMTextPropertyChange change;
    unsigned kind, first = paragraph ? 128 : 1, end = paragraph ? 131 : 5;
    int r;
    *count = 0;
    for (kind = first; kind < end; ++kind)
    {
        if (paragraph)
            r = mm_text_paragraph_delta(kind, c, (const MMTextParagraph *)value, (const MMTextParagraph *)old,
                                        &change.value, &change.mask);
        else
            r = mm_text_character_delta(kind, c, (const MMTextCharacter *)value, (const MMTextCharacter *)old,
                                        &change.value, &change.mask, s);
        if (r < 0)
        {
            if (r == MM_TEXT_FORMAT_FAILED)
            {
                release(changes, *count, s);
                return 0;
            }
            return r;
        }
        if (r)
        {
            change.kind = kind;
            changes[(*count)++] = change;
        }
    }
    return 1;
}
int mm_text_fetch_character(const MMTextFormatConfig *c, MMTextCharacterCache *cache, MMI32 cp, MMI32 characters,
                            const MMTextFormatServices *s)
{
    MMTextPropertyEntry entry;
    MMI32 end, bytes;
    unsigned kind, offset;
    void *group;
    const unsigned char *data;
    int r;
    if (cp >= cache->first && cp < cache->end)
        return 1;
    memset(&cache->value, 0, sizeof(cache->value));
    cache->value.font = c->font;
    cache->value.height = c->height;
    cache->value.foreground = c->black;
    cache->value.background = c->clear;
    cache->first = 0;
    cache->end = characters;
    for (kind = 1; kind < 5; ++kind)
    {
        r = mm_text_property_find(((MMU32)kind << 24) | ((MMU32)cp & 0xffffffUL), characters, &entry, &end, 0,
                                  &s->properties);
        if (end < cache->end)
            cache->end = end;
        if (!r)
            continue;
        if ((MMI32)(entry.key & 0xffffffUL) > cache->first)
            cache->first = (MMI32)(entry.key & 0xffffffUL);
        switch (kind)
        {
        case 1:
            cache->value.style = (MMU32)entry.value & 255U;
            offset = ((MMU32)entry.value >> 8) & 255U;
            cache->value.offset = c->signed_offset && offset >= 128 ? (int)offset - 256 : (int)offset;
            cache->value.height += short_value((MMU32)entry.value >> 16);
            break;
        case 2:
            if (entry.value > 0)
            {
                const MMTextArgumentRows *rows = &s->properties.arguments.rows;
                group = s->properties.arguments.objects.get(s->properties.arguments.objects.context,
                                                            MM_TEXT_OWNER_ARGUMENTS);
                data = (const unsigned char *)rows->data(rows->context, group, entry.value - 1, &bytes);
                if (bytes < 6 || bytes > MM_TEXT_FONT_RECORD)
                {
                    s->bad_font_record(s->context);
                    break;
                }
                cache->value.font = mm_signed_word(get(data, 4, c->text.big));
                r = s->valid_font(s->context, cache->value.font);
                if (r <= 0)
                    return r;
            }
            break;
        case 3:
            if (entry.value)
                cache->value.foreground = c->black + (MMU32)entry.value;
            break;
        case 4:
            if (entry.value)
                cache->value.background = c->clear + (MMU32)entry.value;
            break;
        }
    }
    return 1;
}
void mm_text_fetch_paragraph(const MMTextFormatConfig *c, MMTextParagraphCache *cache, MMI32 cp, MMI32 characters,
                             const MMTextFormatServices *s)
{
    MMTextPropertyEntry entry;
    MMI32 end;
    unsigned kind;
    MMU32 word;
    int r;
    if (cp >= cache->first && cp < cache->end)
        return;
    memset(&cache->value, 0, sizeof(cache->value));
    cache->value.tab = c->tab;
    cache->value.line = c->line;
    cache->value.after = c->after;
    cache->first = 0;
    cache->end = characters;
    for (kind = 128; kind < 131; ++kind)
    {
        r = mm_text_property_find(((MMU32)kind << 24) | ((MMU32)cp & 0xffffffUL), characters, &entry, &end, 0,
                                  &s->properties);
        if (end < cache->end)
            cache->end = end;
        if (!r)
            continue;
        if ((MMI32)(entry.key & 0xffffffUL) > cache->first)
            cache->first = (MMI32)(entry.key & 0xffffffUL);
        word = (MMU32)entry.value;
        switch (kind)
        {
        case 128:
            cache->value.justification = word >> 24;
            cache->value.indent = (word >> 16) & 255U;
            cache->value.tab = short_value((MMU32)c->tab + (MMU32)short_value(word));
            break;
        case 129:
            cache->value.line = short_value((MMU32)c->line + (MMU32)short_value(word >> 16));
            cache->value.extra_line = short_value(word);
            break;
        case 130:
            cache->value.after = short_value((MMU32)c->after + (MMU32)short_value(word >> 16));
            cache->value.extra_after = short_value(word);
            break;
        }
    }
}
int mm_text_paragraph_bounds(MMI32 *first, MMI32 *end, MMI32 characters, int expand, const MMTextParagraphServices *s)
{
    int r;
    MMI32 value;
    if (expand)
    {
        r = s->query(s->context, MM_TEXT_PARA_END, *end - (*end > 0 && *end > *first), &value);
        if (r <= 0)
            return r;
        *end = value;
        r = s->query(s->context, MM_TEXT_PARA_START, *first, &value);
        if (r <= 0)
            return r;
        *first = value;
    }
    else
    {
        r = s->query(s->context, MM_TEXT_PARA_IS_START, *first, &value);
        if (r <= 0)
            return r;
        if (!value)
        {
            r = s->query(s->context, MM_TEXT_PARA_END, *first, &value);
            if (r <= 0)
                return r;
            *first = value;
        }
        r = s->query(s->context, MM_TEXT_PARA_IS_START, *end, &value);
        if (r <= 0)
            return r;
        if (!value)
        {
            r = s->query(s->context, MM_TEXT_PARA_NEXT, *end, &value);
            if (r <= 0)
                return r;
            *end = value;
            if (*end < characters)
            {
                r = s->query(s->context, MM_TEXT_PARA_START, *end, &value);
                if (r <= 0)
                    return r;
                *end = value;
            }
        }
    }
    return 1;
}
int mm_text_format_apply(int paragraph, const MMTextFormatConfig *c, MMI32 cp, MMI32 span, MMI32 characters, int expand,
                         MMU32 flags, const void *value, const void *old, const MMTextFormatEditServices *s)
{
    MMTextPropertyChange changes[4];
    MMTextFormatConfig input_config;
    MMTextCharacter input_character, old_character;
    MMTextParagraph input_paragraph, old_paragraph;
    void *input, *previous;
    MMI32 end = cp + span, count;
    int r, t, has_old = 0;
    if (paragraph)
    {
        r = mm_text_paragraph_bounds(&cp, &end, characters, expand, &s->paragraphs);
        if (r <= 0)
            return r;
        s->publish_range(s->context, cp, end);
        if (end <= cp)
            return 1;
    }
    s->bump(s->context);
    r = s->undo(s->context, cp, end, end - cp);
    if (r <= 0)
        return r;
    if (s->read_input)
    {
        input = paragraph ? (void *)&input_paragraph : (void *)&input_character;
        previous = paragraph ? (void *)&old_paragraph : (void *)&old_character;
        s->read_input(s->context, paragraph, &input_config, input, previous, &has_old);
        c = &input_config;
        value = input;
        old = has_old ? previous : 0;
    }
    r = mm_text_format_changes(paragraph, c, value, old, changes, &count, &s->format);
    if (r < 0)
        return r;
    if (!r)
    {
        t = s->cancel(s->context);
        return t < 0 ? t : 0;
    }
    r = s->reserve(s->context, 2 * count);
    if (r < 0)
        return r;
    if (!r)
    {
        release(changes, count, &s->format);
        t = s->cancel(s->context);
        if (t < 0)
            return t;
        s->validate(s->context);
        return 0;
    }
    if (s->characters)
        characters = s->characters(s->context);
    r = mm_text_properties_apply_changes(cp, end - cp, characters, changes, count, &s->format.properties);
    if (r < 0)
        return r;
    r = s->commit(s->context);
    if (r < 0)
        return r;
    s->bump(s->context);
    s->validate(s->context);
    return s->invalidate(s->context, cp, end - cp, flags);
}
