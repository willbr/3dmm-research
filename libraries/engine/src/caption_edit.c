#include "mm/caption_edit.h"
#include "mm/private_archive.h"
#include "mm/wide.h"
#include <stdlib.h>
#include <string.h>

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

int mm_caption_edit_decode(const char *utf8, size_t length, unsigned short *text, size_t capacity, size_t *out)
{
    size_t count;
    if (mm_text_read_utf8(utf8, length, text, capacity, &count) != 1)
        return 0;
    count = mm_caption_edit_normalize(text, count, 13);
    if (count > MM_CAPTION_EDIT_MAX_UNITS)
        return 0;
    *out = count;
    return 1;
}

int mm_caption_edit_encode(const MMTextHost *host, const unsigned short *text, size_t count,
                           unsigned char *output, size_t capacity, size_t *bytes)
{
    size_t i;
    if (count > MM_CAPTION_EDIT_MAX_UNITS || count > capacity / 2)
        return 0;
    if (count && mm_text_ansi_from_units(host, text, count, (char *)output, count, 1) == count)
    {
        *bytes = count;
        return MM_TEXT_WINDOWS;
    }
    for (i = 0; i < count; ++i)
    {
        output[2 * i] = (unsigned char)text[i];
        output[2 * i + 1] = (unsigned char)(text[i] >> 8);
    }
    *bytes = count * 2;
    return MM_TEXT_UTF16_LE;
}

void mm_caption_edit_dispose_text(MMCaptionText *text)
{
    free(text->text);
    text->text = NULL;
    text->count = 0;
}

int mm_caption_edit_load_text(const MMReadStream *stream, const MMTextHost *encoding_host, int encoding,
                              MMCaptionText *out)
{
    MMCaptionText text = {0};
    unsigned char *bytes;
    int result = MM_CAPTION_EDIT_OK;
    if (!stream)
        return MM_CAPTION_EDIT_READ_TEXT;
    if (stream->length > MM_CAPTION_EDIT_MAX_UNITS * 4UL)
        return MM_CAPTION_EDIT_EXISTING_LIMIT;
    if (stream->length)
    {
        bytes = malloc(stream->length);
        if (!bytes)
            return MM_CAPTION_EDIT_MEMORY;
        if (!mm_read_exact(stream, 0, bytes, stream->length))
            result = MM_CAPTION_EDIT_READ_TEXT;
        else
        {
            text.count = mm_text_utf16(encoding_host, bytes, stream->length, encoding, NULL, 0);
            if (!text.count || text.count > MM_CAPTION_EDIT_MAX_UNITS * 4UL)
                result = MM_CAPTION_EDIT_ENCODING;
            else
            {
                text.text = malloc(text.count * sizeof(*text.text));
                if (!text.text)
                    result = MM_CAPTION_EDIT_MEMORY;
                else if (mm_text_utf16(encoding_host, bytes, stream->length, encoding, text.text, text.count) != text.count)
                    result = MM_CAPTION_EDIT_ENCODING;
            }
        }
        free(bytes);
    }
    if (result == MM_CAPTION_EDIT_OK)
    {
        mm_caption_edit_dispose_text(out);
        *out = text;
    }
    else
        mm_caption_edit_dispose_text(&text);
    return result;
}

int mm_caption_edit_font(const char *utf8, size_t length, const MMTextHost *host, MMMovieTitle *out)
{
    unsigned short units[255];
    char ansi[255];
    size_t count;
    if (mm_text_read_utf8(utf8, length, units, 255, &count) != 1 || !count)
        return MM_CAPTION_EDIT_FONT_LENGTH;
    if (out->unit == 2)
        return mm_movie_title_set(out, units, (MMU32)count) ? MM_CAPTION_EDIT_OK : MM_CAPTION_EDIT_FONT;
    count = mm_text_ansi_from_units(host, units, count, ansi, sizeof(ansi), 1);
    return count && mm_movie_title_set(out, ansi, (MMU32)count) ? MM_CAPTION_EDIT_OK : MM_CAPTION_EDIT_FONT;
}

