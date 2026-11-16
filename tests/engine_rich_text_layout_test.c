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
int main(void)
{
    Host h;
    MMRichTextProvider p;
    MMRichTextRun r;
    int i, maximum, status;
    MMU32 ch;
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
