#include "mm/caption_layout.h"
#include "mm/caption_render.h"
#include "mm/rich_text_layout.h"
#include <stdio.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { printf("layout check failed at %d: %s\n", __LINE__, #e); return 1; } } while (0)
typedef struct Host
{
    unsigned short text[512], buffer[128];
    MMI32 length, styleLimit, objectWidth, offset, ascent, descent;
    MMRichTextParagraph paragraph;
    MMRichTextLine line;
    unsigned published;
    int calls, failAt, object, invalid;
} Host;
static int provide(void *context, MMRichTextOperation op, MMRichTextRequest *q)
{
    Host *h = (Host *)context;
    MMI32 i;
    if (++h->calls == h->failAt) return -73;
    switch (op)
    {
    case MM_RICH_COPY_STYLE: case MM_RICH_SET_FONT: case MM_RICH_WARM_FONT: break;
    case MM_RICH_OFFSET: q->value = h->offset; break;
    case MM_RICH_FLAGS: q->flags = mm_rich_text_flags(q->character); break;
    case MM_RICH_LENGTH: q->value = h->length; break;
    case MM_RICH_PARAGRAPH_START:
        q->value = q->cp == 0 || (q->cp < h->length && h->text[q->cp - 1] == '\r'); break;
    case MM_RICH_FETCH:
        if (q->cp < 0 || q->count < 0 || q->cp + q->count > h->length) return -74;
        memcpy(q->characters, h->text + q->cp, q->count * sizeof(unsigned short)); break;
    case MM_RICH_MEASURE:
        q->left = 0; q->right = q->count * 6;
        q->top = -h->ascent; q->bottom = h->descent;
        /* Also validate every measured prefix is initialized. */
        for (i = 0; i < q->count; ++i)
            if (((unsigned short *)q->characters)[i] == 0xdead) return -75;
        break;
    case MM_RICH_OBJECT:
        q->value = h->object; q->left = 0; q->right = h->objectWidth;
        q->top = -23; q->bottom = 8; break;
    case MM_RICH_FETCH_STYLE: q->value = h->styleLimit; break;
    case MM_RICH_FETCH_PARAGRAPH: q->paragraph = h->paragraph; q->value = h->length; break;
    case MM_RICH_PUBLISH:
        if (q->published & 1)
        { h->line.xpLeft = q->line.xpLeft; h->line.ccp = q->line.ccp; h->line.dyp = q->line.dyp; }
        if (q->published & 2)
        { h->line.cpMin = q->line.cpMin; h->line.dypTot = q->line.dypTot; h->line.dypAscent = q->line.dypAscent; }
        h->published = q->published; break;
    case MM_RICH_INVALID_STYLE: h->invalid++; break;
    }
    return 0;
}
static MMRichTextProvider setup(Host *h, const char *text)
{
    MMRichTextProvider p;
    unsigned i;
    memset(h, 0, sizeof(*h));
    h->length = (MMI32)strlen(text); h->styleLimit = h->length;
    for (i = 0; i < (unsigned)h->length; ++i) h->text[i] = (unsigned char)text[i];
    for (i = 0; i < 128; ++i) h->buffer[i] = 0xdead;
    h->ascent = 13; h->descent = 3; h->objectWidth = 11;
    h->paragraph.tab = 18; h->paragraph.lineScale = h->paragraph.afterScale = 256;
    h->line.cpMin = h->line.dypTot = h->line.dypAscent = -99;
    p.context = h; p.call = provide; p.characters = h->buffer; p.unitBytes = 2;
    return p;
}

typedef struct CaptionRuns
{
    MMI32 x[4], baseline[4];
    unsigned short characters[4];
    MMCaptionStyle style[4];
    int count, fail_at;
} CaptionRuns;

static void caption_property(void *context, size_t index, MMCaptionProperty *out)
{
    static const MMCaptionProperty properties[] = {
        {1, 3, 0x11020304U, {NULL, 0}},
        {0, 2, 1, {"Alt\0X", 5}},
        {1, 2, 0, {NULL, 0}},
        {0, 1, 0xfff6fd05U, {NULL, 0}},
        {1, 1, 0x01050202U, {NULL, 0}},
        {0, 4, 2, {NULL, 0}},
        {0, 128, 0x0101fff3U, {NULL, 0}},
        {0, 129, 1, {NULL, 0}},
        {0, 130, 2, {NULL, 0}}
    };
    (void)context;
    *out = properties[index];
}

