#include "mm/caption_layout.h"
#include "mm/rich_text_layout.h"
#include <string.h>

typedef struct CaptionLayout
{
    const MMCaptionLayoutInput *input;
    const MMCaptionLayoutServices *services;
    MMI32 length;
    MMCaptionStyle style;
    MMRichTextLine line;
} CaptionLayout;

static MMI32 signed_field(MMU32 value, MMU32 range)
{
    value &= range - 1U;
    return value < range / 2U ? (MMI32)value : (MMI32)value - (MMI32)range;
}

static MMI32 fetch_style(CaptionLayout *c, MMI32 cp)
{
    const MMCaptionLayoutInput *in = c->input;
    MMCaptionStyle *s = &c->style;
    MMCaptionProperty p;
    MMI32 limit = c->length, delta;
    size_t i;
    s->font = in->font;
    s->pixels = in->pixels;
    s->flags = s->offset = 0;
    s->foreground = 0x01000000U;
    s->background = 0xffffffffU;
    for (i = 0; i < in->properties; ++i)
    {
        c->services->property(c->services->context, i, &p);
        if (p.kind < 1 || p.kind > 4)
            continue;
        if (p.position > (MMU32)cp)
        {
            if (p.position < (MMU32)limit)
                limit = (MMI32)p.position;
            continue;
        }
        switch (p.kind)
        {
        case 1:
            s->flags = (MMI32)(p.value & 255U);
            s->offset = signed_field(p.value >> 8, 256U);
            delta = signed_field(p.value >> 16, 65536U);
            s->pixels = in->pixels < 4 - delta ? 4 : in->pixels > 255 - delta ? 255 : in->pixels + delta;
            break;
        case 2: s->font = p.font.length ? p.font : in->font; break;
        case 3: s->foreground = 0x01000000U + p.value; break;
        case 4: s->background = 0xffffffffU + p.value; break;
        }
    }
    return limit;
}

static MMI32 fetch_paragraph(const CaptionLayout *c, MMI32 cp, MMRichTextParagraph *pap)
{
    static const MMRichTextParagraph defaults = {0, 0, 18, 256, 256, 0, 0};
    MMCaptionProperty p;
    MMI32 limit = c->length;
    size_t i;
    *pap = defaults;
    for (i = 0; i < c->input->properties; ++i)
    {
        c->services->property(c->services->context, i, &p);
        if (p.kind < 128 || p.kind > 130)
            continue;
        if (p.position > (MMU32)cp)
        {
            if (p.position < (MMU32)limit)
                limit = (MMI32)p.position;
            continue;
        }
        switch (p.kind)
        {
        case 128:
            pap->justification = (MMI32)(p.value >> 24);
            pap->indent = (MMI32)((p.value >> 16) & 255U);
            pap->tab = 18 + signed_field(p.value, 65536U);
            if (pap->tab < 1)
                pap->tab = 1;
            break;
        case 129:
            pap->lineScale = 256 + signed_field(p.value >> 16, 65536U);
            pap->extraLine = signed_field(p.value, 65536U);
            break;
        case 130:
            pap->afterScale = 256 + signed_field(p.value >> 16, 65536U);
            pap->extraAfter = signed_field(p.value, 65536U);
            break;
        }
    }
    return limit;
}

static unsigned short text_at(const CaptionLayout *c, MMI32 cp)
{
    return cp == c->input->length ? '\r' : c->input->text[cp];
}

static int paragraph_start(const CaptionLayout *c, MMI32 cp)
{
    MMU32 flags;
    if (cp <= 0)
        return cp == 0;
    if (cp >= c->length || (mm_rich_text_flags(text_at(c, cp)) & MM_RICH_IGNORE))
        return 0;
    while (cp-- > 0)
    {
        flags = mm_rich_text_flags(text_at(c, cp));
        if (flags & MM_RICH_BREAK)
            return 1;
        if (!(flags & MM_RICH_IGNORE))
            return 0;
    }
    return 1;
}

