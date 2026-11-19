#include "mm/caption_edit.h"
#include "mm/wide.h"
#include <stdlib.h>

#define RGB 0x01000000UL
#define CLEAR 0xffffffffUL
#define END_OF_SCENE 2147483647L

static int checked_word(double value, MMI32 *out)
{
    /* The sums and differences of 32-bit fields fit exactly here. */
    if (value < -2147483648.0 || value > 2147483647.0)
        return 0;
    *out = (MMI32)value;
    return 1;
}

int mm_caption_edit_read_box(const MMTextBoxState *box, MMI32 scene_first, MMI32 scene_last,
                             MMCaptionEditOptions *out)
{
    MMI32 width, height, first, last;
    int through_end = box->end == END_OF_SCENE;
    if (!checked_word((double)box->right - box->left, &width) ||
        !checked_word((double)box->bottom - box->top, &height) ||
        !checked_word((double)box->first - scene_first + 1, &first) ||
        !checked_word(through_end ? (double)scene_last - scene_first + 1 : (double)box->end - scene_first,
                      &last))
        return MM_CAPTION_EDIT_POSITION;
    out->x = box->left;
    out->y = box->top;
    out->width = width;
    out->height = height;
    out->first = first;
    out->last = last;
    out->through_end = through_end;
    out->scrolling = !box->story;
    return MM_CAPTION_EDIT_OK;
}

int mm_caption_edit_write_box(MMTextBoxState *box, MMI32 scene_first, MMI32 scene_last,
                              const MMCaptionEditOptions *value, const MMCaptionEditOptions *before)
{
    MMTextBoxState updated;
    double first, last;
    if (value->width < 1 || value->width > 4096 || value->height < 1 || value->height > 4096)
        return MM_CAPTION_EDIT_SIZE;
    updated = *box;
    if (!checked_word((double)value->x + value->width, &updated.right) ||
        !checked_word((double)value->y + value->height, &updated.bottom))
        return MM_CAPTION_EDIT_POSITION;
    updated.left = value->x;
    updated.top = value->y;
    updated.story = !value->scrolling;
    if (!before || value->first != before->first || value->last != before->last ||
        value->through_end != before->through_end)
    {
        first = (double)scene_first + value->first - 1;
        last = (double)scene_first + value->last - 1;
        if (value->first < 1 || first > scene_last ||
            (!value->through_end && (value->last < value->first || last > scene_last)))
            return MM_CAPTION_EDIT_RANGE;
        if (!checked_word(first, &updated.first) ||
            !checked_word(value->through_end ? END_OF_SCENE : last + 1, &updated.end))
            return MM_CAPTION_EDIT_POSITION;
    }
    *box = updated;
    return MM_CAPTION_EDIT_OK;
}

static void append_change(MMCaptionEditFormat *format, unsigned kind, MMU32 value, MMU32 mask)
{
    MMTextPropertyChange *change = &format->changes[format->count++];
    change->kind = kind;
    change->value = value;
    change->mask = mask;
}

int mm_caption_edit_format(const MMCaptionEditOptions *value, const MMCaptionEditOptions *before,
                           MMCaptionEditFormat *out)
{
    MMCaptionEditFormat format = {0};
    MMU32 mask = 0;
    if (!before || value->font_size != before->font_size)
    {
        if (value->font_size < 4 || value->font_size > 255)
            return MM_CAPTION_EDIT_FONT_SIZE;
        format.defaults |= MM_CAPTION_EDIT_SET_SIZE;
    }
    if (!before || value->transparent != before->transparent || value->paper != before->paper)
    {
        format.defaults |= MM_CAPTION_EDIT_SET_PAPER;
        format.paper = mm_signed_word((MMU32)(value->transparent ? CLEAR : RGB | (value->paper & 0xffffffUL)));
    }
    if (!before || value->width != before->width)
        format.defaults |= MM_CAPTION_EDIT_SET_WIDTH;
    if (!before || value->bold != before->bold)
        mask |= 1;
    if (!before || value->italic != before->italic)
        mask |= 2;
    if (mask)
        append_change(&format, MM_CAPTION_STYLE, ((value->bold ? 1U : 0U) | (value->italic ? 2U : 0U)) & mask, mask);
    if (!before || value->ink != before->ink)
        append_change(&format, MM_CAPTION_FOREGROUND, (MMU32)(value->ink & 0xffffffUL), CLEAR);
    if (!before || value->alignment != before->alignment)
    {
        if (value->alignment < 0 || value->alignment > 2)
            return MM_CAPTION_EDIT_ALIGNMENT;
        append_change(&format, MM_CAPTION_ALIGNMENT, (MMU32)value->alignment << 24, 0xff000000UL);
    }
    *out = format;
    return MM_CAPTION_EDIT_OK;
}