static int caption_set_font(void *context, const MMCaptionStyle *style)
{
    (void)context;
    (void)style;
    return 0;
}

static int caption_measure(void *context, const unsigned short *text, MMI32 count,
                           MMI32 *width, MMI32 *top, MMI32 *bottom)
{
    (void)context;
    (void)text;
    *width = 4 * count;
    *top = -6;
    *bottom = 2;
    return 0;
}

static int caption_emit(void *context, const MMCaptionStyle *style, const unsigned short *text,
                        MMI32 count, MMI32 x, MMI32 baseline)
{
    CaptionRuns *runs = (CaptionRuns *)context;
    int i = runs->count;
    if (i == runs->fail_at || i >= 4 || count != 1)
        return -70;
    runs->x[i] = x;
    runs->baseline[i] = baseline;
    runs->characters[i] = text[0];
    runs->style[i] = *style;
    ++runs->count;
    return 0;
}

static int test_caption_layout(void)
{
    static const unsigned short text[] = {'A', 'B', '\r', '\n', 'C', '\t', 'D'};
    MMCaptionLayoutInput input = {text, 7, 40, 12, {"Base", 4}, 9};
    MMCaptionLayoutServices services = {NULL, caption_property, caption_set_font, caption_measure, caption_emit, NULL};
    CaptionRuns runs;
    MMI32 height = -1;
    services.context = &runs;
    runs.count = 0;
    runs.fail_at = -1;
    CHECK(!mm_caption_layout(&input, &services, &height));
    CHECK(height == 27 && runs.count == 4);
    CHECK(runs.characters[0] == 'A' && runs.characters[1] == 'B' &&
          runs.characters[2] == 'C' && runs.characters[3] == 'D');
    CHECK(runs.x[0] == 32 && runs.x[1] == 36 && runs.x[2] == 31 && runs.x[3] == 36);
    CHECK(runs.baseline[0] == 6 && runs.baseline[1] == 11 && runs.baseline[2] == 22 && runs.baseline[3] == 22);
    CHECK(runs.style[0].pixels == 4 && runs.style[0].flags == 5 && runs.style[0].offset == -3 &&
          runs.style[0].font.length == 5 && !memcmp(runs.style[0].font.name, "Alt\0X", 5) &&
          runs.style[0].foreground == 0x01000000U && runs.style[0].background == 1);
    CHECK(runs.style[1].pixels == 255 && runs.style[1].flags == 2 && runs.style[1].offset == 2 &&
          runs.style[1].font.length == 4 && !memcmp(runs.style[1].font.name, "Base", 4) &&
          runs.style[1].foreground == 0x12020304U && runs.style[1].background == 1);
    runs.count = 0;
    runs.fail_at = 1;
    height = -1;
    CHECK(mm_caption_layout(&input, &services, &height) == -70 && runs.count == 1 && height == -1);
    input.length = 0;
    input.text = NULL;
    input.properties = 0;
    runs.count = 0;
    CHECK(!mm_caption_layout(&input, &services, &height) && height == 8 && !runs.count);
    return 0;
}

typedef struct CaptionStops
{
    MMCaptionCaretStop stops[12];
    int count, measured, fail;
} CaptionStops;

static int caret_advance(void *context, const unsigned short *text, MMI32 count, MMI32 *advance)
{
    CaptionStops *stops = (CaptionStops *)context;
    CHECK(count == 1 && text[0] == 'A');
    ++stops->measured;
    if (stops->fail)
        return -77;
    *advance = 6;
    return 0;
}

static int caret_stop(void *context, const MMCaptionCaretStop *value, int replace)
{
    CaptionStops *stops = (CaptionStops *)context;
    CHECK(stops->count < 12 && (!replace || stops->count));
    if (!replace)
        ++stops->count;
    stops->stops[stops->count - 1] = *value;
    return 0;
}