int mm_caption_edit_write_body(const MMRichTextState *body, const MMCaptionText *replacement,
                               const MMTextHost *host, MMTextChunk *out, const MMRichTextArchive *archive)
{
    MMTextEncoded encoded = {0};
    unsigned char *bytes = NULL;
    size_t count;
    int result = 0;
    if (!replacement)
        return mm_rich_text_archive_write(body, NULL, out, archive);
    if (replacement->count > MM_CAPTION_EDIT_MAX_UNITS)
        return 0;
    if (replacement->count)
    {
        bytes = malloc(replacement->count * 2);
        if (!bytes)
            return -1;
    }
    encoded.encoding = mm_caption_edit_encode(host, replacement->text, replacement->count, bytes,
                                               replacement->count * 2, &count);
    if (encoded.encoding)
    {
        encoded.text = bytes;
        encoded.bytes = (MMU32)count;
        result = mm_rich_text_archive_write(body, &encoded, out, archive);
    }
    free(bytes);
    return result;
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
    const MMTextPropertyRows *properties = &s->rows;
    MMI32 row, count = 0, rows = properties->count(properties->context);
    MMTextPropertyEntry entry;
    unsigned kind;
    for (row = 0; row < rows; ++row)
    {
        properties->get(properties->context, row, &entry);
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

static MMTextCollection *caption_properties(void *p)
{
    return ((MMCaptionPropertyMap *)p)->body->properties;
}

static MMArray caption_property_view(void *p, int *big)
{
    const MMArray *rows = mm_text_collection_property_array(caption_properties(p), big);
    MMArray empty = {0};
    if (rows)
        return *rows;
    *big = 0;
    return empty;
}

static void caption_property_publish(void *p, const MMArray *rows)
{
    MMArray *array = mm_text_collection_property_array(caption_properties(p), NULL);
    if (array)
        *array = *rows;
}

static void caption_bad_property_mutation(void *p)
{
    ((MMCaptionPropertyMap *)p)->diagnosed = 1;
}

static void caption_property_clear(void *p)
{
    (void)p;
}

static int caption_property_paragraph_end(void *p, MMI32 at, MMI32 *end)
{
    const MMCaptionEditWords *words = &((const MMCaptionPropertyMap *)p)->words;
    *end = mm_caption_edit_paragraph_end(words->text, (MMI32)words->count, at);
    return 1;
}

static void caption_property_diagnostic(void *p, int kind)
{
    (void)kind;
    caption_bad_property_mutation(p);
}

static void *caption_argument_owner(void *p, int kind)
{
    (void)kind;
    return ((MMCaptionPropertyMap *)p)->body->arguments;
}

static MMI32 caption_argument_count(void *p, void *group)
{
    (void)p;
    return (MMI32)mm_text_collection_count(group);
}

static int caption_argument_free(void *p, void *group, MMI32 row)
{
    MMI32 bytes;
    (void)p;
    return mm_text_collection_argument(group, (MMU32)row, &bytes) == NULL;
}

static MMU32 caption_argument_ref(void *p, void *group, MMI32 row)
{
    (void)p;
    return mm_text_collection_argument_ref(group, (MMU32)row);
}

static void caption_argument_put(void *p, void *group, MMI32 row, MMU32 value)
{
    if (!mm_text_collection_put_argument_ref(group, (MMU32)row, value))
        ((MMCaptionPropertyMap *)p)->diagnosed = 1;
}

static void caption_argument_remove(void *p, void *group, MMI32 row)
{
    if (!mm_text_collection_delete(group, (MMU32)row))
        ((MMCaptionPropertyMap *)p)->diagnosed = 1;
}

void mm_caption_edit_property_services(MMCaptionPropertyMap *map, MMTextPropertyServices *s)
{
    memset(s, 0, sizeof(*s));
    s->context = map;
    map->array.owner = map;
    map->array.view = caption_property_view;
    map->array.publish = caption_property_publish;
    map->array.bad_mutation = caption_bad_property_mutation;
    map->array.insert_failure = -1;
    mm_text_property_array_rows(&s->rows, &map->array);
    s->clear_cache = caption_property_clear;
    s->paragraph_end = caption_property_paragraph_end;
    s->diagnostic = caption_property_diagnostic;
    s->arguments.context = map;
    s->arguments.objects.context = map;
    s->arguments.objects.get = caption_argument_owner;
    s->arguments.rows.context = map;
    s->arguments.rows.count = caption_argument_count;
    s->arguments.rows.free_slot = caption_argument_free;
    s->arguments.rows.fixed = caption_argument_ref;
    s->arguments.rows.put_fixed = caption_argument_put;
    s->arguments.rows.remove = caption_argument_remove;
    s->arguments.diagnostic = caption_property_diagnostic;
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

static int same_options(const MMCaptionEditOptions *a, const MMCaptionEditOptions *b)
{
    return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height &&
           a->first == b->first && a->last == b->last && a->through_end == b->through_end &&
           a->scrolling == b->scrolling && a->font_size == b->font_size && a->bold == b->bold &&
           a->italic == b->italic && a->alignment == b->alignment && a->transparent == b->transparent &&
           a->ink == b->ink && a->paper == b->paper;
}

int mm_caption_edit_plan(const MMCaptionEditInput *value, const MMCaptionEditInput *before, size_t save_capacity,
                         MMCaptionEditPlan *out)
{
    MMCaptionEditPlan plan;
    plan.value = value;
    plan.before = before;
    plan.font_changed = !before || strcmp(value->font, before->font) != 0;
    plan.text_changed = !before || strcmp(value->text, before->text) != 0;
    plan.text_bytes = plan.text_changed || save_capacity ? strlen(value->text) : 0;
    if (save_capacity)
    {
        if (before && !plan.font_changed && !plan.text_changed && same_options(&value->options, &before->options))
            return MM_CAPTION_EDIT_NO_CHANGE;
        if (plan.text_bytes >= save_capacity || (!before && !plan.text_bytes))
            return MM_CAPTION_EDIT_TEXT_LIMIT;
    }
    *out = plan;
    return MM_CAPTION_EDIT_OK;
}

static int edit_text_and_properties(const MMCaptionEditPlan *plan, const MMCaptionEditFormat *format,
                                    MMRichTextState *body, const MMCaptionEditSource *source,
                                    MMCaptionText *replacement)
{
    MMCaptionText old = {0}, text = {0};
    const MMCaptionText *selected;
    MMCaptionPropertyMap map = {0};
    MMTextPropertyServices properties;
    size_t capacity = plan->text_bytes;
    MMI32 characters;
    int status = MM_CAPTION_EDIT_OK;
    if (plan->before)
    {
        status = mm_caption_edit_load_text(source->stored_text, source->encoding, body->text_system, &old);
        if (status != MM_CAPTION_EDIT_OK)
            return status;
    }
    if (plan->text_changed)
    {
        if (capacity > MM_CAPTION_EDIT_MAX_UNITS * 2UL)
            capacity = MM_CAPTION_EDIT_MAX_UNITS * 2UL;
        if (capacity)
        {
            text.text = malloc(capacity * sizeof(*text.text));
            if (!text.text)
            {
                status = MM_CAPTION_EDIT_MEMORY;
                goto done;
            }
        }
        if (!mm_caption_edit_decode(plan->value->text, plan->text_bytes, text.text, capacity, &text.count))
        {
            status = MM_CAPTION_EDIT_UTF8;
            goto done;
        }
    }
    selected = plan->text_changed ? &text : &old;
    map.body = body;
    map.words.text = selected->text;
    map.words.count = selected->count;
    mm_caption_edit_property_services(&map, &properties);
    characters = (MMI32)selected->count + 1;
    if (plan->text_changed)
        status = mm_caption_edit_replace(old.text, (MMI32)old.count, text.text, characters - 1,
                                         &properties, &map.diagnosed);
    if (status == MM_CAPTION_EDIT_OK && format->count &&
        (mm_text_properties_apply_changes(0, characters, characters, format->changes, format->count, &properties) <= 0 ||
         map.diagnosed))
        status = MM_CAPTION_EDIT_APPLY_FORMAT;
    if (status == MM_CAPTION_EDIT_OK && plan->text_changed)
    {
        mm_caption_edit_dispose_text(replacement);
        *replacement = text;
        text.text = NULL;
    }
done:
    mm_caption_edit_dispose_text(&old);
    mm_caption_edit_dispose_text(&text);
    return status;
}

int mm_caption_edit_apply(const MMCaptionEditPlan *plan, MMI32 first, MMI32 last, MMTextBoxState *box,
                          MMRichTextState *body, const MMCaptionEditSource *source,
                          MMCaptionText *replacement, int *body_changed)
{
    MMTextBoxState edited = *box;
    MMCaptionEditFormat format;
    const MMCaptionEditOptions *value = &plan->value->options;
    const MMCaptionEditOptions *before = plan->before ? &plan->before->options : NULL;
    int status = mm_caption_edit_write_box(&edited, first, last, value, before);
    if (status != MM_CAPTION_EDIT_OK)
        return status;
    if (!before && !body->properties)
    {
        body->properties = mm_text_collection_new(MM_TEXT_PROPERTIES, NULL);
        if (!body->properties)
            return MM_CAPTION_EDIT_PROPERTIES;
    }
    if (plan->font_changed)
    {
        status = mm_caption_edit_font(plan->value->font, strlen(plan->value->font), source->encoding, &body->font);
        if (status != MM_CAPTION_EDIT_OK)
            return status;
        body->defaults.font_system = source->native_system;
    }
    status = mm_caption_edit_format(value, before, &format);
    if (status != MM_CAPTION_EDIT_OK)
        return status;
    if (plan->text_changed || format.count)
    {
        status = edit_text_and_properties(plan, &format, body, source, replacement);
        if (status != MM_CAPTION_EDIT_OK)
            return status;
    }
    if (format.defaults & MM_CAPTION_EDIT_SET_SIZE)
        body->defaults.height = value->font_size;
    if (format.defaults & MM_CAPTION_EDIT_SET_PAPER)
        body->defaults.background = (MMU32)format.paper;
    if (format.defaults & MM_CAPTION_EDIT_SET_WIDTH)
        body->defaults.width = value->width;
    *box = edited;
    *body_changed = plan->font_changed || plan->text_changed || format.defaults || format.count;
    return MM_CAPTION_EDIT_OK;
}

int mm_caption_edit_archive_box(const MMMovieArchiveEditor *editor, const MMSceneArchiveText *original,
                                const MMCaptionArchiveBox *host, MMU32 *out)
{
    MMTextChunk previous, body;
    MMU32 number;
    int changed, status;
    if (original)
    {
        if (host->clone(host->context, original->number, &number) != 1 ||
            mm_text_box_read(host->box, number, &previous, editor) != 1 ||
            host->read_body(host->context, &previous) != 1)
            return MM_CAPTION_EDIT_COPY_BOX;
    }
    else if (!editor->add(editor->context, MM_TEXT_BOX, 0, &number, NULL))
        return MM_CAPTION_EDIT_ADD_BOX;
    status = host->apply(host->context, &changed);
    if (status != MM_CAPTION_EDIT_OK)
        return status;
    if (changed)
    {
        MMChunkChild link;
        if (host->write_body(host->context, &body) != 1)
            return MM_CAPTION_EDIT_WRITE_BODY;
        link.type = body.type;
        link.number = body.number;
        link.slot = host->box->child;
        if (mm_chunk_adopt(&editor->edit, MM_TEXT_BOX, number, &link, 1) != MM_CHUNK_OK)
            return MM_CAPTION_EDIT_WRITE_BODY;
        if (original)
        {
            link.type = previous.type;
            link.number = previous.number;
            mm_chunk_unlink(&editor->edit, MM_TEXT_BOX, number, &link);
        }
    }
    if (mm_text_box_write(host->box, number, editor) != 1)
        return MM_CAPTION_EDIT_WRITE_BOX;
    *out = number;
    return MM_CAPTION_EDIT_OK;
}

typedef struct PrivateCaptionEdit
{
    MMPrivateArchive *file;
    const MMCaptionArchiveChange *change;
    MMMovieArchiveEditor editor;
    MMRichTextArchive archive;
    MMTextBoxState box;
    MMRichTextState body;
    MMCaptionText words;
    int status;
} PrivateCaptionEdit;

static int private_clone_box(void *context, MMU32 original, MMU32 *number)
{
    MMPrivateArchive *file = ((PrivateCaptionEdit *)context)->file;
    return mm_private_archive_copy(file, file, MM_TEXT_BOX, original, 1, number);
}

static int private_read_body(void *context, const MMTextChunk *body)
{
    PrivateCaptionEdit *edit = context;
    return mm_rich_text_archive_read(&edit->body, body, &edit->archive) > 0;
}

static int private_apply(void *context, int *changed)
{
    PrivateCaptionEdit *edit = context;
    const MMCaptionArchiveChange *change = edit->change;
    MMRichTextStream stored;
    MMCaptionEditSource source;
    mm_rich_text_archive_stream(&stored, &edit->archive, &edit->body.text);
    source.encoding = change->encoding;
    source.stored_text = &stored.stream;
    source.native_system = edit->editor.system;
    return mm_caption_edit_apply(change->plan, change->first, change->last, &edit->box, &edit->body,
                                 &source, &edit->words, changed);
}

static int private_write_body(void *context, MMTextChunk *body)
{
    PrivateCaptionEdit *edit = context;
    const MMCaptionArchiveChange *change = edit->change;
    const MMCaptionText *words = change->plan->text_changed ? &edit->words : NULL;
    return mm_caption_edit_write_body(&edit->body, words, change->encoding, body, &edit->archive) > 0;
}

static int private_write_box(void *context, const MMSceneArchiveText *original, MMU32 *number)
{
    PrivateCaptionEdit *edit = context;
    MMCaptionArchiveBox box = {NULL, NULL, private_clone_box, private_read_body, private_apply, private_write_body};
    box.context = edit;
    box.box = &edit->box;
    edit->status = mm_caption_edit_archive_box(&edit->editor, original, &box, number);
    return edit->status == MM_CAPTION_EDIT_OK;
}

int mm_caption_edit_private_archive(MMPrivateArchive *file, const MMCaptionArchiveChange *change,
                                    int *caption_status)
{
    PrivateCaptionEdit edit = {0};
    MMSceneArchiveTextEdit scene = {0};
    int status;
    edit.file = file;
    edit.change = change;
    edit.editor = mm_private_archive_editor(file);
    edit.archive = mm_private_archive_rich_text(file, &edit.editor);
    mm_rich_text_init(&edit.body, edit.editor.text_unit, edit.editor.system);
    scene.scene = change->scene;
    scene.opening_frame = change->first;
    scene.context = &edit;
    scene.original = change->original;
    if (change->plan)
        scene.write_box = private_write_box;
    status = mm_scene_archive_edit_text(&edit.editor, &scene);
    mm_rich_text_archive_dispose(&edit.body, &edit.archive);
    mm_caption_edit_dispose_text(&edit.words);
    if (caption_status)
        *caption_status = edit.status;
    return status;
}
