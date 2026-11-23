#include "caption_catalog.h"
#include "mm/caption_render.h"
#include "mm/private_archive.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define RGB 0x01000000UL
#define CLEAR 0xffffffffUL

MarcusCaptionList *marcus_caption_catalog_create(unsigned long document, MMU32 scene, MMI32 first, MMI32 last,
                                                 MMI32 current)
{
    MarcusCaptionList *list = calloc(1, sizeof(*list));
    MarcusCaptionOptions *value;
    if (!list)
        return NULL;
    list->document = document;
    list->scene = scene;
    list->first_frame = first;
    list->last_frame = last;
    value = &list->defaults;
    value->x = 32;
    value->y = MM_CAPTION_HEIGHT - 88;
    value->width = MM_CAPTION_WIDTH - 2 * value->x;
    value->height = 56;
    value->first_frame = current - first + 1;
    value->last_frame = last - first + 1;
    value->through_end = value->transparent = 1;
    value->font_size = 20;
    value->alignment = 2;
    value->ink = 0xffffffUL;
    memcpy(value->font, "Arial", sizeof("Arial"));
    return list;
}

static void clear_rows(MarcusCaptionEntry *rows, int count)
{
    int i;
    for (i = 0; i < count; ++i)
        free(rows[i].text);
    free(rows);
}

static void clear_fonts(char **names, int count)
{
    int i;
    for (i = 0; i < count; ++i)
        free(names[i]);
    free(names);
}

void marcus_captions_destroy(MarcusCaptionList *list)
{
    if (list)
    {
        clear_rows(list->entries, list->count);
        clear_fonts(list->font_names, list->font_count);
        free(list);
    }
}

int marcus_caption_count(const MarcusCaptionList *list)
{
    return list ? list->count : 0;
}

const MarcusCaptionEntry *marcus_caption_catalog_entry(const MarcusCaptionList *list, int index)
{
    return list && index >= 0 && index < list->count ? &list->entries[index] : NULL;
}

const char *marcus_caption(const MarcusCaptionList *list, int index, MarcusCaptionOptions *options)
{
    const MarcusCaptionEntry *entry;
    if (list && index == -1)
    {
        if (options)
            *options = list->defaults;
        return "";
    }
    entry = marcus_caption_catalog_entry(list, index);
    if (!entry)
        return NULL;
    if (options)
        *options = entry->options;
    return entry->text;
}

int marcus_caption_catalog_find(const MarcusCaptionList *list, MMU32 chunk)
{
    int i;
    for (i = 0; i < list->count; ++i)
        if (list->entries[i].chunk == chunk)
            return i;
    return -1;
}

const char *marcus_caption_font_name(const MarcusCaptionList *list, int index)
{
    return list && index >= 0 && index < list->font_count ? list->font_names[index] : NULL;
}

typedef struct UTF8Buffer
{
    char *next;
    size_t remaining;
} UTF8Buffer;

static int write_utf8(void *p, const char *bytes, size_t count)
{
    UTF8Buffer *out = p;
    if (count > out->remaining)
        return 0;
    memcpy(out->next, bytes, count);
    out->next += count;
    out->remaining -= count;
    return 1;
}

static int editor_text(const MMRichTextState *body, const MMRichTextArchive *archive, const MMTextHost *encoding,
                       char **out, int *detail)
{
    const size_t limit = MARCUS_CAPTION_UTF8_CAPACITY - 1;
    MMRichTextStream stream;
    MMCaptionText words = {0};
    size_t length, capacity;
    UTF8Buffer buffer;
    char *text;
    int encoded;
    mm_rich_text_archive_stream(&stream, archive, &body->text);
    *detail = mm_caption_edit_load_text(&stream.stream, encoding, body->text_system, &words);
    if (*detail != MM_CAPTION_EDIT_OK)
        return MARCUS_CAPTION_CATALOG_EDIT;
    length = mm_caption_edit_normalize(words.text, words.count, 10);
    capacity = length <= limit / 3 ? length * 3 : limit;
    text = malloc(capacity + 1);
    if (!text)
    {
        mm_caption_edit_dispose_text(&words);
        return MARCUS_CAPTION_CATALOG_MEMORY;
    }
    buffer.next = text;
    buffer.remaining = capacity;
    encoded = mm_text_encode_utf8(words.text, length, &buffer, write_utf8);
    mm_caption_edit_dispose_text(&words);
    if (!encoded || memchr(text, 0, (size_t)(buffer.next - text)))
    {
        free(text);
        return encoded ? MARCUS_CAPTION_CATALOG_TEXT_LIMIT : MARCUS_CAPTION_CATALOG_UTF8;
    }
    *buffer.next = 0;
    *out = text;
    return MARCUS_CAPTION_CATALOG_OK;
}