static int test_caption_carets(void)
{
    static const unsigned short text[] = {'A', 0x03a9, '\r', '\n', 0xd83c, 0xdfac, '\t', 'B'};
    static const MMI32 expected[] = {0, 1, 3, -1, 4, -1, 8, 9, 10};
    CaptionStops host = {0};
    MMCaptionLayoutRun run = {text, 0, 2, 4, 10, 23, 4, 16};
    MMCaptionCaretBuilder builder = {NULL, 8, 100, 40, -1, 0};
    MMCaptionCaretServices services = {NULL, caret_advance, caret_stop};
    MMI32 offsets[9];
    builder.offsets = offsets;
    services.context = &host;
    mm_caption_caret_offsets(text, 8, offsets);
    CHECK(!memcmp(offsets, expected, sizeof(offsets)));
    CHECK(!mm_caption_caret_run(&builder, &run, &services));
    CHECK(host.measured == 1 && host.count == 4);
    CHECK(host.stops[0].offset == 0 && host.stops[0].x == 124 && host.stops[0].y == 49);
    CHECK(host.stops[1].offset == 1 && host.stops[1].x == 130 && host.stops[1].height == 16);
    CHECK(host.stops[2].offset == 3 && host.stops[2].x == 137 && host.stops[3].offset == 4);
    run.text = text + 4;
    run.first = 4; run.drawn = 2; run.end = 6;
    run.x = 30; run.right = 40;
    CHECK(!mm_caption_caret_run(&builder, &run, &services));
    CHECK(host.measured == 1 && host.count == 5 && host.stops[3].x == 144 && host.stops[4].offset == 8);
    run.text = text + 6;
    run.first = 6; run.drawn = 0; run.end = 7;
    run.x = 0; run.right = 18; run.top = 20;
    CHECK(!mm_caption_caret_run(&builder, &run, &services));
    CHECK(host.count == 7 && host.stops[5].offset == 8 && host.stops[5].y == 65 && host.stops[6].x == 132);
    run.text = text;
    run.first = 0; run.drawn = 2; run.end = 2;
    host.fail = 1;
    CHECK(mm_caption_caret_run(&builder, &run, &services) == -77 && host.count == 8 && host.measured == 2);
    CHECK(mm_caption_scroll_to_caret(0, 100, 40, 80, 65, 16) == 6);
    CHECK(mm_caption_scroll_to_caret(45, 100, 40, 80, 65, 16) == 20);
    CHECK(mm_caption_scroll_to_caret(45, 30, 40, 80, 65, 16) == 0);
    return 0;
}

