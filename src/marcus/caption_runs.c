#include "caption_runs.h"
#include <stdlib.h>
#include <string.h>

typedef struct RunLayout
{
    MarcusCaptionRuns *runs;
    const MarcusCaptionFonts *fonts;
    size_t budget;
    MMCaptionCaretBuilder caret;
} RunLayout;

static MarcusCaptionRun *rows(const MarcusCaptionRuns *runs)
{
    return (MarcusCaptionRun *)runs->storage.buffers[0].data;
}

const MarcusCaptionStop *marcus_caption_runs_stops(const MarcusCaptionRuns *runs)
{
    return (const MarcusCaptionStop *)runs->storage.buffers[1].data;
}

static void clear_run(MarcusCaptionRun *run)
{
    if (run->mask.release)
        run->mask.release(run->mask.owner);
    free(run->text);
}

void marcus_caption_runs_clear(MarcusCaptionRuns *runs)
{
    size_t i;
    if (runs->scene_bytes)
    {
        for (i = 0; i < runs->count; ++i)
            clear_run(rows(runs) + i);
        *runs->scene_bytes -= runs->mask_bytes;
        mm_storage_dispose(&runs->storage);
    }
    memset(runs, 0, sizeof(*runs));
}

static int reserve(MMCollectionStorage *storage, unsigned section, size_t count, size_t width)
{
    MMU32 bytes[2] = {0, 0};
    if (count > (MM_STORAGE_LIMIT - 1) / width)
        return 0;
    storage->min_grow[section] = storage->buffers[section].capacity ?
        storage->buffers[section].capacity : (MMU32)(8 * width);
    bytes[section] = (MMU32)(count * width);
    return mm_storage_ensure(storage, bytes[0], bytes[1], 0) == MM_STORAGE_OK;
}

static void read_property(void *p, size_t row, MMCaptionProperty *property)
{
    const MarcusCaptionFonts *fonts = ((RunLayout *)p)->fonts;
    fonts->property(fonts->context, row, property);
}

static int set_face(void *p, const MMCaptionStyle *style)
{
    const MarcusCaptionFonts *fonts = ((RunLayout *)p)->fonts;
    return fonts->face(fonts->context, style) ? 0 : MARCUS_CAPTION_RUNS_FONT;
}

static int measure(void *p, const unsigned short *text, MMI32 length, MMI32 *width, MMI32 *top, MMI32 *bottom)
{
    const MarcusCaptionFonts *fonts = ((RunLayout *)p)->fonts;
    MarcusCaptionMetrics metrics;
    if (!fonts->measure(fonts->context, text, length, &metrics))
        return MARCUS_CAPTION_RUNS_MEASURE;
    *width = metrics.advance;
    *top = -metrics.ascent;
    *bottom = metrics.cell_height + metrics.leading - metrics.ascent;
    return 0;
}

static int emit(void *p, const MMCaptionStyle *style, const unsigned short *text, MMI32 length,
                MMI32 x, MMI32 baseline)
{
    const RunLayout *layout = p;
    const MarcusCaptionFonts *fonts = layout->fonts;
    MarcusCaptionRuns *runs = layout->runs;
    const size_t text_bytes = (size_t)length * sizeof(*text);
    MarcusCaptionRun run = {0};
    MarcusCaptionMetrics metrics;
    char *font;
    if (!fonts->face(fonts->context, style) ||
        !fonts->measure(fonts->context, text, length, &metrics) ||
        !fonts->mask(fonts->context, text, length, &run.mask))
        return MARCUS_CAPTION_RUNS_RASTER;
    if (run.mask.bytes > layout->budget - *runs->scene_bytes)
    {
        clear_run(&run);
        return MARCUS_CAPTION_RUNS_BUDGET;
    }
    if (style->font.length > (size_t)-1 - text_bytes - 1 ||
        !(run.text = malloc(text_bytes + style->font.length + 1)) ||
        !reserve(&runs->storage, 0, runs->count + 1, sizeof(run)))
    {
        clear_run(&run);
        return MARCUS_CAPTION_RUNS_MEMORY;
    }
    memcpy(run.text, text, text_bytes);
    font = (char *)run.text + text_bytes;
    if (style->font.length)
        memcpy(font, style->font.name, style->font.length);
    font[style->font.length] = '\0';
    run.style = *style;
    run.style.font.name = font;
    run.length = length;
    run.x = x;
    run.y = baseline - metrics.ascent;
    run.height = metrics.cell_height + metrics.leading;
    run.ascent = metrics.ascent;
    rows(runs)[runs->count++] = run;
    runs->mask_bytes += run.mask.bytes;
    *runs->scene_bytes += run.mask.bytes;
    return 0;
}

static int caret_advance(void *p, const unsigned short *text, MMI32 length, MMI32 *advance)
{
    const MarcusCaptionFonts *fonts = ((RunLayout *)p)->fonts;
    MarcusCaptionMetrics metrics;
    if (!fonts->measure(fonts->context, text, length, &metrics))
        return MARCUS_CAPTION_RUNS_CARET;
    *advance = metrics.advance;
    return 0;
}