static int editor_format(MarcusCaptionOptions *out, const MMTextBoxState *box, const MMRichTextState *body,
                         const MarcusCaptionList *list, const MMTextHost *encoding, int native_system,
                         const void *p, MarcusCaptionColour *colour, int *detail)
{
    MMCaptionEditOptions fields;
    unsigned short units[MM_STRING_MAX];
    UTF8Buffer font;
    size_t length;
    MMU32 i, count;
    *detail = mm_caption_edit_read_box(box, list->first_frame, list->last_frame, &fields);
    if (*detail != MM_CAPTION_EDIT_OK)
        return MARCUS_CAPTION_CATALOG_EDIT;
    out->x = fields.x;
    out->y = fields.y;
    out->width = fields.width;
    out->height = fields.height;
    out->first_frame = fields.first;
    out->last_frame = fields.last;
    out->through_end = fields.through_end;
    out->scrolling = fields.scrolling;
    out->font_size = body->defaults.height;
    out->transparent = body->defaults.background == CLEAR;
    out->paper = out->transparent ? 0 : colour(p, body->defaults.background) & 0xffffffUL;
    length = mm_text_utf16(encoding, mm_movie_title_text(&body->font), body->font.count * body->font.unit,
                           native_system, units, sizeof(units) / sizeof(*units));
    if (body->font.count && !length)
    {
        *detail = MM_CAPTION_EDIT_ENCODING;
        return MARCUS_CAPTION_CATALOG_EDIT;
    }
    font.next = out->font;
    font.remaining = sizeof(out->font) - 1;
    if (!mm_text_encode_utf8(units, length, &font, write_utf8))
        return MARCUS_CAPTION_CATALOG_FONT_LIMIT;
    *font.next = 0;
    count = mm_text_collection_count(body->properties);
    for (i = 0; i < count; ++i)
    {
        MMU32 property[2];
        if (!mm_text_collection_property(body->properties, i, property) || (property[0] & 0xffffffUL))
            continue;
        switch (property[0] >> 24)
        {
        case MM_CAPTION_STYLE:
            out->bold = (property[1] & 1) != 0;
            out->italic = (property[1] & 2) != 0;
            break;
        case MM_CAPTION_FOREGROUND:
            out->ink = colour(p, RGB + property[1]) & 0xffffffUL;
            break;
        case MM_CAPTION_ALIGNMENT:
            out->alignment = property[1] >> 24;
            break;
        }
    }
    return MARCUS_CAPTION_CATALOG_OK;
}

