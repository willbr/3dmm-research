#include "mm/text_replace.h"
#include "mm/wide.h"
static void drop(MMTextLocation *p, const MMTextReplaceBaseServices *s)
{
    if (p->file)
        s->objects.release(s->objects.context, MM_TEXT_OWNER_FILE, p->file);
    p->file = 0;
}
int mm_text_replace_base(unsigned unit, const MMTextReplaceRequest *input, const MMTextReplaceBaseServices *s)
{
    MMTextReplaceRequest q = *input;
    void *stream;
    int r;
    if (q.kind == MM_TEXT_REPLACE_LOCATION)
    {
        s->objects.retain(s->objects.context, MM_TEXT_OWNER_FILE, q.location.file);
        r = s->translate(s->context, &q.location, q.system);
        if (r <= 0)
            goto done;
        q.inserted = q.location.bytes / (MMI32)unit;
    }
    stream = s->objects.get(s->objects.context, MM_TEXT_OWNER_STREAM);
    if (q.kind == MM_TEXT_REPLACE_LOCATION)
        r = s->location(s->context, stream, &q.location, q.copy && q.location.file == input->location.file, q.at * unit,
                        q.deleted * unit);
    else if (q.kind == MM_TEXT_REPLACE_STREAM)
        r = s->stream(s->context, stream, q.stream, q.source * unit, q.inserted * unit, q.at * unit, q.deleted * unit);
    else
        r = s->bytes(s->context, stream, q.data, q.inserted * unit, q.at * unit, q.deleted * unit);
    if (r <= 0)
        goto done;
    r = s->invalidate(s->context, q.at, q.inserted, q.deleted);
    if (r <= 0)
        goto done;
    s->validate(s->context);
    r = s->notify(s->context, q.at, q.inserted, q.deleted, q.flags);
done:
    if (q.kind == MM_TEXT_REPLACE_LOCATION)
        drop(&q.location, s);
    return r;
}
int mm_text_replace_rich(unsigned unit, int system, const MMTextReplaceRequest *input,
                         const MMTextReplaceRichServices *s)
{
    MMTextReplaceRequest q = *input;
    int r, t;
    if (q.kind == MM_TEXT_REPLACE_LOCATION)
    {
        s->base.objects.retain(s->base.objects.context, MM_TEXT_OWNER_FILE, q.location.file);
        r = s->base.translate(s->base.context, &q.location, q.system);
        if (r <= 0)
            goto done;
        q.inserted = q.location.bytes / (MMI32)unit;
    }
    if (s->characters(s->context) + q.inserted >= 0x00800000L)
    {
        s->too_much(s->context);
        r = 0;
        goto done;
    }
    r = s->edit.undo(s->edit.context, q.at, q.at + q.deleted, q.inserted);
    if (r <= 0)
        goto done;
    q.system = system;
    q.flags = 0;
    r = s->replace_base(s->context, &q);
    if (!r)
    {
        t = s->edit.cancel(s->edit.context);
        if (t < 0)
            r = t;
    }
    if (r <= 0)
        goto done;
    if (q.kind == MM_TEXT_REPLACE_LOCATION)
        drop(&q.location, &s->base);
    r = s->adjust(s->context, q.at, q.inserted, q.deleted);
    if (r <= 0)
        goto done;
    r = s->edit.commit(s->edit.context);
    if (r <= 0)
        goto done;
    s->edit.validate(s->edit.context);
    r = s->notify(s->context, q.at, q.inserted, q.deleted, input->flags);
done:
    if (q.kind == MM_TEXT_REPLACE_LOCATION)
        drop(&q.location, &s->base);
    return r;
}
static void release(MMTextPropertyChange *changes, MMI32 count, const MMTextFormatServices *s)
{
    MMI32 i;
    for (i = 0; i < count; ++i)
        mm_text_property_ref(changes[i].kind, mm_signed_word(changes[i].value), 0, &s->properties.arguments);
}
int mm_text_replace_formatted(unsigned unit, int system, const MMTextReplaceRequest *input,
                              const MMTextReplaceRichServices *s)
{
    MMTextReplaceRequest q = *input;
    MMTextPropertyChange changes[7];
    MMTextFormatConfig config;
    MMTextCharacter character, old_character;
    MMTextParagraph paragraph, old_paragraph;
    MMI32 character_count = 0, paragraph_count = 0, first = q.at, end = q.at + q.deleted, a, b;
    int r, t, has_old;
    if (s->characters(s->context) + q.inserted >= 0x00800000L)
    {
        s->too_much(s->context);
        return 0;
    }
    if (s->has_character && q.inserted > 0)
    {
        s->edit.read_input(s->edit.context, 0, &config, &character, &old_character, &has_old);
        r = mm_text_format_changes(0, &config, &character, 0, changes, &character_count, &s->edit.format);
        if (r <= 0)
            return r;
    }
    if (s->has_paragraph && q.inserted > 0)
    {
        r = mm_text_paragraph_bounds(&first, &end, s->characters(s->context), 0, &s->edit.paragraphs);
        if (r < 0)
            return r;
        if (!r)
            goto fail;
        if (first < end)
        {
            s->edit.read_input(s->edit.context, 1, &config, &paragraph, &old_paragraph, &has_old);
            r = mm_text_format_changes(1, &config, &paragraph, 0, changes + character_count, &paragraph_count,
                                       &s->edit.format);
            if (r < 0)
                return r;
            if (!r)
                goto fail;
        }
        if (q.at < first)
            first = q.at;
        if (q.at + q.deleted > end)
            end = q.at + q.deleted;
    }
    r = s->edit.reserve(s->edit.context, 2 * (character_count + paragraph_count));
    if (r < 0)
        return r;
    if (!r)
        goto fail;
    r = s->edit.undo(s->edit.context, first, end, end - first - q.deleted + q.inserted);
    if (r < 0)
        return r;
    if (!r)
        goto fail;
    if (q.kind == MM_TEXT_REPLACE_LOCATION)
    {
        if (q.source || q.inserted * (MMI32)unit != q.location.bytes)
            s->diagnostic(s->context, MM_TEXT_REPLACE_BAD_LOCATION);
    }
    else if (q.kind == MM_TEXT_REPLACE_BYTES && q.source)
        s->diagnostic(s->context, MM_TEXT_REPLACE_BAD_SOURCE);
    q.system = system;
    q.flags = 0;
    r = s->replace_virtual(s->context, &q);
    if (r < 0)
        return r;
    if (!r)
    {
        t = s->edit.cancel(s->edit.context);
        if (t < 0)
            return t;
        goto fail;
    }
    if (character_count > 0)
    {
        if (s->validate_apply)
            s->validate_apply(s->context, q.at, q.inserted);
        r = mm_text_properties_apply_changes(q.at, q.inserted, s->characters(s->context), changes, character_count,
                                             &s->edit.format.properties);
        if (r < 0)
            return r;
    }
    if (paragraph_count > 0)
    {
        a = q.at;
        b = q.at + q.inserted;
        r = mm_text_paragraph_bounds(&a, &b, s->characters(s->context), 0, &s->edit.paragraphs);
        if (r <= 0)
            return r;
        if (a < first)
            s->diagnostic(s->context, MM_TEXT_REPLACE_PARA_BEFORE);
        if (b > end - q.deleted + q.inserted)
            s->diagnostic(s->context, MM_TEXT_REPLACE_PARA_AFTER);
        if (s->validate_apply)
            s->validate_apply(s->context, a, b - a);
        r = mm_text_properties_apply_changes(a, b - a, s->characters(s->context), changes + character_count,
                                             paragraph_count, &s->edit.format.properties);
        if (r < 0)
            return r;
    }
    r = s->edit.commit(s->edit.context);
    if (r <= 0)
        return r;
    s->edit.validate(s->edit.context);
    return s->notify(s->context, first, end - first - q.deleted + q.inserted, end - first, input->flags);
fail:
    release(changes, character_count + paragraph_count, &s->edit.format);
    return 0;
}
int mm_text_replace_formatted_location(unsigned unit, int system, const MMTextReplaceRequest *input,
                                       const MMTextReplaceRichServices *s)
{
    MMTextReplaceRequest q = *input;
    int r;
    s->base.objects.retain(s->base.objects.context, MM_TEXT_OWNER_FILE, q.location.file);
    r = s->base.translate(s->base.context, &q.location, q.system);
    if (r > 0)
    {
        q.copy = q.copy && q.location.file == input->location.file;
        q.inserted = input->location.bytes;
        r = mm_text_replace_formatted(unit, system, &q, s);
    }
    drop(&q.location, &s->base);
    return r;
}