int main(void)
{
    Host h;
    MMRichTextProvider p;
    MMRichTextRun r;
    int i, maximum, status;
    MMU32 ch;
    CHECK(!test_caption_layout());
    CHECK(!test_caption_carets());
    for (ch = 0; ch < 65536U; ++ch)
    {
        MMU32 expected = ch == '\r' ? 22 : ch == '\n' ? 24 : ch == '\t' ? 52 :
                         ch == ' ' ? 5 : ch < 32 || ch == 127 ? 16 : 0;
        CHECK(mm_rich_text_flags(ch) == expected);
    }
    p = setup(&h, "AA BB\r\n");
    CHECK(!mm_rich_text_line(&p, 18, 0, 7));
    CHECK(h.line.ccp == 3 && h.line.dyp == 16 && h.line.dypAscent == 13 && h.line.dypTot == 7);
    CHECK(!mm_rich_text_line(&p, 18, 3, 23));
    CHECK(h.line.ccp == 4 && h.line.dypTot == 23);
    p = setup(&h, "\tA\r"); h.paragraph.justification = 2;
    h.paragraph.lineScale = 128; h.paragraph.extraLine = 3;
    h.paragraph.afterScale = 512; h.paragraph.extraAfter = 1;
    CHECK(!mm_rich_text_line(&p, 100, 0, 0));
    CHECK(h.line.xpLeft == 38 && h.line.ccp == 3 && h.line.dyp == 23);
    p = setup(&h, "A\r"); h.paragraph.lineScale = -129; h.paragraph.extraLine = 10;
    CHECK(!mm_rich_text_line(&p, 100, 0, 0)); CHECK(h.line.dyp == 2);
    /* Signed short spacing wraps before the paragraph multiplier. */
    h.paragraph.lineScale = 256; h.paragraph.extraLine = 32760;
    h.paragraph.afterScale = -256;
    CHECK(!mm_rich_text_line(&p, 100, 0, 0)); CHECK(h.line.dyp == 32760);
    h.paragraph.extraLine = 0; h.paragraph.afterScale = 256;
    /* Large multipliers must use the full product before division. */
    h.ascent = 32765; h.descent = 2; h.paragraph.lineScale = 2147483647L; h.paragraph.extraLine = 200;
    CHECK(!mm_rich_text_line(&p, 100, 0, 0)); CHECK(h.line.dyp == 72);
    h.paragraph.lineScale = (-2147483647L - 1);
    CHECK(!mm_rich_text_line(&p, 100, 0, 0)); CHECK(h.line.dyp == 200);
    p = setup(&h, "\1\nA\r"); h.object = 1;
    memset(&r, 0, sizeof(r));
    CHECK(!mm_rich_text_run_init(&r, &p, &h.paragraph, 0, h.length, 0, 5, 5));
    CHECK(!mm_rich_text_run_next(&r, 0)); CHECK(r.data.cpLim == 0 && !r.object);
    CHECK(!mm_rich_text_run_next(&r, 1));
    CHECK(r.object && r.lineBreak && r.data.cpLim == 2 && r.data.xpLim == 11 && r.ascent == 23);
    /* Object flags survive reinitialization followed by an empty next call. */
    CHECK(!mm_rich_text_run_init(&r, &p, &h.paragraph, 4, 4, 0, 5, 5));
    CHECK(!r.lineBreak && r.object);
    CHECK(!mm_rich_text_run_next(&r, 0)); CHECK(r.object);
    p = setup(&h, "\t\tA\r");
    memset(&r, 0, sizeof(r));
    CHECK(!mm_rich_text_run_init(&r, &p, &h.paragraph, 0, h.length, 0, 1, 1));
    CHECK(!mm_rich_text_run_next(&r, 1));
    CHECK(r.data.cpLim == 1 && r.data.xpLim == 18 && r.data.xpLimDraw == 0 && r.lineBreak);
    p = setup(&h, "");
    h.length = h.styleLimit = 260;
    for (i = 0; i < 260; ++i) h.text[i] = i < 127 ? 0x3a9 : i < 259 ? '\n' : '\r';
    memset(&r, 0, sizeof(r));
    CHECK(!mm_rich_text_run_init(&r, &p, &h.paragraph, 0, 128, 0, 10000, 10000));
    CHECK(!mm_rich_text_run_next(&r, 1));
    CHECK(r.data.cpLim == 259 && r.data.cpLimDraw == 127);
    /* Errors stop immediately, preserve the exact callback error and never
     * publish a completed line. Exercise every callback of a successful line. */
    p = setup(&h, "AV AV\r"); CHECK(!mm_rich_text_line(&p, 18, 0, 7)); maximum = h.calls;
    for (i = 1; i <= maximum; ++i)
    {
        p = setup(&h, "AV AV\r"); h.failAt = i;
        status = mm_rich_text_line(&p, 18, 0, 7);
        CHECK(status == -73 && h.calls == i && h.published != 3);
        CHECK(h.line.cpMin == -99 && h.line.dypAscent == -99);
    }
    p = setup(&h, "A\r"); h.styleLimit = 0;
    CHECK(!mm_rich_text_line(&p, 100, 0, 0));
    CHECK(h.invalid == 1 && h.line.ccp == 0 && h.line.dyp == 1 && h.line.dypAscent == 1);
    p.unitBytes = 4; memset(&r, 0, sizeof(r));
    CHECK(mm_rich_text_run_init(&r, &p, &h.paragraph, 0, 0, 0, 0, 0) < 0);
    puts("C89 rich-text runs, wrapping, spacing, objects, UTF-16, failures and flags passed");
    return 0;
}