static int provide(void *context, MMRichTextOperation op, MMRichTextRequest *q)
{
    CaptionLayout *c = (CaptionLayout *)context;
    const MMCaptionLayoutServices *s = c->services;
    unsigned short *out;
    MMI32 i;
    switch (op)
    {
    case MM_RICH_SET_FONT: return s->set_font(s->context, &c->style);
    case MM_RICH_MEASURE:
        q->left = 0;
        return s->measure(s->context, q->characters, q->count, &q->right, &q->top, &q->bottom);
    case MM_RICH_OFFSET: q->value = c->style.offset; break;
    case MM_RICH_FLAGS: q->flags = mm_rich_text_flags(q->character); break;
    case MM_RICH_LENGTH: q->value = c->length; break;
    case MM_RICH_PARAGRAPH_START: q->value = paragraph_start(c, q->cp); break;
    case MM_RICH_FETCH:
        out = (unsigned short *)q->characters;
        for (i = 0; i < q->count; ++i)
            out[i] = text_at(c, q->cp + i);
        break;
    case MM_RICH_OBJECT: q->value = 0; break;
    case MM_RICH_FETCH_STYLE: q->value = fetch_style(c, q->cp); break;
    case MM_RICH_FETCH_PARAGRAPH: q->value = fetch_paragraph(c, q->cp, &q->paragraph); break;
    case MM_RICH_PUBLISH:
        if (q->published & 2)
            c->line = q->line;
        break;
    case MM_RICH_COPY_STYLE: /* Captions have no objects that mutate the style. */
    case MM_RICH_WARM_FONT:
    case MM_RICH_INVALID_STYLE:
        break;
    }
    return 0;
}

static int emit_line(CaptionLayout *c, const MMRichTextProvider *provider, MMI32 cp, MMI32 y)
{
    MMRichTextParagraph paragraph;
    MMRichTextRun run;
    MMI32 limit = cp + c->line.ccp, style_limit = cp, x = 0;
    MMI32 width = c->input->width;
    int status;
    memset(&run, 0, sizeof(run));
    fetch_paragraph(c, cp, &paragraph);
    while (cp < limit)
    {
        if (cp >= style_limit)
        {
            style_limit = fetch_style(c, cp);
            if (style_limit > limit)
                style_limit = limit;
            status = mm_rich_text_run_init(&run, provider, &paragraph, cp, style_limit, x, width, width);
            if (status < 0)
                return status;
        }
        status = mm_rich_text_run_next(&run, 1);
        if (status < 0)
            return status;
        if (run.data.cpLimDraw > cp)
        {
            status = c->services->emit(c->services->context, &c->style, provider->characters,
                                       run.data.cpLimDraw - run.cpMin, c->line.xpLeft + run.xpMin,
                                       y + c->line.dypAscent + c->style.offset);
            if (status < 0)
                return status;
        }
        if (run.lineBreak)
            break;
        if (run.data.cpLim <= cp)
            return MM_CAPTION_LAYOUT_STALLED;
        cp = run.data.cpLim;
        x = run.data.xpLim;
    }
    return 0;
}

int mm_caption_layout(const MMCaptionLayoutInput *in, const MMCaptionLayoutServices *s, MMI32 *height)
{
    CaptionLayout c;
    MMRichTextProvider p;
    unsigned short characters[MM_RICH_BUFFER];
    MMI32 cp = 0, y = 0;
    int status;
    if (!in || !s || !height || in->length < 0 || in->length > 65536L || in->width <= 0 ||
        (in->length && !in->text) || (in->properties && !s->property) || !s->set_font || !s->measure || !s->emit)
        return MM_CAPTION_LAYOUT_FAILED;
    c.input = in;
    c.services = s;
    c.length = in->length + 1;
    p.context = &c;
    p.call = provide;
    p.characters = characters;
    p.unitBytes = sizeof(characters[0]);
    while (cp < c.length)
    {
        status = mm_rich_text_line(&p, in->width, cp, y);
        if (status < 0)
            return status;
        if (c.line.ccp <= 0)
            return MM_CAPTION_LAYOUT_STALLED;
        status = emit_line(&c, &p, cp, y);
        if (status < 0)
            return status;
        cp += c.line.ccp;
        y += c.line.dyp;
    }
    *height = y;
    return 0;
}