size_t mm_caption_edit_normalize(unsigned short *text, size_t count, unsigned short separator)
{
    size_t read, written = 0;
    unsigned short unit;
    for (read = 0; read < count; ++read)
    {
        unit = text[read];
        if (unit == 13 && read + 1 < count && text[read + 1] == 10)
            ++read;
        text[written++] = unit == 13 || unit == 10 ? separator : unit;
    }
    return written;
}

MMI32 mm_caption_edit_paragraph_end(const unsigned short *text, MMI32 count, MMI32 start)
{
    MMI32 i;
    for (i = start < 0 ? 0 : start; i < count; ++i)
    {
        if (text[i] == 13)
            return i + 1 + (i + 1 < count && text[i + 1] == 10);
        if (text[i] == 10)
            return i + 1;
    }
    return count + 1;
}

static MMI32 inherit_first(const MMTextPropertyServices *s, MMTextPropertyChange *changes)
{
    MMI32 row, count = 0, rows = s->count(s->context);
    MMTextPropertyEntry entry;
    unsigned kind;
    for (row = 0; row < rows; ++row)
    {
        s->get(s->context, row, &entry);
        kind = entry.key >> 24;
        if (kind >= MM_CAPTION_ALIGNMENT)
            break;
        if ((entry.key & 0xffffffUL) || !entry.value)
            continue;
        if (changes)
        {
            changes[count].kind = kind;
            changes[count].value = (MMU32)entry.value;
            changes[count].mask = CLEAR;
            mm_text_property_ref(kind, entry.value, 1, &s->arguments);
        }
        ++count;
    }
    return count;
}

int mm_caption_edit_replace(const unsigned short *old_text, MMI32 old_count,
                            const unsigned short *new_text, MMI32 new_count,
                            const MMTextPropertyServices *s, const int *diagnosed)
{
    MMTextPropertyChange *changes = NULL;
    MMI32 at = 0, old_end = old_count, new_end = new_count, inserted, count = 0;
    int result = MM_CAPTION_EDIT_OK;
    if (old_count < 0 || old_count >= 0x00800000L || new_count < 0 || new_count >= 0x00800000L)
        return MM_CAPTION_EDIT_ADJUST;
    while (at < old_end && at < new_end && old_text[at] == new_text[at])
        ++at;
    while (old_end > at && new_end > at && old_text[old_end - 1] == new_text[new_end - 1])
    {
        --old_end;
        --new_end;
    }
    inserted = new_end - at;
    if (!at && inserted)
        count = inherit_first(s, NULL);
    if (count)
    {
        changes = (MMTextPropertyChange *)calloc((size_t)count, sizeof(*changes));
        if (!changes)
            return MM_CAPTION_EDIT_MEMORY;
        count = inherit_first(s, changes);
    }
    if (mm_text_properties_adjust(at, inserted, old_end - at, new_count + 1, s) <= 0 || *diagnosed)
        result = MM_CAPTION_EDIT_ADJUST;
    else if (count && (mm_text_properties_apply_changes(0, inserted, new_count + 1, changes, count, s) <= 0 ||
                       *diagnosed))
        result = MM_CAPTION_EDIT_INHERIT;
    free(changes);
    return result;
}