static int save_caret(void *p, const MMCaptionCaretStop *stop, int replace)
{
    MarcusCaptionRuns *runs = ((RunLayout *)p)->runs;
    MarcusCaptionStop *stored;
    if (!replace)
    {
        if (!reserve(&runs->storage, 1, runs->stop_count + 1, sizeof(*stored)))
            return MARCUS_CAPTION_RUNS_MEMORY;
        ++runs->stop_count;
    }
    stored = (MarcusCaptionStop *)runs->storage.buffers[1].data + runs->stop_count - 1;
    stored->offset = stop->offset;
    stored->x = stop->x;
    stored->y = stop->y;
    stored->height = stop->height;
    return 0;
}

static int position(void *p, const MMCaptionLayoutRun *run)
{
    MMCaptionCaretServices caret = {NULL, caret_advance, save_caret};
    caret.context = p;
    return mm_caption_caret_run(&((RunLayout *)p)->caret, run, &caret);
}

int marcus_caption_runs_layout(MarcusCaptionRuns *out, const MarcusCaptionInput *input, const MarcusCaptionFonts *fonts,
                                size_t *scene_bytes, size_t budget, int editing)
{
    MarcusCaptionRuns next = {0};
    RunLayout layout = {0};
    MMCaptionLayoutInput text;
    MMCaptionLayoutServices services = {NULL, read_property, set_face, measure, emit, NULL};
    MMI32 *offsets = NULL;
    int status;
    if (out->ready)
        return 0;
    text.width = mm_caption_layout_width(&input->box, input->length);
    if (!text.width)
        return MARCUS_CAPTION_RUNS_BOUNDS;
    if (*scene_bytes > budget)
        return MARCUS_CAPTION_RUNS_BUDGET;
    text.text = input->text;
    text.length = (MMI32)input->length;
    text.pixels = input->pixels;
    text.font = input->font;
    text.properties = input->properties;
    if (editing)
    {
        offsets = malloc((input->length + 1) * sizeof(*offsets));
        if (!offsets)
            return MARCUS_CAPTION_RUNS_MEMORY;
        mm_caption_caret_offsets(input->text, text.length, offsets);
        services.position = position;
    }
    if (mm_storage_init(&next.storage, NULL) != MM_STORAGE_OK)
    {
        free(offsets);
        return MARCUS_CAPTION_RUNS_MEMORY;
    }
    next.scene_bytes = scene_bytes;
    layout.runs = &next;
    layout.fonts = fonts;
    layout.budget = budget;
    layout.caret.offsets = offsets;
    layout.caret.length = text.length;
    layout.caret.left = input->box.left;
    layout.caret.top = input->box.top;
    layout.caret.last_offset = -1;
    services.context = &layout;
    status = mm_caption_layout(&text, &services, &next.height);
    free(offsets);
    if (status < 0)
        marcus_caption_runs_clear(&next);
    else
    {
        next.ready = 1;
        *out = next;
    }
    return status;
}

static void read_caret(void *p, size_t index, MMCaptionCaretStop *out)
{
    const MarcusCaptionStop *stop = marcus_caption_runs_stops(p) + index;
    out->offset = stop->offset;
    out->x = stop->x;
    out->y = stop->y;
    out->height = stop->height;
}

int marcus_caption_runs_caret(const MarcusCaptionRuns *runs, MMCaptionPresentation *presentation,
                             MMI32 offset, MMI32 top, MMI32 bottom)
{
    return mm_caption_presentation_caret(presentation, offset, runs->height, top, bottom, runs->stop_count,
                                         (void *)runs, read_caret);
}

int marcus_caption_runs_paint(const MarcusCaptionRuns *runs, MMCaptionCanvas *canvas, const MMTextBoxBounds *box,
                               MMI32 scroll, MMU32 background, const MarcusCaptionPaintServices *services)
{
    size_t i;
    mm_caption_box(canvas, box, services->colour(services->context, background));
    for (i = 0; i < runs->count; ++i)
    {
        const MarcusCaptionRun *row = rows(runs) + i;
        const int scale = mm_caption_raster_scale(canvas, row->style.pixels);
        MarcusCaptionRaster raster = row->mask.raster;
        MMCaptionGlyph glyph;
        if (scale > 1 && !services->raster(services->context, row, scale, &raster))
            return 0;
        glyph.mask = raster.geometry;
        glyph.ink = raster.ink;
        glyph.paper = raster.paper;
        glyph.x = row->x;
        glyph.y = row->y;
        glyph.ascent = row->ascent;
        glyph.raster_ascent = raster.ascent;
        glyph.density = raster.density;
        glyph.supersampled = scale > 1;
        glyph.foreground = services->colour(services->context, row->style.foreground);
        glyph.background = services->colour(services->context, row->style.background);
        mm_caption_glyph(canvas, box, scroll, &glyph);
    }
    return 1;
}
