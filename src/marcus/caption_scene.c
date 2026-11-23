#include "caption_scene.h"
#include "mm/preview_media.h"
#include <stdlib.h>
#include <string.h>

enum { CAPTION_SCENE_BUDGET = 128UL * 1024UL * 1024UL };

static void clear_caption(MarcusStoredCaption *caption)
{
    MarcusCaptionInput *layout = &caption->source.layout;
    size_t i;
    marcus_caption_runs_clear(&caption->runs);
    for (i = 0; i < layout->properties; ++i)
        free((void *)caption->properties[i].font.name);
    free(caption->properties);
    free((void *)layout->font.name);
    free((void *)layout->text);
}

static void clear_rows(MarcusStoredCaption *rows, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i)
        clear_caption(rows + i);
    free(rows);
}

static void *copy_bytes(const void *bytes, size_t size)
{
    void *copy = malloc(size);
    if (copy)
        memcpy(copy, bytes, size);
    return copy;
}

static void read_property(const void *p, size_t row, MMCaptionProperty *out)
{
    *out = ((const MMCaptionProperty *)p)[row];
}

static int copy_caption(MarcusStoredCaption *caption, const MarcusCaptionSource *source)
{
    const MarcusCaptionInput *value = &source->layout;
    MarcusCaptionInput *layout = &caption->source.layout;
    size_t i;
    caption->source = *source;
    layout->font.name = NULL;
    layout->text = NULL;
    layout->properties = 0;
    layout->property = read_property;
    layout->property_context = NULL;
    if (value->font.length && !(layout->font.name = copy_bytes(value->font.name, value->font.length)))
        return MARCUS_CAPTION_RUNS_MEMORY;
    if (value->length &&
        (value->length > (size_t)-1 / sizeof(*value->text) ||
         !(layout->text = copy_bytes(value->text, value->length * sizeof(*value->text)))))
        return MARCUS_CAPTION_RUNS_MEMORY;
    if (value->properties)
    {
        if (value->properties > (size_t)-1 / sizeof(*caption->properties) ||
            !(caption->properties = calloc(value->properties, sizeof(*caption->properties))))
            return MARCUS_CAPTION_RUNS_MEMORY;
        layout->property_context = caption->properties;
    }
    for (i = 0; i < value->properties; ++i)
    {
        MMCaptionProperty row;
        value->property(value->property_context, i, &row);
        if (row.font.length)
        {
            row.font.name = copy_bytes(row.font.name, row.font.length);
            if (!row.font.name)
                return MARCUS_CAPTION_RUNS_MEMORY;
        }
        else
            row.font.name = NULL;
        caption->properties[layout->properties++] = row;
    }
    return 0;
}

static void read_row(void *p, size_t i, MMCaptionRowInput *row)
{
    const MarcusCaptionSource *source = &((const MarcusStoredCaption *)p)[i].source;
    row->first = source->first;
    row->end = source->end;
    row->bounds = source->layout.box;
    row->chunk = source->chunk;
    row->story = source->story;
}

int marcus_caption_scene_init(MarcusCaptionScene *scene)
{
    memset(scene, 0, sizeof(*scene));
    return mm_caption_presentation_init(&scene->presentation, NULL);
}

void marcus_caption_scene_clear_draft(MarcusCaptionScene *scene)
{
    if (scene->draft)
    {
        clear_caption(scene->draft);
        free(scene->draft);
        scene->draft = NULL;
    }
    scene->presentation.draft_scroll = 0;
}

void marcus_caption_scene_clear(MarcusCaptionScene *scene)
{
    marcus_caption_scene_clear_draft(scene);
    clear_rows(scene->rows, scene->count);
    scene->rows = NULL;
    scene->count = scene->capacity = 0;
    mm_caption_presentation_clear(&scene->presentation);
}

void marcus_caption_scene_dispose(MarcusCaptionScene *scene)
{
    marcus_caption_scene_clear(scene);
    mm_caption_presentation_dispose(&scene->presentation);
}