int marcus_caption_catalog_load(MarcusCaptionList *list, MMPrivateArchive *file, const MMTextHost *encoding,
                                int native_system, const void *p, MarcusCaptionColour *colour, int *detail)
{
    MMMovieArchiveEditor editor = mm_private_archive_editor(file);
    MMRichTextArchive archive = mm_private_archive_rich_text(file, &editor);
    MMSceneArchiveText *links;
    MarcusCaptionEntry *rows = NULL;
    MMU32 count, i;
    int status = MARCUS_CAPTION_CATALOG_OK;
    *detail = mm_scene_archive_list_text(&editor, list->scene, &links, &count);
    if (*detail != MM_SCENE_ARCHIVE_TEXT_OK)
        return MARCUS_CAPTION_CATALOG_ARCHIVE;
    if (count > INT_MAX || (size_t)count > (size_t)-1 / sizeof(*rows) ||
        (count && !(rows = calloc(count, sizeof(*rows)))))
        status = MARCUS_CAPTION_CATALOG_MEMORY;
    for (i = 0; status == MARCUS_CAPTION_CATALOG_OK && i < count; ++i)
    {
        MMTextChunk chunk;
        MMTextBoxState box;
        MMRichTextState body;
        rows[i].slot = links[i].slot;
        rows[i].chunk = links[i].number;
        mm_rich_text_init(&body, editor.text_unit, editor.system);
        if (mm_text_box_read(&box, rows[i].chunk, &chunk, &editor) <= 0 ||
            mm_rich_text_archive_read(&body, &chunk, &archive) <= 0)
            status = MARCUS_CAPTION_CATALOG_BOX;
        else
        {
            status = editor_text(&body, &archive, encoding, &rows[i].text, detail);
            if (status == MARCUS_CAPTION_CATALOG_OK)
                status = editor_format(&rows[i].options, &box, &body, list, encoding, native_system, p, colour, detail);
        }
        mm_rich_text_archive_dispose(&body, &archive);
    }
    mm_scene_archive_free_text_list(links);
    if (status != MARCUS_CAPTION_CATALOG_OK)
    {
        if (rows)
            clear_rows(rows, (int)count);
        return status;
    }
    clear_rows(list->entries, list->count);
    list->entries = rows;
    list->count = (int)count;
    return status;
}

typedef struct FontCandidate
{
    const char *name;
    size_t bytes, order;
} FontCandidate;

static unsigned fold(unsigned c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int font_key(const FontCandidate *a, const FontCandidate *b)
{
    size_t i;
    for (i = 0; i < a->bytes && i < b->bytes; ++i)
    {
        unsigned x = fold((unsigned char)a->name[i]);
        unsigned y = fold((unsigned char)b->name[i]);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return a->bytes < b->bytes ? -1 : a->bytes != b->bytes;
}

static int order_fonts(const void *left, const void *right)
{
    const FontCandidate *a = left, *b = right;
    int key = font_key(a, b);
    if (key)
        return key;
    return a->order < b->order ? -1 : a->order != b->order;
}

int marcus_caption_catalog_fonts(MarcusCaptionList *list, size_t count, const void *p, MarcusCaptionFontRead *read,
                                 const MMTextHost *encoding, unsigned native_unit)
{
    FontCandidate *candidates = NULL;
    char **names = NULL;
    int selected = 0;
    size_t i, accepted = 0;
    if (count > INT_MAX || count > (size_t)-1 / sizeof(*candidates) ||
        (count && !(candidates = malloc(count * sizeof(*candidates)))))
        return 0;
    for (i = 0; i < count; ++i)
    {
        MMMovieTitle font;
        FontCandidate *row = &candidates[accepted];
        row->name = read(p, i, &row->bytes);
        row->order = i;
        mm_movie_title_init(&font, native_unit);
        if (mm_caption_edit_font(row->name, row->bytes, encoding, &font) == MM_CAPTION_EDIT_OK)
            ++accepted;
    }
    if (accepted && !(names = calloc(accepted, sizeof(*names))))
        goto failed;
    if (accepted > 1)
        qsort(candidates, accepted, sizeof(*candidates), order_fonts);
    for (i = 0; i < accepted; ++i)
    {
        const FontCandidate *font = &candidates[i];
        if (i + 1 < accepted && !font_key(font, &candidates[i + 1]))
            continue;
        names[selected] = malloc(font->bytes + 1);
        if (!names[selected])
            goto failed;
        memcpy(names[selected], font->name, font->bytes);
        names[selected++][font->bytes] = 0;
    }
    free(candidates);
    clear_fonts(list->font_names, list->font_count);
    list->font_names = names;
    list->font_count = selected;
    return 1;
failed:
    clear_fonts(names, selected);
    free(candidates);
    return 0;
}