int marcus_caption_scene_load(MarcusCaptionScene *scene, size_t count, const void *p, MarcusCaptionRead *read)
{
    MarcusStoredCaption *rows = NULL;
    size_t i;
    int status;
    if (count &&
        (count > (size_t)-1 / sizeof(*rows) || !(rows = calloc(count, sizeof(*rows)))))
        return MARCUS_CAPTION_RUNS_MEMORY;
    for (i = 0; i < count; ++i)
    {
        MarcusCaptionSource source;
        status = read(p, i, &source) ? copy_caption(rows + i, &source) : MM_CAPTION_LAYOUT_FAILED;
        if (status)
        {
            clear_rows(rows, i + 1);
            return status;
        }
    }
    if (!mm_caption_presentation_load(&scene->presentation, count, read_row, rows))
    {
        clear_rows(rows, count);
        return MARCUS_CAPTION_RUNS_MEMORY;
    }
    marcus_caption_scene_clear_draft(scene);
    clear_rows(scene->rows, scene->count);
    scene->rows = rows;
    scene->count = scene->capacity = count;
    return 0;
}

int marcus_caption_scene_take(MarcusCaptionScene *scene, MMPreviewOwnedCaption *caption)
{
    const MMPreviewCaptionData *data = &caption->data;
    MarcusStoredCaption *row;
    MarcusCaptionInput *layout;
    if (scene->count == scene->capacity)
    {
        const size_t limit = (size_t)-1 / sizeof(*scene->rows);
        size_t capacity = scene->capacity;
        if (capacity == limit)
            return -1;
        capacity = capacity ? (capacity <= limit / 2 ? 2 * capacity : limit) : 8;
        row = realloc(scene->rows, capacity * sizeof(*row));
        if (!row)
            return -1;
        scene->rows = row;
        scene->capacity = capacity;
    }
    row = &scene->rows[scene->count++];
    memset(row, 0, sizeof(*row));
    row->source.first = data->first;
    row->source.end = data->end;
    row->source.chunk = caption->chunk;
    row->source.background = data->background;
    row->source.story = data->story;
    row->properties = caption->properties;
    layout = &row->source.layout;
    layout->box.left = data->left;
    layout->box.top = data->top;
    layout->box.right = data->right;
    layout->box.bottom = data->bottom;
    layout->pixels = data->font_size;
    layout->font = caption->font;
    layout->text = caption->text;
    layout->length = caption->length;
    layout->properties = caption->property_count;
    layout->property_context = row->properties;
    layout->property = read_property;
    memset(caption, 0, sizeof(*caption));
    return 1;
}

int marcus_caption_scene_publish(MarcusCaptionScene *scene, MMI32 frame)
{
    if (!mm_caption_presentation_load(&scene->presentation, scene->count, read_row, scene->rows))
        return MARCUS_CAPTION_RUNS_MEMORY;
    mm_caption_presentation_observe(&scene->presentation, frame);
    return 0;
}

int marcus_caption_scene_draft(MarcusCaptionScene *scene, const MarcusCaptionSource *source, const MarcusCaptionFonts *fonts)
{
    const MMI32 scroll = scene->presentation.draft_scroll;
    MarcusStoredCaption *draft = calloc(1, sizeof(*draft));
    const MMTextBoxBounds *box;
    int status = draft ? copy_caption(draft, source) : MARCUS_CAPTION_RUNS_MEMORY;
    marcus_caption_scene_clear_draft(scene);
    scene->draft = draft;
    if (!status)
        status = marcus_caption_runs_layout(&scene->draft->runs, &scene->draft->source.layout, fonts,
                                            &scene->mask_bytes, CAPTION_SCENE_BUDGET, 1);
    if (status)
        marcus_caption_scene_clear_draft(scene);
    else
    {
        box = &draft->source.layout.box;
        mm_caption_presentation_draft(&scene->presentation, scroll, draft->runs.height, box->top, box->bottom);
    }
    return status;
}

int marcus_caption_scene_layout(MarcusCaptionScene *scene, MarcusStoredCaption *caption, const MarcusCaptionFonts *fonts)
{
    return marcus_caption_runs_layout(&caption->runs, &caption->source.layout, fonts,
                                      &scene->mask_bytes, CAPTION_SCENE_BUDGET, 0);
}

int marcus_caption_scene_measure(const MarcusCaptionSource *source, const MarcusCaptionFonts *fonts, MMI32 *height)
{
    size_t bytes = 0;
    MarcusCaptionRuns runs = {0};
    const int status = marcus_caption_runs_layout(&runs, &source->layout, fonts, &bytes, CAPTION_SCENE_BUDGET, 0);
    if (!status)
        *height = runs.height;
    marcus_caption_runs_clear(&runs);
    return status;
}

MarcusStoredCaption *marcus_caption_scene_row(MarcusCaptionScene *scene, size_t row)
{
    return row ? scene->rows + row - 1 : scene->draft;
}
